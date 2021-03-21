#include <Arduino.h>
#include <map>
#include <vector>
#include <list>

namespace OperameLanguage {

struct Texts {
    const char
        *error_mqtt,
        *error_driver,
        *error_module,
        *error_timeout,
        *error_sensor,
        *error_wifi,
        *error_format,
        *config_wifi,
        *config_ota,
        *config_co2_warning,
        *config_co2_critical,
        *config_co2_blink,
        *config_mqtt,
        *config_mqtt_server,
        *config_mqtt_port,
        *config_max_failures,
        *config_mqtt_topic,
        *config_mqtt_interval,
        *config_mqtt_template,
        *config_template_info,
        *connecting,
        *wait
    ;
    std::vector<std::list<String>> portal_instructions;
    std::list<String> first_run;
    std::list<String> calibration;
    std::list<String> calibrating;
};

std::map<const String, const String> languages {
    // Ordered alphabetically
    { "en", "English" },
    { "nl", "Nederlands" },
};

bool available(const String& language) {
    return languages.count(language) == 1;
}

bool select(Texts& T, String language) {
    if (! available(language)) {
        if (available("en")) language = "en";
        else language = languages.begin()->first;
    }

    if (language == "en") {
        T.error_mqtt = "MQTT unreachable";
        T.error_driver = "driver error";
        T.error_module = "module turned around!";
        T.error_timeout = "Time's up";
        T.error_sensor = "sensor error";
        T.error_wifi = "WiFi failed!";
        T.error_format = "Formatting failed";
        T.wait = "wait...";
        T.config_wifi = "Use WiFi connection";
        T.config_ota = "Enable wireless reprogramming. (Uses portal password!)";
        T.config_co2_warning = "Yellow from [ppm]";
        T.config_co2_critical = "Red from [ppm]";
        T.config_co2_blink = "Blink from [ppm]";
        T.config_mqtt = "Publish measurements via the MQTT protocol";
        T.config_mqtt_server = "Broker";  // probably should not be translated
        T.config_mqtt_port = "Broker TCP port";
        T.config_max_failures = "Number of failed connections before automatic restart";
        T.config_mqtt_topic = "Topic";  // probably should not be translated
        T.config_mqtt_interval = "Publication interval [s]";
        T.config_mqtt_template = "Message template";
        T.config_template_info = "The {} in the template is replaced by the measurement value.";
        T.connecting = "Connecting to WiFi...";
        T.portal_instructions = {
            {
                "For configuration,",
                "connect to WiFi",
                "\"{ssid}\"",
                "with a smartphone."
            },
            {
                "Follow instructions",
                "on your smartphone.",
                "(log in notification)"
            },
            {
                "Change settings",
                "and click \"Save\".",
                "(bottom right)"
            },
            {
                "Change settings",
                "and click \"Save\".",
                "Or \"Restart device\"",
                "when you're done."
            }
        };
        T.first_run = {
            "DO NOT TURN OFF",
            "Initializing",
            "flash memory.",
        };
        T.calibration = {
            "Manual calibration!",
            "Press button",
            "to cancel.",
            ""
        };
        T.calibrating = {
            "Assuming current",
            "CO2 level to be",
            "400 PPM."
        };
        return true;
    }

    if(language == "nl") {
        T.error_mqtt = "MQTT onbereikbaar";
        T.error_driver = "driverfout";
        T.error_module = "module verkeerd om!";
        T.error_timeout = "Tijd verstreken";
        T.error_sensor = "sensorfout";
        T.error_wifi = "WiFi mislukt!";
        T.error_format = "Formatteren mislukt";
        T.wait = "wacht...";
        T.config_wifi = "WiFi-verbinding gebruiken";
        T.config_ota = "Draadloos herprogrammeren inschakelen. (Gebruikt portaalwachtwoord!)";
        T.config_co2_warning = "Geel vanaf [ppm]";
        T.config_co2_critical = "Rood vanaf [ppm]";
        T.config_co2_blink = "Knipperen vanaf [ppm]";
        T.config_mqtt = "Metingen via het MQTT-protocol versturen";
        T.config_mqtt_server = "Broker";  // zo heet dat in MQTT
        T.config_mqtt_port = "Broker TCP-poort";
        T.config_max_failures = "Aantal verbindingsfouten voor automatische herstart";
        T.config_mqtt_topic = "Topic";  // zo heet dat in MQTT
        T.config_mqtt_interval = "Publicatie-interval [s]";
        T.config_mqtt_template = "Berichtsjabloon";
        T.config_template_info = "De {} in het sjabloon wordt vervangen door de gemeten waarde.";
        T.connecting = "Verbinden met WiFi...";
        T.portal_instructions = {
            {
                "Voor configuratie,",
                "verbind met WiFi",
                "\"{ssid}\"",
                "met een smartphone."
            },
            {
                "Volg instructies op",
                "uw smartphone.",
                "(inlog-notificatie)"
            },
            {
                "Wijzig instellingen",
                "en klik op \"Opslaan\".",
                "(rechtsonder)"
            },
            {
                "Wijzig instellingen",
                "en klik op \"Opslaan\".",
                "Of \"Herstarten\"",
                "als u klaar bent."
            }
        };
        T.first_run = {
            "NIET",
            "UITSCHAKELEN",
            "Flashgeheugen",
            "wordt voorbereid."
        };
        T.calibration = {
            "Handmatige",
            "calibratie!",
            "knop = stop",
            ""
        };
        T.calibrating = {
            "Het huidige CO2-",
            "niveau wordt",
            "aangenomen",
            "400 PPM te zijn."
        };
        return true;
    }

    return false;
}

} // namespace
