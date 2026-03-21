#pragma once
/**
 * @file pirroutiner.h
 * @brief PIR-sensor og hardware-switch håndtering med debounce og tidsstempling.
 *
 * Håndterer 2× PIR-indgange og 1× hardware switch (alle aktiv LOW med intern pull-up).
 * Debounce via tæller i timerRoutine() som kaldes 4 Hz (250 ms).
 * Log-events sendes til core0 via FIFO. Tidsstempler beskyttes med pir_mutex.
 */

#include <Arduino.h>
#include "hardware/rtc.h"
#include "LysParam.h"

extern mutex_t pir_mutex;
extern mutex_t param_mutex;

class pirroutiner {
private:
    int pir1ben = -1;
    int pir2ben = -1;
    int hwswben = -1;

    bool pir1_tilstede = false;
    bool pir2_tilstede = false;
    bool hwsw_tilstede = false;

    bool pir1_aktiv = false;
    bool pir2_aktiv = false;
    bool hwsw_aktiv = false;

    bool pir1_aktiveret = false;
    bool pir2_aktiveret = false;
    bool hwsw_aktiveret = false;

    uint8_t pir1_count = 0;
    uint8_t pir2_count = 0;
    uint8_t hwsw_count = 0;

    String* pir1_tid;
    String* pir2_tid;
    String* hwsw_tid;

    LysParam& param;

    /** Initialisér GPIO med intern pull-up. */
    void initInputs(void) {
        if (pir1ben >= 0 && pir1ben < 29) {
            pinMode(pir1ben, INPUT_PULLUP);
            pir1_tilstede = true;
        }
        if (pir2ben >= 0 && pir2ben < 29) {
            pinMode(pir2ben, INPUT_PULLUP);
            pir2_tilstede = true;
        }
        if (hwswben >= 0 && hwswben < 29) {
            pinMode(hwswben, INPUT_PULLUP);
            hwsw_tilstede = true;
        }
    }

    /** Hent tidsstempel fra RTC som string. */
    String tidSomStrengFraRTC() {
        datetime_t t;
        rtc_get_datetime(&t);
        char buf[20];
        sprintf(buf, "%04d-%02d-%02d %02d:%02d:%02d", t.year, t.month, t.day, t.hour, t.min, t.sec);
        return String(buf);
    }

public:
    /**
     * @brief Constructor.
     * @param pir1 GPIO til PIR1.
     * @param pir2 GPIO til PIR2.
     * @param hwsw GPIO til hardware switch.
     * @param pir1txt Pointer til PIR1-tidsstempel (mutex-beskyttet).
     * @param pir2txt Pointer til PIR2-tidsstempel (mutex-beskyttet).
     * @param hwswtxt Pointer til HW-switch-tidsstempel (mutex-beskyttet).
     * @param p Reference til LysParam.
     */
    pirroutiner(int pir1, int pir2, int hwsw, String* pir1txt, String* pir2txt, String* hwswtxt, LysParam& p)
        : pir1_tid(pir1txt), pir2_tid(pir2txt), hwsw_tid(hwswtxt), param(p)
    {
        this->pir1ben = pir1;
        this->pir2ben = pir2;
        this->hwswben = hwsw;
        this->initInputs();
    }

    /**
     * @brief Debounce-rutine – kaldes 4 Hz fra softlysIrq().
     *        Detekterer PIR/HW aktivering, opdaterer tidsstempler og sender FIFO-events.
     */
    void timerRoutine() {
        // PIR1
        if (pir1_tilstede) {
            if (digitalRead(pir1ben) == LOW) {
                pir1_count++;
                if (pir1_count >= 3 && !pir1_aktiveret && !pir1_aktiv) {
                    pir1_aktiveret = true;
                    pir1_aktiv = true;
                    uint32_t owner = 0;
                    if (mutex_try_enter(&param_mutex, &owner)) {
                        if (param.logpirdetection) rp2040.fifo.push_nb(pir1_detection);
                        mutex_exit(&param_mutex);
                    }
                    if (mutex_try_enter(&pir_mutex, &owner)) {
                        *pir1_tid = tidSomStrengFraRTC();
                        mutex_exit(&pir_mutex);
                    }
                }
            } else {
                pir1_count = 0;
                pir1_aktiv = false;
            }
        }

        // PIR2
        if (pir2_tilstede) {
            if (digitalRead(pir2ben) == LOW) {
                pir2_count++;
                if (pir2_count >= 3 && !pir2_aktiveret && !pir2_aktiv) {
                    pir2_aktiveret = true;
                    pir2_aktiv = true;
                    uint32_t owner = 0;
                    if (mutex_try_enter(&param_mutex, &owner)) {
                        if (param.logpirdetection) rp2040.fifo.push_nb(pir2_detection);
                        mutex_exit(&param_mutex);
                    }
                    if (mutex_try_enter(&pir_mutex, &owner)) {
                        *pir2_tid = tidSomStrengFraRTC();
                        mutex_exit(&pir_mutex);
                    }
                }
            } else {
                pir2_count = 0;
                pir2_aktiv = false;
            }
        }

        // Hardware switch
        if (hwsw_tilstede) {
            if (digitalRead(hwswben) == LOW) {
                hwsw_count++;
                if (hwsw_count >= 2 && !hwsw_aktiveret) {
                    hwsw_aktiveret = true;
                    uint32_t owner = 0;
                    if (mutex_try_enter(&param_mutex, &owner)) {
                        if (param.logpirdetection) rp2040.fifo.push_nb(hwsw_on);
                        mutex_exit(&param_mutex);
                    }
                    if (mutex_try_enter(&pir_mutex, &owner)) {
                        *hwsw_tid = tidSomStrengFraRTC();
                        mutex_exit(&pir_mutex);
                    }
                }
            } else {
                hwsw_count = 0;
                hwsw_aktiveret = false;
            }
            hwsw_aktiv = (digitalRead(hwswben) == LOW);
        }
    }

    /** Log hardware switch OFF (kald ved overgang aktiv → inaktiv). */
    void logHWSWOff() {
        if (param.logpirdetection) {
            rp2040.fifo.push_nb(hwsw_off);
        }
        uint32_t owner = 0;
        if (mutex_try_enter(&pir_mutex, &owner)) {
            *hwsw_tid = tidSomStrengFraRTC();
            mutex_exit(&pir_mutex);
        }
    }

    bool isPIR1Activated() {
        bool wasActivated = pir1_aktiveret;
        pir1_aktiveret = false;
        return wasActivated;
    }

    bool isPIR2Activated() {
        bool wasActivated = pir2_aktiveret;
        pir2_aktiveret = false;
        return wasActivated;
    }

    /** Log software-on event via FIFO. */
    void setswswOn(void) {
        uint32_t owner = 0;
        if (mutex_try_enter(&param_mutex, &owner)) {
            if (param.logpirdetection) rp2040.fifo.push_nb(swsw_on);
            mutex_exit(&param_mutex);
        }
    }

    bool isPIR1Present() { return pir1_tilstede; }
    bool isPIR2Present() { return pir2_tilstede; }
    bool isPIR1BenLow()  { return pir1_aktiv; }
    bool isPIR2BenLow()  { return pir2_aktiv; }
    String getPIR1Time() { return *pir1_tid; }
    String getPIR2Time() { return *pir2_tid; }

    bool isHWSWPresent() { return hwsw_tilstede; }
    bool isHWSWBenLow()  { return hwsw_aktiv; }
    String getHWSWTime() { return *hwsw_tid; }
};
