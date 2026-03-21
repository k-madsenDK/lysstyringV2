/**
 * @file LysParam.h
 * @brief Struktur for alle konfigurationsparametre til lysautomatik.
 */
#pragma once
#include <Arduino.h>
#include <stdint.h>

enum lyslogstate {
    nataktivfalse,
    nataktivtrue,
    pir1_detection,
    pir2_detection,
    hwsw_on,
    swsw_on,
    hwsw_off,
    swsw_off,
    wdt_reset,
    i2c_reset_wire,
    i2c_reset_wire1,
    astro_log_request
};

struct LysParam {
    // "Tid" | "Klokken" | "Astro"
    String styringsvalg = "Tid";

    float luxstartvaerdi = 8.0f;

    long timerA = 75; // sek (Tid-mode)
    int pwmA = 75;    // %

    long timerC = 30;
    int pwmC = 100;

    long timerE = 30;
    int pwmE = 55;

    int pwmG = 0;
    long natdagdelay = 15;

    // Segment 1 (basis)
    int slutKlokkeTimer = 22;
    int slutKlokkeMinutter = 0;

    // Segment 2 (tillæg)
    bool seg2Enabled = false;
    int  seg2StartTimer = 5;
    int  seg2StartMinutter = 30;
    int  seg2SlutTimer = 7;
    int  seg2SlutMinutter = 30;
    uint8_t seg2WeekMask = 0x7F; // bit0=Sun .. bit6=Sat

    // Segment 3 (tillæg)
    bool seg3Enabled = false;
    int  seg3StartTimer = 0;
    int  seg3StartMinutter = 0;
    int  seg3SlutTimer = 0;
    int  seg3SlutMinutter = 0;
    uint8_t seg3WeekMask = 0x7F;

    bool lognataktiv = true;
    bool logpirdetection = true;
    int aktuelStepfrekvens = 5;

    // ---- Astro ----
    bool  astroEnabled = false;      // master enable
    float astroLat = 56.150f;        // grader (N positiv)
    float astroLon = 10.200f;        // grader (E positiv)
    int   astroSunsetOffsetMin  = 0; // min (kan være negativ)
    int   astroSunriseOffsetMin = 0; // min (kan være negativ)

    // Hvis true: lux kan aktivere nat tidligere end solnedgang (kun "early start" – ikke "late keep-on")
    bool astroLuxEarlyStart = true;
};
