#include <Arduino.h>
#include <WiFi.h>
#include <MQTT.h>
#include <SPIFFS.h>
#include <WiFiSettings.h>
#include <ArduinoOTA.h>
#include <src/strings.h>
#include <src/display.h>
#include <src/sensors.h>

#define LANGUAGE "nl"
OperameLanguage::Texts T;

MQTTClient      mqtt;
HardwareSerial  hwserial1(1);
CO2Sensor       *sensor;

const int       pin_portalbutton = 35;
const int       pin_demobutton   = 0;
const int       pin_backlight    = 4;
const int       pin_sensor_rx    = 27;
const int       pin_sensor_tx    = 26;
const int       pin_pcb_ok       = 12;   // pulled to GND by PCB trace

// Configuration via WiFiSettings
unsigned long   mqtt_interval;
bool            ota_enabled;
int             co2_warning;
int             co2_critical;
int             co2_blink;
String          mqtt_topic;
String          mqtt_template;
bool            add_units;
bool            wifi_enabled;
bool            mqtt_enabled;
int             max_failures;

bool button(int pin) {
    if (digitalRead(pin)) return false;
    unsigned long start = millis();
    while (!digitalRead(pin)) {
        if (millis() - start >= 50) display("");
    }
    return millis() - start >= 50;
}

void calibrate() {
    auto lines = T.calibration;
    for (int count = 60; count >= 0; count--) {
        lines.back() = String(count);
        display(lines, TFT_RED);
        unsigned long start = millis();
        while (millis() - start < 1000) {
            if (button(pin_demobutton) || button(pin_portalbutton)) return;
        }
    }

    lines = T.calibrating;
    for (auto& line : lines) line.replace("400", String(sensor->co2_zero));
    display(lines, TFT_MAGENTA);

    sensor->set_zero();  // actually instantaneous
    delay(15000);  // give time to read long message
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
    display(String(ppm), fg, bg);
}

void ppm_demo() {
    display("demo!");
    delay(3000);
    display_logo();
    delay(1000);
    int buttoncounter = 0;
    for (int p = 400; p < 1200; p++) {
        display_ppm(p);
        if (button(pin_demobutton)) {
            display_logo();
            delay(500);
            return;
        }

        // Hold portal button from 700 to 800 for manual calibration
        if (p >= 700 && p < 800 && !digitalRead(pin_portalbutton)) {
            buttoncounter++;
        }
        if (p == 800 && buttoncounter >= 85) {
            while (!digitalRead(pin_portalbutton)) delay(100);
            calibrate();
            display_logo();
            delay(500);
            return;
        }
        delay(30);
    }
    display_logo();
    delay(5000);
}

void check_portalbutton() {
    if (button(pin_portalbutton)) WiFiSettings.portal();
}

void check_demobutton() {
    if (button(pin_demobutton)) ppm_demo();
}

void setup_ota() {
    ArduinoOTA.setHostname(WiFiSettings.hostname.c_str());
    ArduinoOTA.setPassword(WiFiSettings.password.c_str());
    ArduinoOTA.onStart(   []()              { display("OTA", TFT_BLUE); });
    ArduinoOTA.onEnd(     []()              { display("OTA done", TFT_GREEN); });
    ArduinoOTA.onError(   [](ota_error_t e) { display("OTA failed", TFT_RED); });
    ArduinoOTA.onProgress([](unsigned int p, unsigned int t) {
        String pct { (int) ((float) p / t * 100) };
        display(pct + "%");
    });
    ArduinoOTA.begin();
}

void connect_mqtt() {
    if (mqtt.connected()) return;  // already/still connected

    static int failures = 0;
    if (mqtt.connect(WiFiSettings.hostname.c_str())) {
        failures = 0;
    } else {
        failures++;
        if (failures >= max_failures) panic(T.error_mqtt);
    }
}

void retain(const String& topic, const String& message) {
    Serial.printf("%s %s\n", topic.c_str(), message.c_str());
    mqtt.publish(topic, message, true, 0);
}

