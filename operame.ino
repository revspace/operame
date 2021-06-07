#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <MQTT.h>
#include <SPIFFS.h>
#include <WiFiSettings.h>
#include <MHZ19.h>
#include <ArduinoOTA.h>
#include <ArduinoJSON.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <logo.h>
#include <list>
#include <operame_strings.h>
#include "Stream.h"

#define LANGUAGE "nl"
OperameLanguage::Texts T;

enum Driver { AQC, MHZ };
Driver            driver;
MQTTClient        mqtt;
HardwareSerial    hwserial1(1);
TFT_eSPI          display;
TFT_eSprite       sprite(&display);
MHZ19             mhz;
WiFiClientSecure  wificlient;

const int       pin_portalbutton = 35;
const int       pin_demobutton   = 0;
const int       pin_backlight    = 4;
const int       pin_sensor_rx    = 27;
const int       pin_sensor_tx    = 26;
const int       pin_pcb_ok       = 12;   // pulled to GND by PCB trace
int             mhz_co2_init     = 410;  // magic value reported during init

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

// REST configuration via WiFiSettings
unsigned long   rest_interval;
int             rest_port;
String          rest_domain;
String          rest_uri;
String          rest_resource_id;
String          rest_cert;
bool            rest_enabled;

void retain(const String& topic, const String& message) {
    Serial.printf("%s %s\n", topic.c_str(), message.c_str());
    mqtt.publish(topic, message, true, 0);
}

void clear_sprite(int bg = TFT_BLACK) {
    sprite.fillSprite(bg);
    if (WiFi.status() == WL_CONNECTED) {
        sprite.drawRect(0, 0, display.width(), display.height(), TFT_BLUE);
    }
}

void display_big(const String& text, int fg = TFT_WHITE, int bg = TFT_BLACK) {
    clear_sprite(bg);
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
    sprite.drawString(text, display.width()/2, display.height()/2);

    sprite.pushSprite(0, 0);
}

void display_lines(const std::list<String>& lines, int fg = TFT_WHITE, int bg = TFT_BLACK) {
    clear_sprite(bg);
    sprite.setTextSize(1);
    sprite.setTextFont(4);
    sprite.setTextDatum(MC_DATUM);
    sprite.setTextColor(fg, bg);

    const int line_height = 32;
    int y = display.height()/2 - (lines.size()-1) * line_height/2;
    for (auto line : lines) {
        sprite.drawString(line, display.width()/2, y);
        y += line_height;
    }
    sprite.pushSprite(0, 0);
}

