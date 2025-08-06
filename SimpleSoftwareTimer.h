/**
 * @file SimpleSoftwareTimer.h
 * @brief Enkel software timer-klasse til brug i Arduino-loop.
 *
 * Eksempel på brug:
 *   SimpleSoftwareTimer myTimer;
 *   myTimer.setInterval(1000, myCallback);
 *   void loop() { myTimer.run(); }
 */
#pragma once
#include <Arduino.h>

class SimpleSoftwareTimer {
public:
    SimpleSoftwareTimer() : prevMillis(0), interval(0), active(false), cb(nullptr) {}
    /**
     * @brief Sæt interval og callback.
     * @param interval_ms Interval i millisekunder
     * @param callback Pointer til callback-funktion
     */
    void setInterval(uint32_t interval_ms, void (*callback)()) {
        interval = interval_ms;
        cb = callback;
        prevMillis = millis();
        active = true;
    }
     /**
     * @brief Stop timeren.
     */
    void stop() {
        active = false;
    }
    /**
     * @brief Skal kaldes ofte fra loop(). Udløser callback, hvis interval er gået.
     */
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
