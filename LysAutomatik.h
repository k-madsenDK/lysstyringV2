#pragma once
#include "LysParam.h"
#include "Dimmerfunktion.h"

class LysAutomatik {
private:
    enum LysState { OFF, TIMER_A, TIMER_C, TIMER_E, NIGHT_GLOW };
    LysState currentState = OFF;
    LysParam& param;
    dimmerfunktion* dimmer;
    bool slukActiveret = false;
    long timerA = 0, timerC = 0, timerE = 0;
    bool nataktiv = false;
    long natdagdelayTimer = 0;
    bool lastLuxOver = true;

public:
    LysAutomatik(LysParam& p, dimmerfunktion* d) : param(p), dimmer(d) {}

    void update(float lux, bool pirEvent, time_t ntpTid) {
        if (!nataktiv && lux < param.luxstartvaerdi && lastLuxOver) {
            natdagdelayTimer = param.natdagdelay;
            lastLuxOver = false;
        }
        if (lux >= param.luxstartvaerdi) {
            lastLuxOver = true;
            if (param.lognataktiv && nataktiv)
                rp2040.fifo.push_nb(nataktivfalse);
            if (nataktiv) nataktiv = false;
        }

        if (slukActiveret && nataktiv) {
            slukActiveret = false;
            switch(currentState){
              case TIMER_A: dimmer->setlysiprocentSoft(param.pwmA); break;
              case TIMER_C: dimmer->setlysiprocentSoft(param.pwmC); break;
              case TIMER_E: dimmer->setlysiprocentSoft(param.pwmE); break;
              case NIGHT_GLOW: dimmer->setlysiprocentSoft(param.pwmG); break;
              default: break;
            }
        } else {
            slukActiveret = false;
        }

        if (natdagdelayTimer > 0) {
            natdagdelayTimer--;
            if (natdagdelayTimer == 0) {
                nataktiv = true;
                if (param.lognataktiv) rp2040.fifo.push_nb(nataktivtrue);
            }
        }

        if (nataktiv) {
            if (currentState == OFF) startA(ntpTid);
            if (pirEvent) startC();
        } else {
            if (currentState != OFF) {
                dimmer->sluk();
                currentState = OFF;
            }
        }

        switch (currentState) {
            case TIMER_A:
                if (--timerA <= 0) {
                    currentState = NIGHT_GLOW;
                    dimmer->setlysiprocentSoft(param.pwmG);
                }
                break;
            case TIMER_C:
                if (timerA > 0) --timerA;
                if (--timerC <= 0) startE();
                break;
            case TIMER_E:
                if (timerA > 0) --timerA;
                if (--timerE <= 0) {
                    if (timerA > 0) resumeA();
                    else {
                        currentState = NIGHT_GLOW;
                        dimmer->setlysiprocentSoft(param.pwmG);
                    }
                }
                break;
            case NIGHT_GLOW:
            case OFF:
            default:
                break;
        }
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

    void forceOn()  { dimmer->taend(); }
    void forceOff() { dimmer->sluk(); slukActiveret = true; }
    bool getNataktiv() const { return nataktiv; }
};
