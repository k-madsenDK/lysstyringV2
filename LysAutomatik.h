#pragma once

#include <Arduino.h>
#include <ctime>

#include "AstroSun.h"
#include "Dimmerfunktion.h"
#include "LysParam.h"

class LysAutomatik {
private:
    enum LysState {
        OFF,
        TIMER_A,
        TIMER_C,
        TIMER_E,
        NIGHT_GLOW
    };

    LysState currentState = OFF;

    LysParam& param;
    dimmerfunktion* dimmer;

    bool slukActiveret = false;

    long timerA = 0;
    long timerC = 0;
    long timerE = 0;

    bool nataktiv = false;

    long natdagdelayTimer = 0;
    long dagNatDelayTimer = 0;
    bool lastLuxOver = true;

    int cachedY = -1;
    int cachedM = -1;
    int cachedD = -1;
    AstroTimes cachedAstro;

    static int toSec(int h, int m, int s = 0) {
        return h * 3600 + m * 60 + s;
    }

    static int toMin(int h, int m) {
        return h * 60 + m;
    }

    static bool toLocalTm(time_t t, tm& out) {
        return localtime_r(&t, &out) != nullptr;
    }

    static int wrapMin(int m) {
        while (m < 0) m += 1440;
        while (m >= 1440) m -= 1440;
        return m;
    }

    static bool inRangeSec(int nowSec, int startSec, int endSec) {
        // OBS:
        // start == end betyder IKKE aktiv.
        // Det forhindrer segment 2/3 med 00:00 -> 00:00 i at blive "altid aktiv".
        if (startSec == endSec) return false;

        if (startSec < endSec) {
            return nowSec >= startSec && nowSec < endSec;
        }

        // Interval over midnat
        return nowSec >= startSec || nowSec < endSec;
    }

    static bool inRangeMin(int nowMin, int startMin, int endMin) {
        if (startMin == endMin) return false;

        if (startMin < endMin) {
            return nowMin >= startMin && nowMin < endMin;
        }

        // Interval over midnat
        return nowMin >= startMin || nowMin < endMin;
    }

    static int effectiveNightWday(int wday, int nowSec) {
        if (wday < 0 || wday > 6) return wday;

        // Før middag regnes som natten før.
        if (nowSec < toSec(12, 0, 0)) {
            return (wday + 6) % 7;
        }

        return wday;
    }

    void ensureAstroCached(time_t ntpTid) {
        tm ti;
        if (!toLocalTm(ntpTid, ti)) return;

        int y = ti.tm_year + 1900;
        int m = ti.tm_mon + 1;
        int d = ti.tm_mday;

        if (y != cachedY || m != cachedM || d != cachedD || !cachedAstro.valid()) {
            cachedAstro = AstroSun::computeLocalTimes(
                y,
                m,
                d,
                param.astroLat,
                param.astroLon
            );

            cachedY = y;
            cachedM = m;
            cachedD = d;
        }
    }

    bool getAstroRiseSetMin(time_t ntpTid, int& sunriseMin, int& sunsetMin) {
        ensureAstroCached(ntpTid);

        if (!cachedAstro.valid()) {
            return false;
        }

        sunriseMin = wrapMin(cachedAstro.sunriseMin + param.astroSunriseOffsetMin);
        sunsetMin  = wrapMin(cachedAstro.sunsetMin  + param.astroSunsetOffsetMin);

        return true;
    }

    bool isSegmentMode() const {
        return param.styringsvalg == "Klokken" || param.styringsvalg == "Astro";
    }

    void setNataktiv(bool newVal) {
        if (nataktiv == newVal) return;

        nataktiv = newVal;

        if (param.lognataktiv) {
            rp2040.fifo.push_nb(nataktiv ? nataktivtrue : nataktivfalse);
        }
    }

    void resetLuxTimers() {
        natdagdelayTimer = 0;
        dagNatDelayTimer = 0;
        lastLuxOver = true;
    }

    void applyOutputForState(LysState state) {
        if (!dimmer) return;

        switch (state) {
            case OFF:
                if (dimmer->returnersetvaerdi() != 0) {
                    dimmer->sluk();
                }
                break;

            case TIMER_A:
                if (dimmer->returnersetvaerdi() != param.pwmA) {
                    dimmer->setlysiprocentSoft(param.pwmA);
                }
                break;

            case TIMER_C:
                if (dimmer->returnersetvaerdi() != param.pwmC) {
                    dimmer->setlysiprocentSoft(param.pwmC);
                }
                break;

            case TIMER_E:
                if (dimmer->returnersetvaerdi() != param.pwmE) {
                    dimmer->setlysiprocentSoft(param.pwmE);
                }
                break;

            case NIGHT_GLOW:
                if (dimmer->returnersetvaerdi() != param.pwmG) {
                    dimmer->setlysiprocentSoft(param.pwmG);
                }
                break;
        }
    }

