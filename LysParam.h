/**
 * @file LysParam.h
 * @brief Konfigurationsparametre og log-event enum for lysautomatik.
 *
 * LysParam indeholder alle justerbare parametre for lys-automatik, segmenter,
 * dimmer og astro-mode. Deles mellem core0 og core1 via param_mutex.
 *
 * lyslogstate enum bruges til FIFO-kommunikation (core1 → core0) for logning.
 */
#pragma once
#include <Arduino.h>
#include <cstdint>

/** FIFO event koder – sendes fra core1 til core0 for logning på SD-kort. */
enum lyslogstate {
    nataktivfalse,      // Nat → dag overgang
    nataktivtrue,       // Dag → nat overgang
    pir1_detection,     // PIR1 aktiveret
    pir2_detection,     // PIR2 aktiveret
    hwsw_on,            // Hardware switch ON
    swsw_on,            // Software switch ON (fra web)
    hwsw_off,           // Hardware switch OFF
    swsw_off,           // Software switch OFF (fra web)
    wdt_reset,          // Watchdog forårsagede reboot
    i2c_reset_wire,     // VEML7700 I2C bus reset (Wire/I2C0)
    i2c_reset_wire1,    // BMP280 I2C bus reset (Wire1/I2C1)
    astro_log_request   // Request til core0 om at logge astro-data for i dag
};

/** Alle konfigurationsparametre for lysautomatik. */
struct LysParam {
    // Styringsmode: "Tid" | "Klokken" | "Astro"
    String styringsvalg = "Tid";

    // Lux-tærskel for nat/dag-skift
    float luxstartvaerdi = 8.0f;

    // Timer-mode varigheder (sekunder) og lysniveauer (%)
    long timerA = 75;       // Grundlys varighed (sek, Tid-mode)
    int  pwmA   = 75;       // Grundlys niveau (%)

    long timerC = 30;       // PIR 1. fase varighed (sek)
    int  pwmC   = 100;      // PIR 1. fase niveau (%)

    long timerE = 30;       // PIR 2. fase varighed (sek)
    int  pwmE   = 55;       // PIR 2. fase niveau (%)

    int  pwmG   = 0;        // Natglød niveau (%)

    // Forsinkelse for nat/dag-skift (sekunder, symmetrisk begge veje)
    long natdagdelay = 15;

    // Segment 1 (basis) – slut-tidspunkt (Klokken/Astro mode)
    int slutKlokkeTimer    = 22;
    int slutKlokkeMinutter = 0;

    // Segment 2 (tillæg) – start/slut + ugedage
    bool    seg2Enabled       = false;
    int     seg2StartTimer    = 5;
    int     seg2StartMinutter = 30;
    int     seg2SlutTimer     = 7;
    int     seg2SlutMinutter  = 30;
    uint8_t seg2WeekMask      = 0x7F;  // bit0=Søn .. bit6=Lør (0x7F = alle dage)

    // Segment 3 (tillæg) – start/slut + ugedage
    bool    seg3Enabled       = false;
    int     seg3StartTimer    = 0;
    int     seg3StartMinutter = 0;
    int     seg3SlutTimer     = 0;
    int     seg3SlutMinutter  = 0;
    uint8_t seg3WeekMask      = 0x7F;

    // Log-styring (kan slås til/fra via web)
    bool lognataktiv     = true;
    bool logpirdetection = true;

    // Softlys step-størrelse (1–10, bruges af Dimmerfunktion)
    int aktuelStepfrekvens = 5;

    // Astro-mode parametre
    bool  astroEnabled          = false;    // Master enable for astro-beregning
    float astroLat              = 56.150f;  // Latitude i grader (N positiv)
    float astroLon              = 10.200f;  // Longitude i grader (E positiv)
    int   astroSunsetOffsetMin  = 0;        // Offset til solnedgang (minutter, kan være negativ)
    int   astroSunriseOffsetMin = 0;        // Offset til solopgang (minutter, kan være negativ)
    bool  astroLuxEarlyStart    = true;     // Lux kan aktivere nat før beregnet solnedgang
};
