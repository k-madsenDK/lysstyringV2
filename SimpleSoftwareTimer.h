#pragma once

#include <Arduino.h>

class SimpleSoftwareTimer {
public:
    SimpleSoftwareTimer() : prevMillis(0), interval(0), active(false), cb(nullptr) {}
/*

    SimpleSoftwareTimer myTimer; erstatter din gamle hardware timer.
    Sæt interval og callback med myTimer.setInterval(1000, myCallback);
    I dit loop skal du kalde myTimer.run(); ofte (helst hver gang).
    Callbacken (myCallback) bliver kaldt ca. hvert sekund.

*/
    void setInterval(uint32_t interval_ms, void (*callback)()) {
        interval = interval_ms;
        cb = callback;
        prevMillis = millis();
        active = true;
    }

    void stop() {
        active = false;
    }

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

// --------------------------------------------------------------------
// Eksempel på brug af SimpleSoftwareTimer i stedet for hardware timer:
// --------------------------------------------------------------------

// // Definér timeren som global variabel
// SimpleSoftwareTimer myTimer;

// // Din callback-funktion
// void myCallback() {
//     Serial.println("Timer callback kaldt!");
// }

// void setup() {
//     Serial.begin(115200);
//     // Start timeren med 1000 ms interval og callback
//     myTimer.setInterval(1000, myCallback);
// }

// void loop() {
//     // Kald timerens run() ofte i loopet
//     myTimer.run();
//
//     // Resten af din kode
// }