void setup() {
    Serial.begin(115200);
    Serial.println("Operame start");

    digitalWrite(pin_backlight, HIGH);
    tft.init();
    tft.fillScreen(TFT_BLACK);
    tft.setRotation(1);
    sprite.createSprite(tft.width(), tft.height());

    OperameLanguage::select(T, LANGUAGE);

    if (!SPIFFS.begin(false)) {
        display(T.first_run, TFT_MAGENTA);
        if (!SPIFFS.format()) {
            display(T.error_format, TFT_RED);
            delay(20*1000);
        }
    }

    pinMode(pin_portalbutton,   INPUT_PULLUP);
    pinMode(pin_demobutton,     INPUT_PULLUP);
    pinMode(pin_pcb_ok,         INPUT_PULLUP);
    pinMode(pin_backlight,      OUTPUT);

    WiFiSettings.hostname = "operame-";
    WiFiSettings.language = LANGUAGE;
    WiFiSettings.begin();
    OperameLanguage::select(T, WiFiSettings.language);

    while (digitalRead(pin_pcb_ok)) {
        display(T.error_module, TFT_RED);
        delay(1000);
    }

    display_logo();
    delay(2000);

    hwserial1.begin(9600, SERIAL_8N1, pin_sensor_rx, pin_sensor_tx);

    sensor = new AQC(&hwserial1);
    sensor->begin();
    if (sensor->get_co2() >= 0) {
        hwserial1.setTimeout(100);
        Serial.println("Using AQC driver.");
    } else {
        delete sensor;
        sensor = new MHZ(&hwserial1);
        sensor->begin();
        Serial.println("Using MHZ driver.");
    }

    for (auto& str : T.portal_instructions[0]) {
        str.replace("{ssid}", WiFiSettings.hostname);
    }

    wifi_enabled  = WiFiSettings.checkbox("operame_wifi", false, T.config_wifi);
    ota_enabled   = WiFiSettings.checkbox("operame_ota", false, T.config_ota) && wifi_enabled;

    WiFiSettings.heading("CO2-niveaus");
    co2_warning   = WiFiSettings.integer("operame_co2_warning", 400, 5000, 700, T.config_co2_warning);
    co2_critical  = WiFiSettings.integer("operame_co2_critical",400, 5000, 800, T.config_co2_critical);
    co2_blink     = WiFiSettings.integer("operame_co2_blink",   800, 5000, 800, T.config_co2_blink);

    WiFiSettings.heading("MQTT");
    mqtt_enabled  = WiFiSettings.checkbox("operame_mqtt", false, T.config_mqtt) && wifi_enabled;
    String server = WiFiSettings.string("mqtt_server", 64, "", T.config_mqtt_server);
    int port      = WiFiSettings.integer("mqtt_port", 0, 65535, 1883, T.config_mqtt_port);
    max_failures  = WiFiSettings.integer("operame_max_failures", 0, 1000, 10, T.config_max_failures);
    mqtt_topic    = WiFiSettings.string("operame_mqtt_topic", WiFiSettings.hostname, T.config_mqtt_topic);
    mqtt_interval = 1000UL * WiFiSettings.integer("operame_mqtt_interval", 10, 3600, 60, T.config_mqtt_interval);
    mqtt_template = WiFiSettings.string("operame_mqtt_template", "{} PPM", T.config_mqtt_template);
    WiFiSettings.info(T.config_template_info);

    WiFiSettings.onConnect = [] {
        display(T.connecting, TFT_BLUE);
        check_portalbutton();
        return 50;
    };
    WiFiSettings.onFailure = [] {
        display(T.error_wifi, TFT_RED);
        delay(2000);
    };
    static int portal_phase = 0;
    static unsigned long portal_start;
    WiFiSettings.onPortal = [] {
        if (ota_enabled) setup_ota();
        portal_start = millis();
    };
    WiFiSettings.onPortalView = [] {
        if (portal_phase < 2) portal_phase = 2;
    };
    WiFiSettings.onConfigSaved = [] {
        portal_phase = 3;
    };
    WiFiSettings.onPortalWaitLoop = [] {
        if (WiFi.softAPgetStationNum() == 0) portal_phase = 0;
        else if (! portal_phase) portal_phase = 1;

        display(T.portal_instructions[portal_phase], TFT_WHITE, TFT_BLUE);

        if (portal_phase == 0 && millis() - portal_start > 10*60*1000) {
            panic(T.error_timeout);
        }

        if (ota_enabled) ArduinoOTA.handle();
        if (button(pin_portalbutton)) ESP.restart();
    };

    if (wifi_enabled) WiFiSettings.connect(false, 15);

    static WiFiClient wificlient;
    if (mqtt_enabled) mqtt.begin(server.c_str(), port, wificlient);

    if (ota_enabled) setup_ota();
}

#define every(t) for (static unsigned long _lasttime; (unsigned long)((unsigned long)millis() - _lasttime) >= (t); _lasttime = millis())

void loop() {
    static int co2;

    every(5000) {
        co2 = sensor->get_co2();
        Serial.println(co2);
    }

    every(50) {
        if (co2 < 0) {
            display(T.error_sensor, TFT_RED);
        } else if (co2 == 0) {
            display(T.wait);
        } else {
            // some MH-Z19's go to 10000 but the display has space for 4 digits
            display_ppm(co2 > 9999 ? 9999 : co2);
        }
    }

    if (mqtt_enabled) {
        mqtt.loop();
        every(mqtt_interval) {
            if (co2 <= 0) break;
            connect_mqtt();
            String message = mqtt_template;
            message.replace("{}", String(co2));
            retain(mqtt_topic, message);
        }
    }

    if (ota_enabled) ArduinoOTA.handle();

    check_portalbutton();
    check_demobutton();
}