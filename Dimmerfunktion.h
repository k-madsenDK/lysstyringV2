#pragma once
/**
 * @file Dimmerfunktion.h
 * @brief AC-dimmer med softstart/softsluk via PWM + relæ.
 *
 * PWM 10 kHz, 16-bit range. Relæ til/frakobling af last.
 * Softstart og softsluk med konfigurerbart step fra LysParam::aktuelStepfrekvens.
 * Step-funktioner kaldes 4 Hz fra softlysIrq() i core1.
 *
 * VIGTIGT:
 * Alt under eller lig med OFF_CUTOFF_PROCENT behandles som OFF.
 * Det betyder, at fx 1-25% fysisk slukker relæ og PWM.
 */

#include <Arduino.h>
#include "LysParam.h"

class dimmerfunktion {
private:
    int pwmstartvaerdi;
    int pwmmaxvaerdi;
    int enprocent = 0;
    int relayben;
    int pwmben;

    int aktuelpwmvaerdi = 0;
    int aktuelprocentvaerdi = 0;

    bool softstart_aktiv = false;
    bool softsluk_aktiv = false;

    int soft_slut = 100;
    int soft_step = 5;
    int soft_nuvaerende = 0;

    int aktuelsetvaerdi = 0;

    LysParam* lysparam_ptr = nullptr;

    // ALT <= 25% bliver fysisk OFF.
    // Dvs. hvis automatikken prøver at pulse 5/10/15/20%, bliver lyset slukket.
    static constexpr int OFF_CUTOFF_PROCENT = 25;

    void dimmerinit() {
        analogWriteRange(65535);
        analogWriteFreq(10000);
        analogWrite(pwmben, 0);

        pinMode(relayben, OUTPUT);
        digitalWrite(relayben, 0);

        enprocent = (pwmmaxvaerdi - pwmstartvaerdi) / 100;

        aktuelpwmvaerdi = 0;
        aktuelprocentvaerdi = 0;
        aktuelsetvaerdi = 0;
        softstart_aktiv = false;
        softsluk_aktiv = false;
    }

    void relayOn() {
        digitalWrite(relayben, 1);
    }

    void relayOff() {
        digitalWrite(relayben, 0);
    }

    int clampProcent(int v) const {
        if (v < 0) return 0;
        if (v > 100) return 100;
        return v;
    }

    int normaliserTarget(int v) const {
        v = clampProcent(v);

        // Kommandoer på 25% eller mindre skal være OFF.
        if (v <= OFF_CUTOFF_PROCENT) {
            return 0;
        }

        return v;
    }

    int normaliserOutput(int v) const {
        v = clampProcent(v);

        // Fysisk output på 25% eller mindre slukkes helt.
        if (v <= OFF_CUTOFF_PROCENT) {
            return 0;
        }

        return v;
    }

    int hentStep() const {
        if (lysparam_ptr && lysparam_ptr->aktuelStepfrekvens > 0) {
            return lysparam_ptr->aktuelStepfrekvens;
        }

        return 5;
    }

    void stopSoft() {
        softstart_aktiv = false;
        softsluk_aktiv = false;
    }

