/**
 * @file LysAutomatik.h
 * @brief Automatik og state machine for nat/dag, timer, PIR og hardwareoutput.
 * 
 * Styrer systemets hovedlogik for nat/dag, PIR-events, PWM og relæ.
 * Bruger parametre fra LysParam (mutexbeskyttet!).
 */

#pragma once
#include "LysParam.h"
#include "Dimmerfunktion.h"
/**
 * @class LysAutomatik
 * @brief Hoved-automatik og tilstandsmaskine. Kaldes fra core1.
 */
class LysAutomatik {
private:
    enum LysState { OFF, TIMER_A, TIMER_C, TIMER_E, NIGHT_GLOW };
    LysState currentState = OFF ;
    
    LysParam& param;          ///< Konfigurationsparametre (mutexbeskyttet fra main)
    dimmerfunktion *dimmer;   ///< Pointer til dimmerobjekt
    bool slukActiveret = false;
    // Timers
    long timerA = 0, timerC = 0, timerE = 0;
    bool nataktiv = false;
    long natdagdelayTimer = 0;
    bool klokmode = false;
    bool lastLuxOver = true;
    bool lastNataktiv = false; // NYT

public:
    LysAutomatik(LysParam& p, dimmerfunktion* d)
      : param(p), dimmer(d) {}

void update(float lux, bool pirEvent, time_t ntpTid) {
    // --- Nat/dag skift og delay ---
    if (!nataktiv && lux < param.luxstartvaerdi && lastLuxOver) {
        natdagdelayTimer = param.natdagdelay;
        lastLuxOver = false;
    }
    if (lux >= param.luxstartvaerdi) {
        lastLuxOver = true;
        if(param.lognataktiv && nataktiv){ 
            rp2040.fifo.push_nb(nataktivfalse);
        }
        if(nataktiv) nataktiv = false;
    }

    // Tæl delay ned, aktiver nat når delay er slut
    if (natdagdelayTimer > 0) {
        natdagdelayTimer--;
        if (natdagdelayTimer == 0){ 
            nataktiv = true;
            if(param.lognataktiv) rp2040.fifo.push_nb(nataktivtrue);
        }
    }

    // --- Hovedautomatik ---
    if (nataktiv) {
        // Hvis vi lige er gået i nataktiv (OFF->TIMER_A), eller nataktiv er sand og state OFF
        if (currentState == OFF) {
            startA(ntpTid);
        }
        if (pirEvent) {
            startC();
        }
    } else {
        // Kun sluk hvis vi ikke allerede er OFF!
        if (currentState != OFF) {
            dimmer->sluk();
            currentState = OFF;
        }
    }

    // --- State machine ---
    switch (currentState) {
        case TIMER_A:
            if (--timerA <= 0) {
                currentState = NIGHT_GLOW;
                dimmer->setlysiprocentSoft(param.pwmG);
            }
            break;
        case TIMER_C:
            if(timerA > 0) --timerA;
            if (--timerC <= 0) {
                startE();
            }
            break;
        case TIMER_E:
            if(timerA > 0) --timerA;
            if (--timerE <= 0) {
                if (timerA > 0) {
                    resumeA();
                } else {
                    currentState = NIGHT_GLOW;
                    dimmer->setlysiprocentSoft(param.pwmG);
                }
            }
            break;
        case NIGHT_GLOW:
            // nothing, stays at pwmG
            break;
        case OFF:
        default:
            break;
    }

    // -- Debug print EFTER update, så du ser de aktuelle værdier --
    
    /*Serial.print("Lux: "); Serial.println(lux);
    Serial.print("Aktuel lys procent: "); Serial.println(dimmer->returneraktuelvaerdi());
    Serial.print("Nataktiv: "); Serial.println(nataktiv);
    Serial.print("State: "); Serial.println(currentState);
    Serial.print("timerA: "); Serial.println(timerA);
    Serial.print("timerc: "); Serial.println(timerC);
    Serial.print("timere: "); Serial.println(timerE);
    */
}

    void startA(time_t ntpTid) {
        currentState = TIMER_A;
        if (param.styringsvalg == "Klokken") {
            tm *timeinfo = localtime(&ntpTid);
            int nowsec = timeinfo->tm_hour * 3600 + timeinfo->tm_min * 60 + timeinfo->tm_sec;
            int endsec = param.slutKlokkeTimer * 3600 + param.slutKlokkeMinutter * 60;
            timerA = endsec - nowsec;
            if (timerA < 0) timerA = 0;
        } else {
            timerA = param.timerA;
        }
        dimmer->setlysiprocentSoft(param.pwmA);
    }

    void startC() {
        currentState = TIMER_C;
        timerC = param.timerC;
        dimmer->setlysiprocentSoft(param.pwmC);
    }

    void startE() {
        currentState = TIMER_E;
        timerE = param.timerE;
        dimmer->setlysiprocentSoft(param.pwmE);
    }

    void resumeA() {
        currentState = TIMER_A;
        dimmer->setlysiprocentSoft(param.pwmA);
    }

    // Hardware override
    void forceOn()  {  dimmer->taend(); }
    void forceOff() { dimmer->sluk(); slukActiveret = true; }
    bool getNataktiv() const { return nataktiv; }
};
