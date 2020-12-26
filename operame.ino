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
#include <list>

using namespace std;

unsigned long mqtt_interval;
const int portalbutton = 35;
const int demobutton = 0;
bool ota_enabled;
int co2_warning;
int co2_critical;
int co2_blink;

enum Driver { AQC, MHZ };
Driver driver;

int mhz_co2_init = 410;  // magic value reported while initializing

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

struct Timer {
    unsigned long previous;
    unsigned long interval;
    std::function<void()> function;
    void operator()() {
        if (millis() - previous >= interval) {
            function();
            previous = millis();
        }
    }
    Timer(unsigned long ms, std::function<void()> f)
        : interval(ms), function(f) {}
};

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

void display_lines(const std::list<String>& lines, int fg = TFT_WHITE, int bg = TFT_BLACK) {
    sprite.setTextSize(1);
    sprite.setTextFont(4);
    sprite.setTextDatum(MC_DATUM);
    sprite.setTextColor(fg, bg);
    sprite.fillSprite(bg);
    if (WiFi.status() == WL_CONNECTED)
        sprite.drawRect(0, 0, display.width(), display.height(), TFT_BLUE);
    const int line_height = 32;
    int y = display.height()/2 - (lines.size()-1) * line_height/2;
    for (auto line : lines) {
        sprite.drawString(line, display.width()/2, y);
        y += line_height;
    }
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
        if (!digitalRead(demobutton)) {
            display_logo();
            delay(500);
            while (!digitalRead(demobutton));
            return;
        }
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

    if (aqc_get_co2() >= 0) {
        driver = AQC;
        hwserial1.setTimeout(100);
        Serial.println("Using AQC driver.");
    } else {
        driver = MHZ;
        mhz_setup();
        Serial.println("Using MHZ driver.");
    }

    display_logo();
    delay(2000); 

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
    max_failures  = WiFiSettings.integer("operame_max_failures", 0, 1000, 10, "Aantal verbindingsfouten voor automatische herstart");
    mqtt_topic  = WiFiSettings.string("operame_mqtt_topic", WiFiSettings.hostname, "Topic");
    mqtt_interval = 1000UL * WiFiSettings.integer("operame_mqtt_interval", 10, 3600, 60, "Publicatie-interval [s]");
    mqtt_template = WiFiSettings.string("operame_mqtt_template", "{} PPM", "Berichtsjabloon");
    WiFiSettings.info("De {} in het sjabloon wordt vervangen door de gemeten waarde.");

    if (ota_enabled) WiFiSettings.onPortal = setup_ota;

    WiFiSettings.onConnect = [] {
        check_buttons();
        display_big("Verbinden met WiFi...", TFT_BLUE);
        return 50;
    };
    WiFiSettings.onFailure = [] {
        display_big("WiFi mislukt!", TFT_RED);
        delay(2000);
    };
    static int portal_phase = 0;
    WiFiSettings.onPortalView = [] {
        if (portal_phase < 2) portal_phase = 2;
    };
    WiFiSettings.onConfigSaved = [] {
        portal_phase = 3;
    };
    WiFiSettings.onPortalWaitLoop = [] {
        if (WiFi.softAPgetStationNum() == 0) portal_phase = 0;
        else if (! portal_phase) portal_phase = 1;

        switch (portal_phase) {
            case 0: {
                display_lines({
                    "Voor configuratie,",
                    "verbind met WiFi",
                    "\"" + WiFiSettings.hostname + "\"",
                    "met een smartphone."
                }, TFT_WHITE, TFT_BLUE);
                break ;
            }
            case 1: {
                display_lines({
                    "Volg instructies op",
                    "uw smartphone.",
                    "(inlog-notificatie)"
                }, TFT_WHITE, TFT_BLUE);
                break;
            }
            case 2: {
                display_lines({
                    "Wijzig instellingen",
                    "en klik op \"Save\".",
                    "(rechtsonder)"
                }, TFT_WHITE, TFT_BLUE);
                break;
            }
            case 3: {
                display_lines({
                    "Wijzig instellingen",
                    "en klik op \"Save\".",
                    "Of \"Restart device\"",
                    "als u klaar bent."
                }, TFT_WHITE, TFT_BLUE);
                break;
            }
        }
        if (portal_phase == 0 && millis() > 10*60*1000) ESP.restart();

        if (ota_enabled) ArduinoOTA.handle();
        if (!digitalRead(portalbutton)) {
            delay(50);
            if (!digitalRead(portalbutton)) ESP.restart();
        }
    };

    if (wifi_enabled) WiFiSettings.connect(false, 15);

    static WiFiClient wificlient;
    if (mqtt_enabled) mqtt.begin(server.c_str(), port, wificlient);

    if (ota_enabled) setup_ota();

    display_big(":-)");
}

