#define Sprintf(f, ...) ({ char* s; asprintf(&s, f, __VA_ARGS__); String r = s; free(s); r; })
#include <WiFi.h>
#include <MQTT.h>
#include <SPIFFS.h>
#include <WiFiSettings.h>
#include <MHZ19.h>
#include <ArduinoOTA.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <logo.h>
#include <deque>
#include <algorithm>

using namespace std;

unsigned long mqtt_interval;
const int portalbutton = 35;
const int demobutton = 0;
bool ota_enabled;
int co2_warning;                   // [PPM]
int co2_critical;                  // [PPM]
int co2_blink;                     // [PPM]
int co2_baseline;                  // set by ABC [PPM]
const int tick = 5000;             // duration of one loop() iteration [ms]
const int co2_assumed = 420;       // outdoor PPM [PPM]
const int co2_window = 2*60*1000;  // for moving average [ms]
const int co2_size = co2_window / tick;
deque<int> co2_history;

int co2_init = 410;  // magic value reported by sensor during startup

MQTTClient mqtt;
HardwareSerial hwserial1(1);
TFT_eSPI display;
TFT_eSprite sprite(&display);
MHZ19 mhz;
String mqtt_topic;
String mqtt_template;
bool add_units;
bool wifi_enabled;
bool mqtt_enabled;
int max_failures;

deque<int> abc_history;
const int abc_maxdev = 50;  // max. deviation to consider a straight line [PPM]
const int abc_window = 12;           // number of samples for moving window
const int abc_interval = 5*60*1000;  // sample interval [ms]
const int abc_minchange = 10;        // don't do tiny updates [PPM]
int abc_size = 8 * 24 * (60 / 5);    // number of samples

String slurp(const String& fn) {
    File f = SPIFFS.open(fn, "r");
    String r = f.readString();
    f.close();
    return r;
}

bool spurt(const String& fn, const String& content) {
    File f = SPIFFS.open(fn, "w");
    if (!f) return false;
    auto w = f.print(content);
    f.close();
    return w == content.length();
}

void retain(String topic, String message) {
    Serial.printf("%s %s\n", topic.c_str(), message.c_str());
    mqtt.publish(topic, message, true, 0);
}

void display_big(const String& text, int fg = TFT_WHITE, int bg = TFT_BLACK) {
    sprite.setTextSize(1);
    bool nondigits = false;
    for (int i = 0; i < text.length(); i++) {
        char c = text.charAt(i);
        if (c < '0' || c > '9') nondigits = true;
    }
    sprite.setTextFont(nondigits ? 4 : 8);
    sprite.setTextSize(nondigits && text.length() < 10 ? 2 : 1);
    sprite.setTextDatum(MC_DATUM);
    sprite.setTextColor(fg, bg);
    sprite.fillSprite(bg);
    if (WiFi.status() == WL_CONNECTED)
        sprite.drawRect(0, 0, display.width(), display.height(), TFT_BLUE);
    sprite.drawString(text, display.width()/2, display.height()/2);

    sprite.pushSprite(0, 0);
}

void display_logo() {
    sprite.setSwapBytes(true);
    sprite.fillSprite(TFT_BLACK);
    sprite.pushImage(12, 30, 215, 76, OPERAME_LOGO);
    if (WiFi.status() == WL_CONNECTED)
        sprite.drawRect(0, 0, display.width(), display.height(), TFT_BLUE);

    sprite.pushSprite(0, 0);
}

void setup_ota() {
    ArduinoOTA.setHostname(WiFiSettings.hostname.c_str());
    ArduinoOTA.setPassword(WiFiSettings.password.c_str());
    ArduinoOTA.onStart(   []()              { display_big("OTA", TFT_BLUE); });
    ArduinoOTA.onEnd(     []()              { display_big("OTA done", TFT_GREEN); });
    ArduinoOTA.onError(   [](ota_error_t e) { display_big("OTA failed", TFT_RED); });
    ArduinoOTA.onProgress([](unsigned int p, unsigned int t) {
        String pct { (int) ((float) p / t * 100) };
        display_big(pct + "%");
    });
    ArduinoOTA.begin();
}

void check_portalbutton() {
    if (digitalRead(portalbutton)) return;
    delay(50);
    if (digitalRead(portalbutton)) return;
    WiFiSettings.portal();
}

void check_demobutton() {
    if (digitalRead(demobutton)) return;
    delay(50);
    if (digitalRead(demobutton)) return;
    ppm_demo();
}

void check_buttons() {
    check_portalbutton();
    check_demobutton();
}

