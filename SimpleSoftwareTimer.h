#pragma once
/**
 * @file SimpleSoftwareTimer.h
 * @brief Enkel software timer til brug i Arduino loop().
 *
 * Kalder en callback med fast interval. Kræver at run() kaldes ofte fra loop().
 */

#include <Arduino.h>

class SimpleSoftwareTimer {
public:
    SimpleSoftwareTimer() : prevMillis(0), interval(0), active(false), cb(nullptr) {}

    /**
     * @brief Start timer med interval og callback.
     * @param interval_ms Interval i millisekunder.
     * @param callback Funktion der kaldes ved timeout.
     */
    void setInterval(uint32_t interval_ms, void (*callback)()) {
        interval = interval_ms;
        cb = callback;
        prevMillis = millis();
        active = true;
    }

    /** Stop timeren. */
    void stop() {
        active = false;
    }

    /** Kald fra loop() – udløser callback hvis interval er overskredet. */
    void run() {
        if (active && cb) {
            uint32_t now = millis();
            if (now - prevMillis >= interval) {
                prevMillis += interval;
                cb();
            }
        }
    }

private:
    uint32_t prevMillis;
    uint32_t interval;
    bool active;
    void (*cb)();
};