    void changeState(LysState newState) {
        if (currentState == newState) return;

        currentState = newState;
        applyOutputForState(currentState);
    }

    bool dayAllowedCurrent(int wday, uint8_t mask) const {
        if (wday < 0 || wday > 6) return true;
        return (mask & (1u << wday)) != 0;
    }

    bool segment2Active(int nowSec, int wday, int& outEndSec) {
        if (!param.seg2Enabled) return false;
        if (!dayAllowedCurrent(wday, param.seg2WeekMask)) return false;

        int startSec = toSec(param.seg2StartTimer, param.seg2StartMinutter, 0);
        int endSec   = toSec(param.seg2SlutTimer,  param.seg2SlutMinutter,  0);

        if (startSec == endSec) return false;

        if (inRangeSec(nowSec, startSec, endSec)) {
            outEndSec = endSec;
            return true;
        }

        return false;
    }

    bool segment3Active(int nowSec, int wday, int& outEndSec) {
        if (!param.seg3Enabled) return false;
        if (!dayAllowedCurrent(wday, param.seg3WeekMask)) return false;

        int startSec = toSec(param.seg3StartTimer, param.seg3StartMinutter, 0);
        int endSec   = toSec(param.seg3SlutTimer,  param.seg3SlutMinutter,  0);

        if (startSec == endSec) return false;

        if (inRangeSec(nowSec, startSec, endSec)) {
            outEndSec = endSec;
            return true;
        }

        return false;
    }