void connect_mqtt() {
    if (mqtt.connected()) return;  // already/still connected

    static int failures = 0;
    if (mqtt.connect(WiFiSettings.hostname.c_str())) {
        failures = 0;
    } else {
        failures++;
        if (failures >= max_failures) ESP.restart();
    }
}

int aqc_get_co2() {
    static bool initialized = false;

    const uint8_t command[9] = { 0xff, 0x01, 0xc5, 0, 0, 0, 0, 0, 0x3a };
    char response[9];
    int co2 = -1;

    for (int attempt = 0; attempt < 3; attempt++) {
        hwserial1.flush();
        int limit = 20;  // .available() sometimes stays true
        while(hwserial1.available() && --limit) hwserial1.read();

        hwserial1.write(command, sizeof(command));
        delay(50);
        int c = hwserial1.readBytes(response, sizeof(response));
        if (c != sizeof(response) || response[0] != 0xff || response[1] != 0x86) {
            continue;
        }
        uint8_t checksum = 255;
        for (int i = 0; i < sizeof(response) - 1; i++) {
            checksum -= response[i];
        }
        if (response[8] == checksum) {
            co2 = response[2] * 256 + response[3];
            break;
        }
        delay(50);
    }

    if (co2 < 0) {
        initialized = false;
        return co2;
    }

    if (!initialized && (co2 == 9999 || co2 == 400)) return 0;
    initialized = true;
    return co2;
}

void mhz_setup() {
    mhz.begin(hwserial1);
    // mhz.setFilter(true, true);  Library filter doesn't handle 0436
    mhz.autoCalibration(true);
    char v[5];
    mhz.getVersion(v);
    v[4] = '\0';
    if (strcmp("0436", v) == 0) mhz_co2_init = 436;
}

int mhz_get_co2() {
    int co2       = mhz.getCO2();
    int unclamped = mhz.getCO2(false);

    if (mhz.errorCode != RESULT_OK) {
        delay(500);
        mhz_setup();
        return -1;
    }

    // reimplement filter from library, but also checking for 436 because our
    // sensors (firmware 0436, coincidence?) return that instead of 410...
    if (unclamped == mhz_co2_init && co2 - unclamped >= 10) return 0;

    // No known sensors support >10k PPM (library filter tests for >32767)
    if (co2 > 10000 || unclamped > 10000) return 0;

    return co2;
}

int get_co2() {
    if (driver == AQC) return aqc_get_co2();
    if (driver == MHZ) return mhz_get_co2();

    // Should be unreachable
    ESP.restart();
    return -1;  // suppress warning
}


void loop() {
    static int co2;

    static Timer read_sensor {
        5000,
        [] {
            co2 = get_co2();
            Serial.println(co2);
        }
    };
    read_sensor();

    static Timer display {
        50,
        [] {
            if (co2 < 0) {
                display_big("sensorfout", TFT_RED);
            } else if (co2 == 0) {
                display_big("wacht...");
            } else {
                // some MH-Z19's go to 10000 but the display has space for 4 digits
                display_ppm(co2 > 9999 ? 9999 : co2);
            }
        }
    };
    display();

    static Timer publish {
        mqtt_interval,
        [] {
            if (co2 <= 0) return;
            connect_mqtt();
            String message = mqtt_template;
            message.replace("{}", String(co2));
            retain(mqtt_topic, message);
        }
    };
    if (mqtt_enabled) {
        mqtt.loop();
        publish();
    }

    if (ota_enabled) ArduinoOTA.handle();
    check_buttons();
}
