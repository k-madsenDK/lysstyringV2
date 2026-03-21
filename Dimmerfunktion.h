#pragma once
/**
 * @file Dimmerfunktion.h
 * @brief AC-dimmer med softstart/softsluk via PWM + relæ.
 *
 * PWM 10 kHz, 16-bit range. Relæ til/frakobling af last.
 * Softstart og softsluk med konfigurerbart step (fra LysParam::aktuelStepfrekvens).
 * Step-funktion kaldes 4 Hz fra softlysIrq() i core1.
 */

#include <Arduino.h>
#include "LysParam.h"

class dimmerfunktion {
private:
    int pwmstartvaerdi;     // Nedre PWM-grænse (dimmer tærskel)
    int pwmmaxvaerdi;       // Øvre PWM-grænse (fuld lysstyrke)
    int enprocent = 0;      // PWM-enheder per procent
    int relayben;           // GPIO til relæ
    int pwmben;             // GPIO til AC-dimmer PWM
    int aktuelpwmvaerdi = 0;
    int aktuelprocentvaerdi = 0;

    bool softstart_aktiv = false;
    bool softsluk_aktiv = false;
    int  soft_slut = 100;
    int  soft_step = 5;
    int  soft_nuvaerende = 0;
    int  aktuelsetvaerdi = 0;

    LysParam* lysparam_ptr = nullptr;

    void dimmerinit() {
        analogWriteRange(65535);
        analogWriteFreq(10000);
        analogWrite(pwmben, aktuelpwmvaerdi);
        pinMode(relayben, OUTPUT);
        digitalWrite(relayben, 0);
        enprocent = (pwmmaxvaerdi - pwmstartvaerdi) / 100;
    }

    void relayOn()  { digitalWrite(relayben, 1); }
    void relayOff() { digitalWrite(relayben, 0); }

    /** Sæt lysprocent direkte (intern – bruges af softstart/sluk). */
    bool setlysiprocent(int nyvaerdi) {
        if (nyvaerdi < 0 || nyvaerdi > 100) return false;
        aktuelprocentvaerdi = nyvaerdi;
        if (nyvaerdi == 0) {
            aktuelsetvaerdi = 0;
            aktuelpwmvaerdi = 0;
            analogWrite(pwmben, 0);
            relayOff();
            return true;
        }
        aktuelpwmvaerdi = (enprocent * nyvaerdi) + pwmstartvaerdi;
        analogWrite(pwmben, aktuelpwmvaerdi);
        relayOn();
        return true;
    }

    /** Hent step-størrelse fra LysParam (dynamisk). */
    int hentStep() const {
        if (lysparam_ptr && lysparam_ptr->aktuelStepfrekvens > 0)
            return lysparam_ptr->aktuelStepfrekvens;
        return 5;
    }

public:
    dimmerfunktion(int relayben = 2,
                   int pwmben = 0,
                   int pwmlow = 0,
                   int pwmhigh = 65535,
                   LysParam* lysparam = nullptr)
        : relayben(relayben), pwmben(pwmben),
          pwmstartvaerdi(pwmlow), pwmmaxvaerdi(pwmhigh),
          lysparam_ptr(lysparam) {
        dimmerinit();
    }

    void setLysParam(LysParam* p) { lysparam_ptr = p; }

    void sluk()  { setlysiprocentSoft(0); }
    void taend() { setlysiprocentSoft(100); }

    /** Start softstart mod slutProcent. */
    void startSoftStart(int slutProcent = 100) {
        softstart_aktiv = true;
        softsluk_aktiv = false;
        soft_slut = slutProcent;
        soft_step = hentStep();
        soft_nuvaerende = aktuelprocentvaerdi;
    }

    /** Kaldes 4 Hz – ét step op mod soft_slut. */
    void softstartStep() {
        if (!softstart_aktiv) return;
        soft_nuvaerende += soft_step;
        if (soft_nuvaerende >= soft_slut) {
            setlysiprocent(soft_slut);
            softstart_aktiv = false;
        } else {
            setlysiprocent(soft_nuvaerende);
        }
    }

    bool softstartAktiv() { return softstart_aktiv; }

    /** Start softsluk mod slutProcent. */
    void startSoftSluk(int slutProcent) {
        softsluk_aktiv = true;
        softstart_aktiv = false;
        soft_slut = slutProcent;
        soft_step = hentStep();
        soft_nuvaerende = aktuelprocentvaerdi;
    }

    /** Kaldes 4 Hz – ét step ned mod soft_slut. */
    void softslukStep() {
        if (!softsluk_aktiv) return;
        soft_nuvaerende -= soft_step;
        if (soft_nuvaerende <= soft_slut) {
            setlysiprocent(soft_slut);
            softsluk_aktiv = false;
        } else {
            setlysiprocent(soft_nuvaerende);
        }
    }

    /** Start softstart eller softsluk afhængigt af retning. */
    void setlysiprocentSoft(int nyvaerdi) {
        if (nyvaerdi < 0 || nyvaerdi > 100) return;
        aktuelsetvaerdi = nyvaerdi;
        if (nyvaerdi > aktuelprocentvaerdi) {
            startSoftStart(nyvaerdi);
        } else if (nyvaerdi < aktuelprocentvaerdi) {
            startSoftSluk(nyvaerdi);
        }
    }

    bool softslukAktiv() { return softsluk_aktiv; }

    int returnersetvaerdi()    { return aktuelsetvaerdi; }
    int returneraktuelvaerdi() { return aktuelprocentvaerdi; }
};