    // ------------------------------------------------------------
    // Klokken-mode
    // ------------------------------------------------------------
    bool klokWantAAndEnd(time_t ntpTid, int& outEndSec) {
        tm timeinfo;

        if (!toLocalTm(ntpTid, timeinfo)) {
            outEndSec = toSec(param.slutKlokkeTimer, param.slutKlokkeMinutter, 0);
            return false;
        }

        int nowSec = toSec(timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        int wday   = timeinfo.tm_wday;

        const int noonSec = toSec(12, 0, 0);
        const int seg1End = toSec(param.slutKlokkeTimer, param.slutKlokkeMinutter, 0);

        bool wantA = false;

        /*
          Segment 1:

          Slut efter middag, fx 22:00:
            Aktiv fra 12:00 til 22:00.
            IKKE aktiv efter midnat.

          Slut før middag, fx 02:00:
            Aktiv fra 12:00 over midnat til 02:00.
        */
        if (seg1End < noonSec) {
            wantA = (nowSec >= noonSec) || (nowSec < seg1End);
        } else {
            wantA = (nowSec >= noonSec) && (nowSec < seg1End);
        }

        // Segment 2/3 kan give TIMER_A uden for segment 1
        if (segment2Active(nowSec, wday, outEndSec)) {
            return true;
        }

        if (segment3Active(nowSec, wday, outEndSec)) {
            return true;
        }

        outEndSec = seg1End;
        return wantA;
    }

    // ------------------------------------------------------------
    // Astro-mode
    // ------------------------------------------------------------
    bool astroWantAAndEnd(time_t ntpTid, int& outEndSec) {
        tm timeinfo;

        if (!toLocalTm(ntpTid, timeinfo)) {
            outEndSec = toSec(param.slutKlokkeTimer, param.slutKlokkeMinutter, 0);
            return false;
        }

        int nowSec = toSec(timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        int wdayEff = effectiveNightWday(timeinfo.tm_wday, nowSec);

        int sunriseMin = 0;
        int sunsetMin = 0;

        if (!getAstroRiseSetMin(ntpTid, sunriseMin, sunsetMin)) {
            return klokWantAAndEnd(ntpTid, outEndSec);
        }

        const int noonSec   = toSec(12, 0, 0);
        const int sunsetSec = sunsetMin * 60;
        const int seg1End   = toSec(param.slutKlokkeTimer, param.slutKlokkeMinutter, 0);

        bool wantA = false;

        /*
          Astro segment 1:

          Slut efter middag, fx 22:00:
            Aktiv fra solnedgang til 22:00.
            Hvis solnedgang er senere end 22:00, er segment 1 ikke aktiv.
            IKKE aktiv efter midnat.

          Slut før middag, fx 02:00:
            Aktiv fra solnedgang over midnat til 02:00.
        */
        if (seg1End < noonSec) {
            wantA = (nowSec >= sunsetSec) || (nowSec < seg1End);
        } else {
            if (sunsetSec < seg1End) {
                wantA = (nowSec >= sunsetSec) && (nowSec < seg1End);
            } else {
                wantA = false;
            }
        }

        // Segment 2/3 kan give TIMER_A uden for astro segment 1
        if (segment2Active(nowSec, wdayEff, outEndSec)) {
            return true;
        }

        if (segment3Active(nowSec, wdayEff, outEndSec)) {
            return true;
        }

        outEndSec = seg1End;
        return wantA;
    }

    bool segmentWantAAndEnd(time_t ntpTid, int& outEndSec) {
        if (param.styringsvalg == "Astro") {
            return astroWantAAndEnd(ntpTid, outEndSec);
        }

        return klokWantAAndEnd(ntpTid, outEndSec);
    }

    void setTimerAToEnd(time_t ntpTid, int endSec) {
        tm timeinfo;

        if (!toLocalTm(ntpTid, timeinfo)) {
            timerA = 0;
            return;
        }

        int nowSec = toSec(timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

        timerA = endSec - nowSec;

        if (timerA < 0) {
            timerA += 24L * 3600L;
        }

        /*
          Hvis der er mere end 12 timer til slut, er vi uden for det aktive segment.
          Så må TIMER_A ikke starte.
        */
        if (timerA > 12L * 3600L) {
            timerA = 0;
        }

        if (timerA < 0) {
            timerA = 0;
        }
    }

    bool startTimerAIfValid(time_t ntpTid, int endSec) {
        setTimerAToEnd(ntpTid, endSec);

        if (timerA <= 0) {
            changeState(NIGHT_GLOW);
            return false;
        }

        changeState(TIMER_A);
        return true;
    }

    void updateLuxNat(float lux) {
        // Dag -> nat
        if (!nataktiv) {
            dagNatDelayTimer = 0;

            if (lux < param.luxstartvaerdi && lastLuxOver) {
                natdagdelayTimer = param.natdagdelay;
                lastLuxOver = false;
            }

            if (lux >= param.luxstartvaerdi) {
                lastLuxOver = true;
            }

            if (natdagdelayTimer > 0) {
                natdagdelayTimer--;

                if (natdagdelayTimer == 0) {
                    setNataktiv(true);
                }
            }
        }

        // Nat -> dag
        if (nataktiv) {
            natdagdelayTimer = 0;

            if (lux >= param.luxstartvaerdi) {
                if (dagNatDelayTimer == 0) {
                    dagNatDelayTimer = param.natdagdelay;
                }
            } else {
                dagNatDelayTimer = 0;
                lastLuxOver = false;
            }

            if (dagNatDelayTimer > 0) {
                dagNatDelayTimer--;

                if (dagNatDelayTimer == 0) {
                    setNataktiv(false);
                }
            }
        }
    }

    void updateAstroMode(float lux, time_t ntpTid) {
        int sunriseMin = 0;
        int sunsetMin = 0;

        if (!getAstroRiseSetMin(ntpTid, sunriseMin, sunsetMin)) {
            updateLuxNat(lux);
            return;
        }

        tm ti;
        if (!toLocalTm(ntpTid, ti)) return;

        int nowMin = toMin(ti.tm_hour, ti.tm_min);
        bool astroNight = inRangeMin(nowMin, sunsetMin, sunriseMin);

        if (astroNight) {
            setNataktiv(true);
            resetLuxTimers();
        } else {
            updateLuxNat(lux);
        }
    }

public:
    LysAutomatik(LysParam& p, dimmerfunktion* d)
        : param(p), dimmer(d) {}

    void initFromNow(float lux, time_t ntpTid) {
        currentState = OFF;
        slukActiveret = false;
        timerA = 0;
        timerC = 0;
        timerE = 0;
        resetLuxTimers();

        if (param.styringsvalg == "Astro" && param.astroEnabled) {
            int sunriseMin = 0;
            int sunsetMin = 0;

            if (getAstroRiseSetMin(ntpTid, sunriseMin, sunsetMin)) {
                tm ti;

                if (toLocalTm(ntpTid, ti)) {
                    int nowMin = toMin(ti.tm_hour, ti.tm_min);
                    bool astroNight = inRangeMin(nowMin, sunsetMin, sunriseMin);

                    if (astroNight) {
                        setNataktiv(true);
                    } else {
                        setNataktiv(lux < param.luxstartvaerdi);
                        lastLuxOver = (lux >= param.luxstartvaerdi);
                    }
                } else {
                    setNataktiv(true);
                }
            } else {
                setNataktiv(lux < param.luxstartvaerdi);
                lastLuxOver = (lux >= param.luxstartvaerdi);
            }
        } else {
            setNataktiv(lux < param.luxstartvaerdi);
            lastLuxOver = (lux >= param.luxstartvaerdi);
        }

        if (!nataktiv) {
            currentState = OFF;
            applyOutputForState(OFF);
            return;
        }

        if (isSegmentMode()) {
            int endSec = 0;
            bool wantA = segmentWantAAndEnd(ntpTid, endSec);

            if (wantA) {
                startTimerAIfValid(ntpTid, endSec);
            } else {
                changeState(NIGHT_GLOW);
            }
        } else {
            startA(ntpTid);
        }
    }

    void update(float lux, bool pirEvent, time_t ntpTid) {
        // 1) Nat/dag
        if (param.styringsvalg == "Astro" && param.astroEnabled) {
            updateAstroMode(lux, ntpTid);
        } else {
            updateLuxNat(lux);
        }

        // 2) Return fra forceOff
        if (slukActiveret && nataktiv) {
            slukActiveret = false;
            applyOutputForState(currentState);
        } else {
            slukActiveret = false;
        }

        // 3) Hovedlogik
        if (nataktiv) {
            if (pirEvent) {
                startC();
            }

            if (isSegmentMode()) {
                int endSec = 0;
                bool wantA = segmentWantAAndEnd(ntpTid, endSec);

                if (currentState == OFF) {
                    if (wantA) {
                        startTimerAIfValid(ntpTid, endSec);
                    } else {
                        changeState(NIGHT_GLOW);
                    }
                } else if (currentState == TIMER_A) {
                    if (!wantA) {
                        changeState(NIGHT_GLOW);
                    } else {
                        // Opdater timerA, men start ikke noget nyt output.
                        setTimerAToEnd(ntpTid, endSec);

                        if (timerA <= 0) {
                            changeState(NIGHT_GLOW);
                        }
                    }
                } else if (currentState == NIGHT_GLOW) {
                    if (wantA) {
                        startTimerAIfValid(ntpTid, endSec);
                    }
                }
            } else {
                if (currentState == OFF) {
                    startA(ntpTid);
                }
            }
        } else {
            if (currentState != OFF) {
                changeState(OFF);
            }
        }

        // 4) Countdown
        switch (currentState) {
            case TIMER_A:
                if (timerA > 0) {
                    timerA--;
                }

                if (timerA <= 0) {
                    changeState(NIGHT_GLOW);
                }
                break;

            case TIMER_C:
                if (timerA > 0) {
                    timerA--;
                }

                timerC--;

                if (timerC <= 0) {
                    startE();
                }
                break;

            case TIMER_E:
                if (timerA > 0) {
                    timerA--;
                }

                timerE--;

                if (timerE <= 0) {
                    if (isSegmentMode()) {
                        int endSec = 0;
                        bool wantA = segmentWantAAndEnd(ntpTid, endSec);

                        if (wantA) {
                            startTimerAIfValid(ntpTid, endSec);
                        } else {
                            changeState(NIGHT_GLOW);
                        }
                    } else {
                        if (timerA > 0) {
                            resumeA();
                        } else {
                            changeState(NIGHT_GLOW);
                        }
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
        if (isSegmentMode()) {
            int endSec = 0;
            bool wantA = segmentWantAAndEnd(ntpTid, endSec);

            if (!wantA) {
                changeState(NIGHT_GLOW);
                return;
            }

            startTimerAIfValid(ntpTid, endSec);
            return;
        }

        timerA = param.timerA;

        if (timerA <= 0) {
            changeState(NIGHT_GLOW);
            return;
        }

        changeState(TIMER_A);
    }

    void startC() {
        timerC = param.timerC;

        if (timerC <= 0) {
            startE();
            return;
        }

        changeState(TIMER_C);
    }

    void startE() {
        timerE = param.timerE;

        if (timerE <= 0) {
            changeState(NIGHT_GLOW);
            return;
        }

        changeState(TIMER_E);
    }

    void resumeA() {
        if (timerA > 0) {
            changeState(TIMER_A);
        } else {
            changeState(NIGHT_GLOW);
        }
    }

    void forceOn() {
        if (dimmer) {
            dimmer->taend();
        }
    }

    void forceOff() {
        if (dimmer) {
            dimmer->sluk();
        }

        slukActiveret = true;
    }

    bool getNataktiv() const {
        return nataktiv;
    }
};
