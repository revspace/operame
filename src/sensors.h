#include <Arduino.h>
#include <MHZ19.h>

struct CO2Sensor {
    int co2_zero;
    virtual ~CO2Sensor() = default;
    virtual void begin() = 0;
    virtual void set_zero() = 0;
    virtual int get_co2() = 0;
    // <0 means read error, 0 means still initializing, >0 is PPM value
};

struct AQC : CO2Sensor {
    int co2_zero = 425;
    
    Stream *serial;
    AQC(Stream *x) : serial(x) {}

    void flush(int limit = 20) {
        // .available() sometimes stays true (why?), hence the limit

        serial->flush();  // flush output
        while(serial->available() && --limit) serial->read();  // flush input
    }

    void begin() { }

    int get_co2() {
        static bool initialized = false;

        const uint8_t command[9] = { 0xff, 0x01, 0xc5, 0, 0, 0, 0, 0, 0x3a };
        uint8_t response[9];
        int co2 = -1;

        for (int attempt = 0; attempt < 3; attempt++) {
            flush();
            serial->write(command, sizeof(command));
            delay(50);

            size_t c = serial->readBytes(response, sizeof(response));
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

    void set_zero() {
        const uint8_t command[9] = { 0xff, 0x01, 0x87, 0, 0, 0, 0, 0, 0x78 };
        flush();
        serial->write(command, sizeof(command));
    }
};

struct MHZ : CO2Sensor {
    MHZ19 mhz;
    int co2_zero = 400;
    int co2_init = 410;
    
    Stream *serial;
    MHZ(Stream *x) : serial(x) {}

    void begin() {
        mhz.begin(*serial);
        // mhz.setFilter(true, true);  Library filter doesn't handle 0436
        mhz.autoCalibration(true);
        char v[5] = {};
        mhz.getVersion(v);
        v[4] = '\0';
        if (strcmp("0436", v) == 0) co2_init = 436;
    }

    int get_co2() {
        int co2       = mhz.getCO2();
        int unclamped = mhz.getCO2(false);

        if (mhz.errorCode != RESULT_OK) {
            delay(500);
            setup();
            return -1;
        }

        // reimplement filter from library, but also checking for 436 because our
        // sensors (firmware 0436, coincidence?) return that instead of 410...
        if (unclamped == co2_init && co2 - unclamped >= 10) return 0;

        // No known sensors support >10k PPM (library filter tests for >32767)
        if (co2 > 10000 || unclamped > 10000) return 0;

        return co2;
    }

    void set_zero() {
        mhz.calibrate();
    }
};