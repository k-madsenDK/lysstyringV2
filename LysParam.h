/**
 * @file LysParam.h
 * @brief Struktur for alle konfigurationsparametre til lysautomatik.
 *
 * Kan let udvides, bruges i web, hardware og JSON.
 */
#pragma once
#include <Arduino.h>
/**
 * @enum lyslogstate
 * @brief Mulige log-events (bruges til FIFO mellem cores).
 */
enum lyslogstate {nataktivfalse,nataktivtrue,pir1_detection,pir2_detection,hwsw_on,swsw_on};
/**
 * @struct LysParam
 * @brief Samler alle brugerparametre til automatik, log og hardware.
 */
struct LysParam {
    String styringsvalg = "Tid"; // "Tid" eller "Klokken"
    float luxstartvaerdi = 8.0;
    long timerA = 75; // sekunder
    int pwmA = 75; // i %
    long timerC = 30;
    int pwmC = 100;
    long timerE = 30;
    int pwmE = 55;
    int pwmG = 0;
    long natdagdelay = 15;
    int slutKlokkeTimer = 22;
    int slutKlokkeMinutter = 0;
    bool lognataktiv = true;
    bool logpirdetection = true;
    int aktuelStepfrekvens = 5;   // <-- NYT
};