    /**
     * Skriv fysisk output.
     *
     * Vigtigt:
     * Denne funktion ændrer IKKE aktuelsetvaerdi.
     * Ellers kan softstart miste sit mål, når de første trin under 25%
     * fysisk bliver omsat til 0.
     */
    bool writeOutputProcent(int nyvaerdi) {
        nyvaerdi = normaliserOutput(nyvaerdi);

        aktuelprocentvaerdi = nyvaerdi;

        if (nyvaerdi == 0) {
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

public:
    dimmerfunktion(int relayben = 2,
                   int pwmben = 0,
                   int pwmlow = 0,
                   int pwmhigh = 65535,
                   LysParam* lysparam = nullptr)
        : relayben(relayben),
          pwmben(pwmben),
          pwmstartvaerdi(pwmlow),
          pwmmaxvaerdi(pwmhigh),
          lysparam_ptr(lysparam) {
        dimmerinit();
    }

    void setLysParam(LysParam* p) {
        lysparam_ptr = p;
    }

    void sluk() {
        setlysiprocentSoft(0);
    }

    void taend() {
        setlysiprocentSoft(100);
    }

    void slukNu() {
        stopSoft();
        aktuelsetvaerdi = 0;
        soft_slut = 0;
        soft_nuvaerende = 0;
        writeOutputProcent(0);
    }

    void startSoftStart(int slutProcent = 100) {
        slutProcent = normaliserTarget(slutProcent);

        if (slutProcent == 0) {
            slukNu();
            return;
        }

        softstart_aktiv = true;
        softsluk_aktiv = false;
        soft_slut = slutProcent;
        soft_step = hentStep();
        soft_nuvaerende = aktuelprocentvaerdi;

        if (soft_step <= 0) {
            soft_step = 5;
        }
    }

    void softstartStep() {
        if (!softstart_aktiv) return;

        soft_nuvaerende += soft_step;

        if (soft_nuvaerende >= soft_slut) {
            writeOutputProcent(soft_slut);
            softstart_aktiv = false;
        } else {
            // Hvis trinnet er <=25%, skriver writeOutputProcent fysisk 0.
            // Men aktuelsetvaerdi bevares stadig som soft_slut.
            writeOutputProcent(soft_nuvaerende);
        }
    }

    bool softstartAktiv() {
        return softstart_aktiv;
    }

    void startSoftSluk(int slutProcent) {
        slutProcent = normaliserTarget(slutProcent);

        softsluk_aktiv = true;
        softstart_aktiv = false;
        soft_slut = slutProcent;
        soft_step = hentStep();
        soft_nuvaerende = aktuelprocentvaerdi;

        if (soft_step <= 0) {
            soft_step = 5;
        }

        if (soft_slut == 0 && aktuelprocentvaerdi <= OFF_CUTOFF_PROCENT) {
            slukNu();
        }
    }

    void softslukStep() {
        if (!softsluk_aktiv) return;

        soft_nuvaerende -= soft_step;

        // Hvis målet er OFF, så sluk helt når vi kommer ned på 25% eller mindre.
        if (soft_slut == 0) {
            if (soft_nuvaerende <= OFF_CUTOFF_PROCENT) {
                slukNu();
                return;
            }

            writeOutputProcent(soft_nuvaerende);
            return;
        }

        // Hvis målet er fx 30%, stop på 30.
        if (soft_nuvaerende <= soft_slut) {
            writeOutputProcent(soft_slut);
            softsluk_aktiv = false;
            return;
        }

        writeOutputProcent(soft_nuvaerende);
    }

    bool softslukAktiv() {
        return softsluk_aktiv;
    }

    void setlysiprocentSoft(int nyvaerdi) {
        nyvaerdi = normaliserTarget(nyvaerdi);

        // Hvis vi allerede arbejder mod samme mål, gør intet.
        if (nyvaerdi == aktuelsetvaerdi) {
            return;
        }

        aktuelsetvaerdi = nyvaerdi;

        if (nyvaerdi == 0) {
            if (aktuelprocentvaerdi <= OFF_CUTOFF_PROCENT) {
                slukNu();
            } else {
                startSoftSluk(0);
            }
            return;
        }

        if (nyvaerdi > aktuelprocentvaerdi) {
            startSoftStart(nyvaerdi);
        } else if (nyvaerdi < aktuelprocentvaerdi) {
            startSoftSluk(nyvaerdi);
        } else {
            stopSoft();
            writeOutputProcent(nyvaerdi);
        }
    }

    int returnersetvaerdi() {
        return aktuelsetvaerdi;
    }

    int returneraktuelvaerdi() {
        return aktuelprocentvaerdi;
    }
};