void display_ppm(int ppm) {
    int fg, bg;
    if (ppm >= co2_critical) {
        fg = TFT_WHITE;
        bg = TFT_RED;
    } else if (ppm >= co2_warning) {
        fg = TFT_BLACK;
        bg = TFT_YELLOW;
    } else {
        fg = TFT_GREEN;
        bg = TFT_BLACK;
    }

    if (ppm >= co2_blink && millis() % 2000 < 1000) {
        std::swap(fg, bg);
    }
    display_big(String(ppm), fg, bg);
}

void ppm_demo() {
    display_big("demo!");
    delay(3000);
    display_logo();
    delay(1000);
    for (int p = 400; p < 1200; p++) {
        display_ppm(p);
        delay(30);
    }
    display_logo();
    delay(5000);
}

void setup() {
    Serial.begin(115200);
    Serial.println("Operame start");
    SPIFFS.begin(true);
    pinMode(portalbutton, INPUT_PULLUP);
    pinMode(demobutton, INPUT_PULLUP);
    pinMode(4, OUTPUT);
    digitalWrite(4, HIGH);

    display.init();
    display.fillScreen(TFT_BLACK);
    display.setRotation(1);
    sprite.createSprite(display.width(), display.height());

    pinMode(12, INPUT_PULLUP);
    while (digitalRead(12)) {
        display_big("module verkeerd om!", TFT_RED);
        delay(1000);
    }

    hwserial1.begin(9600, SERIAL_8N1, 27, 26);
    mhz.begin(hwserial1);

    display_logo();
    delay(2000); 

    check_sensor();
    mhz.autoCalibration(false);  // ours works better

    // mhz.setFilter(true, true);  Library filter doesn't handle 0436
    char v[5];
    mhz.getVersion(v);
    v[4] = '\0';
    if (strcmp("0436", v) == 0) co2_init = 436;
    Serial.printf("MH-Z19 firmware version %s\n", v);

    WiFiSettings.hostname = "operame-";
    wifi_enabled  = WiFiSettings.checkbox("operame_wifi", false, "WiFi-verbinding gebruiken");
    ota_enabled   = WiFiSettings.checkbox("operame_ota", false, "Draadloos herprogrammeren inschakelen. (Gebruikt portaalwachtwoord!)") && wifi_enabled;

    WiFiSettings.heading("CO2-niveaus");
    co2_warning   = WiFiSettings.integer("operame_co2_warning", 400, 5000, 700, "Geel vanaf [ppm]");
    co2_critical  = WiFiSettings.integer("operame_co2_critical",400, 5000, 800, "Rood vanaf [ppm]");
    co2_blink     = WiFiSettings.integer("operame_co2_blink",   800, 5000, 800, "Knipperen vanaf [ppm]");

    WiFiSettings.heading("MQTT");
    mqtt_enabled  = WiFiSettings.checkbox("operame_mqtt", false, "Metingen via het MQTT-protocol versturen") && wifi_enabled;
    String server = WiFiSettings.string("mqtt_server", 64, "", "Broker");
    int port      = WiFiSettings.integer("mqtt_port", 0, 65535, 1883, "Broker TCP-poort");
    max_failures  = WiFiSettings.integer("operame_max_failures", 0, 1000, 100, "Aantal verbindingsfouten voor automatische herstart (0 = nooit)");
    mqtt_topic  = WiFiSettings.string("operame_mqtt_topic", WiFiSettings.hostname, "Topic");
    mqtt_interval = 1000UL * WiFiSettings.integer("operame_mqtt_interval", 10, 3600, 60, "Publicatie-interval [s]");
    mqtt_template = WiFiSettings.string("operame_mqtt_template", "{} PPM", "Berichtsjabloon");
    WiFiSettings.info("De {} in het sjabloon wordt vervangen door de gemeten waarde.");

    if (ota_enabled) WiFiSettings.onPortal = setup_ota;

    WiFiSettings.onConnect = []() {
        check_buttons();
        display_big("Verbinden met WiFi...", TFT_BLUE);
        return 50;
    };
    WiFiSettings.onFailure = []() {
        display_big("WiFi mislukt!", TFT_RED);
        delay(2000);
    };
    WiFiSettings.onPortal = []() {
        display_big("Configuratieportal", TFT_BLUE);
    };
    WiFiSettings.onPortalWaitLoop = []() {
        if (ota_enabled) ArduinoOTA.handle();
    };

    if (wifi_enabled) WiFiSettings.connect(false, 15);

    static WiFiClient wificlient;
    if (mqtt_enabled) mqtt.begin(server.c_str(), port, wificlient);

    if (ota_enabled) setup_ota();

    display_big(":-)");

    co2_baseline = slurp("/operame_baseline").toInt();
    if (co2_baseline) {
        // Populate ABC so future "lowest" has to compete with existing baseline
        for (int i=0; i < abc_window; i++) abc_history.push_back(co2_baseline);
    } else {
        co2_baseline = co2_assumed;
    }
    Serial.printf("Initial CO2 baseline: %d PPM.\n", co2_baseline);
}

