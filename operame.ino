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
    mhz.setFilter(true, true);
    mhz.autoCalibration();

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

void check_sensor() {
    if (mhz.errorCode == RESULT_OK) return;
    while (1) {
        delay(1000);
        mhz.verify();
        if (mhz.errorCode == RESULT_OK) return;
        display_big("sensorfout", TFT_RED);
    }
}

void loop() {
    static unsigned long previous_mqtt = 0;
    unsigned long start = millis();

    if (mqtt_enabled) mqtt.loop();

    int CO2 = mhz.getCO2();
    check_sensor();

    Serial.println(CO2);

    if (CO2) {
        display_ppm(CO2);

        if (mqtt_enabled && millis() - previous_mqtt >= mqtt_interval) {
            previous_mqtt = millis();
            connect_mqtt();
            String message = mqtt_template;
            message.replace("{}", String(CO2));
            retain(mqtt_topic, message);
        }
    } else {
        display_big("wacht...");
    }

    while (millis() - start < 6000) {
        if (CO2) display_ppm(CO2);  // repeat, for blinking
        if (ota_enabled) ArduinoOTA.handle();
        check_buttons();
        delay(20);
    }
    Serial.println(esp_get_free_heap_size());
}