void display_logo() {
    clear_sprite();
    sprite.setSwapBytes(true);
    sprite.pushImage(0, 0, 240, 135, CONTROL_CO2_V2_240_135_LOGO);
    sprite.pushSprite(0, 0);
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

void calibrate() {
    auto lines = T.calibration;
    for (int count = 60; count >= 0; count--) {
        lines.back() = String(count);
        display_lines(lines, TFT_RED);
        unsigned long start = millis();
        while (millis() - start < 1000) {
            if (button(pin_demobutton) || button(pin_portalbutton)) return;
        }
    }

    lines = T.calibrating;
    if (driver == AQC) for (auto& line : lines) line.replace("400", "425");
    display_lines(lines, TFT_MAGENTA);

    set_zero();    // actually instantaneous
    delay(15000);  // give time to read long message
}

void ppm_demo() {
    display_big("demo!");
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

void panic(const String& message) {
    display_big(message, TFT_RED);
    delay(5000);
    ESP.restart();
}

bool button(int pin) {
    if (digitalRead(pin)) return false;
    unsigned long start = millis();
    while (!digitalRead(pin)) {
        if (millis() - start >= 50) display_big("");
    }
    return millis() - start >= 50;
}

void check_portalbutton() {
    if (button(pin_portalbutton)) WiFiSettings.portal();
}

void check_demobutton() {
    if (button(pin_demobutton)) ppm_demo();
}

void check_buttons() {
    check_portalbutton();
    check_demobutton();
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

void flush(Stream& s, int limit = 20) {
    // .available() sometimes stays true (why?), hence the limit

    s.flush();  // flush output
    while(s.available() && --limit) s.read();  // flush input
}

int aqc_get_co2() {
    static bool initialized = false;

    const uint8_t command[9] = { 0xff, 0x01, 0xc5, 0, 0, 0, 0, 0, 0x3a };
    uint8_t response[9];
    int co2 = -1;

    for (int attempt = 0; attempt < 3; attempt++) {
        flush(hwserial1);
        hwserial1.write(command, sizeof(command));
        delay(50);

        size_t c = hwserial1.readBytes(response, sizeof(response));
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

void aqc_set_zero() {
    const uint8_t command[9] = { 0xff, 0x01, 0x87, 0, 0, 0, 0, 0, 0x78 };
    flush(hwserial1);
    hwserial1.write(command, sizeof(command));
}

void mhz_setup() {
    mhz.begin(hwserial1);
    // mhz.setFilter(true, true);  Library filter doesn't handle 0436
    mhz.autoCalibration(true);
    char v[5] = {};
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

void mhz_set_zero() {
    mhz.calibrate();
}

int get_co2() {
    // <0 means read error, 0 means still initializing, >0 is PPM value

    if (driver == AQC) return aqc_get_co2();
    if (driver == MHZ) return mhz_get_co2();

    // Should be unreachable
    panic(T.error_driver);
    return -1;  // suppress warning
}

void set_zero() {
    if (driver == AQC) { aqc_set_zero(); return; }
    if (driver == MHZ) { mhz_set_zero(); return; }

    // Should be unreachable
    panic(T.error_driver);
}

void setup() {
    Serial.begin(115200);
    Serial.println("Operame start");

    digitalWrite(pin_backlight, HIGH);
    display.init();
    display.fillScreen(TFT_BLACK);
    display.setRotation(1);
    sprite.createSprite(display.width(), display.height());

    OperameLanguage::select(T, LANGUAGE);

    if (!SPIFFS.begin(false)) {
        display_lines(T.first_run, TFT_MAGENTA);
        if (!SPIFFS.format()) {
            display_big(T.error_format, TFT_RED);
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
        display_big(T.error_module, TFT_RED);
        delay(1000);
    }

    display_logo();
    delay(2000);

    hwserial1.begin(9600, SERIAL_8N1, pin_sensor_rx, pin_sensor_tx);

    if (aqc_get_co2() >= 0) {
        driver = AQC;
        hwserial1.setTimeout(100);
        Serial.println("Using AQC driver.");
    } else {
        driver = MHZ;
        mhz_setup();
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
    mqtt_topic  = WiFiSettings.string("operame_mqtt_topic", WiFiSettings.hostname, T.config_mqtt_topic);
    mqtt_interval = 1000UL * WiFiSettings.integer("operame_mqtt_interval", 10, 3600, 60, T.config_mqtt_interval);
    mqtt_template = WiFiSettings.string("operame_mqtt_template", "{} PPM", T.config_mqtt_template);
    WiFiSettings.info(T.config_template_info);

    WiFiSettings.heading("REST");
    rest_enabled            = WiFiSettings.checkbox("operame_rest", false, T.config_rest) && wifi_enabled;
    rest_domain             = WiFiSettings.string("rest_domain", 150, "", T.config_rest_domain);
    rest_uri                = WiFiSettings.string("rest_uri", 600, "", T.config_rest_uri);
    rest_port               = WiFiSettings.integer("rest_port", 0, 65535, 443, T.config_rest_port);
    rest_interval           = 1000UL * WiFiSettings.integer("operame_rest_interval", 10, 3600, 60 * 5, T.config_rest_interval);
    rest_resource_id        = WiFiSettings.string("rest_resource_id", 64, "", T.config_rest_resource_id);
    bool rest_cert_enabled  = WiFiSettings.checkbox("operame_rest_cert", false, T.config_rest_cert_enabled);
    rest_cert        = WiFiSettings.string("rest_cert", 2000, "", T.config_rest_cert);
    rest_cert.replace("\\n", "\n");

    WiFiSettings.onConnect = [] {
        display_big(T.connecting, TFT_BLUE);
        check_portalbutton();
        return 50;
    };
    WiFiSettings.onFailure = [] {
        display_big(T.error_wifi, TFT_RED);
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

        display_lines(T.portal_instructions[portal_phase], TFT_WHITE, TFT_BLUE);

        if (portal_phase == 0 && millis() - portal_start > 10*60*1000) {
            panic(T.error_timeout);
        }

        if (ota_enabled) ArduinoOTA.handle();
        if (button(pin_portalbutton)) ESP.restart();
    };

    if (wifi_enabled) WiFiSettings.connect(false, 15);

    if (mqtt_enabled) mqtt.begin(server.c_str(), port, wificlient);

    if (rest_cert_enabled) wificlient.setCACert(rest_cert.c_str());

    if (ota_enabled) setup_ota();
}

void post_rest_message(DynamicJsonDocument message, Stream& stream) {
    stream.println("POST " + rest_uri + " HTTP/1.1");
    stream.println("Host: " + rest_domain);
    stream.println("Content-Type: application/json");
    stream.println("Connection: keep-alive");
    stream.print("Content-Length: ");
    stream.println(measureJson(message));
    stream.println();
    serializeJson(message, stream);
    stream.println();
}

#define every(t) for (static unsigned long _lasttime; (unsigned long)((unsigned long)millis() - _lasttime) >= (t); _lasttime = millis())

void loop() {
    static int co2;

    every(5000) {
        co2 = get_co2();
        Serial.print(co2);
        Serial.println();
    }

    every(50) {
        if (co2 < 0) {
            display_big(T.error_sensor, TFT_RED);
        } else if (co2 == 0) {
            display_big(T.wait);
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

    if (rest_enabled) {
        while(wificlient.available()){
            String line = wificlient.readStringUntil('\r');
            Serial.print(line);
        }

        every(rest_interval) {
            if (co2 <= 0) break;

            const size_t capacity = JSON_OBJECT_SIZE(2);
            DynamicJsonDocument message(capacity);
            message["co2"] = co2;
            message["id"] = rest_resource_id.c_str();

            if (wificlient.connected() || wificlient.connect(&rest_domain[0], rest_port)) {
                post_rest_message(message, wificlient);
            }
        }
    }

    if (ota_enabled) ArduinoOTA.handle();
    check_buttons();
}