void connect_mqtt() {
    if (mqtt.connected()) return;  // already/still connected

    static int failures = 0;
    if (WiFi.status() == WL_CONNECTED && mqtt.connect(WiFiSettings.hostname.c_str())) {
        failures = 0;
    } else {
        failures++;
        if (max_failures && failures >= max_failures) ESP.restart();
    }
}

void check_sensor() {
    if (mhz.errorCode == RESULT_OK) return;
    while (1) {
        delay(1000);
        mhz.verify();
        if (mhz.errorCode == RESULT_OK) return;
        display_big("sensorfout", TFT_RED);
    }
}

void abc() {
    if (abc_history.size() < abc_window) return;

    int lowest = 99999;
    auto begin = abc_history.begin();
    auto end = abc_history.end() - abc_window;
    for (auto it = begin; it != end; ++it) {
        auto wend = it + abc_window;
        auto minmax = std::minmax_element(it, wend);
        if (abs(*minmax.first - *minmax.second) > abc_maxdev) continue;
        int sum = 0;
        for (auto sit = it; sit != wend; ++sit) sum += *sit;
        int avg = sum / abc_window;
        if (avg < lowest) lowest = avg;
    }
    if (lowest != 99999  // whether a straight line has been found at all
        && abs(co2_baseline - lowest) >= abc_minchange
    ) {
        co2_baseline = lowest;
        Serial.printf("New CO2 baseline: %d PPM.\n", co2_baseline);
        spurt("/operame_baseline", String(co2_baseline));
    }
}

int moving_average() {
    int sum = 0;
    auto end = co2_history.end();
    for (auto it = co2_history.begin(); it != end; ++it) sum += *it;
    return sum / co2_history.size();
}

void loop() {
    unsigned long start = millis();

    if (mqtt_enabled) mqtt.loop();

    int CO2       = mhz.getCO2();
    int unclamped = mhz.getCO2(false);

    // reimplement filter from library, but also checking for 436 because our
    // sensors (firmware 0436, coincidence?) return that instead of 410...
    if (unclamped == co2_init && CO2 - unclamped >= 10) CO2 = 0;

    // No known sensors support >10k PPM (library filter tests for >32767)
    if (CO2 > 10000 || unclamped > 10000) CO2 = 0;

    check_sensor();

    if (CO2) {
        // Moving average
        if (co2_history.size() == co2_size) co2_history.pop_front();
        co2_history.push_back(CO2);
        int smoothed = moving_average();

        // Automatic Baseline Calibration
        static unsigned long previous_abc = 0;
        if (millis() - previous_abc >= abc_interval) {
            previous_abc = millis();
            if (abc_history.size() == abc_size) abc_history.pop_front();
            abc_history.push_back(CO2);
        }
        abc();

        // Apply ABC
        int display_co2 = smoothed + co2_assumed - co2_baseline;
        int current_co2 = CO2      + co2_assumed - co2_baseline;

        // some MH-Z19's go to 10000 but the display has space for 4 digits
        if (display_co2 > 9999) CO2 = 9999;

        Serial.printf(
            "%d %s %d = %d; moving average based on %d samples is %d PPM.\n",
            CO2,
            (current_co2 > CO2 ? "+" : "-"),
            abs(co2_assumed - co2_baseline),
            current_co2,
            co2_history.size(),
            display_co2
        );
        display_ppm(display_co2);

        static unsigned long previous_mqtt = 0;
        if (mqtt_enabled && millis() - previous_mqtt >= mqtt_interval) {
            previous_mqtt = millis();
            connect_mqtt();
            String message = mqtt_template;
            message.replace("{}", String(display_co2));
            retain(mqtt_topic, message);
        }

        CO2 = display_co2;  // for repeated displaying
    } else {
        display_big("wacht...");
    }

    while (millis() - start < tick) {
        if (CO2) display_ppm(CO2);  // repeat, for blinking
        if (ota_enabled) ArduinoOTA.handle();
        check_buttons();
        delay(20);
    }

    Serial.println(esp_get_free_heap_size());
}
