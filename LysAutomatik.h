#pragma once

#include <Arduino.h>
#include <ctime>

#include "AstroSun.h"
#include "Dimmerfunktion.h"
#include "LysParam.h"

class LysAutomatik {
private:
    enum LysState { OFF, TIMER_A, TIMER_C, TIMER_E, NIGHT_GLOW };
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

    int cachedY = -1, cachedM = -1, cachedD = -1;
    AstroTimes cachedAstro;

    static int toSec(int h, int m, int s = 0) { return h * 3600 + m * 60 + s; }
    static int toMin(int h, int m) { return h * 60 + m; }

    static bool inRangeSec(int nowSec, int startSec, int endSec) {
        if (startSec == endSec) return true;
        if (startSec < endSec)  return (nowSec >= startSec && nowSec < endSec);
        return (nowSec >= startSec || nowSec < endSec);
    }

    static bool inRangeMin(int nowMin, int startMin, int endMin) {
        if (startMin == endMin) return true;
        if (startMin < endMin)  return (nowMin >= startMin && nowMin < endMin);
        return (nowMin >= startMin || nowMin < endMin);
    }

    static int wrapMin(int m) {
        while (m < 0) m += 1440;
        while (m >= 1440) m -= 1440;
        return m;
    }

    static bool toLocalTm(time_t t, tm& out) {
        return localtime_r(&t, &out) != nullptr;
    }

    void ensureAstroCached(time_t ntpTid) {
        tm ti;
        if (!toLocalTm(ntpTid, ti)) return;
        int y = ti.tm_year + 1900;
        int m = ti.tm_mon + 1;
        int d = ti.tm_mday;
        if (y != cachedY || m != cachedM || d != cachedD || !cachedAstro.valid()) {
            cachedAstro = AstroSun::computeLocalTimes(y, m, d, param.astroLat, param.astroLon);
            cachedY = y; cachedM = m; cachedD = d;
        }
    }

    bool getAstroRiseSetMin(time_t ntpTid, int& sunriseMin, int& sunsetMin) {
        ensureAstroCached(ntpTid);
        if (!cachedAstro.valid()) return false;
        sunriseMin = wrapMin(cachedAstro.sunriseMin + param.astroSunriseOffsetMin);
        sunsetMin  = wrapMin(cachedAstro.sunsetMin  + param.astroSunsetOffsetMin);
        return true;
    }

    bool isSegmentMode() const {
        return (param.styringsvalg == "Klokken" || param.styringsvalg == "Astro");
    }

    void setNataktiv(bool newVal) {
        if (nataktiv == newVal) return;
        nataktiv = newVal;
        if (param.lognataktiv) rp2040.fifo.push_nb(nataktiv ? nataktivtrue : nataktivfalse);
    }

    void resetLuxTimers() {
        natdagdelayTimer = 0;
        dagNatDelayTimer = 0;
        lastLuxOver = true;
    }

    static int effectiveNightWday(int wday, int nowSec) {
        if (wday < 0 || wday > 6) return wday;
        if (nowSec < toSec(12, 0, 0)) return (wday + 6) % 7;
        return wday;
    }

    // ---------- Segment (Klokken) ----------
    bool klokWantAAndEnd(time_t ntpTid, int& outEndSec) {
        tm timeinfo;
        if (!toLocalTm(ntpTid, timeinfo)) {
            outEndSec = toSec(param.slutKlokkeTimer, param.slutKlokkeMinutter, 0);
            return true;
        }
        int nowSec = toSec(timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        int wday = timeinfo.tm_wday;
        auto dayAllowed = [&](uint8_t mask) -> bool {
            if (wday < 0 || wday > 6) return true;
            return (mask & (1u << wday)) != 0;
        };
        const int seg1End = toSec(param.slutKlokkeTimer, param.slutKlokkeMinutter, 0);
        bool wantA;
        if (seg1End < toSec(12, 0, 0)) {
            wantA = (nowSec >= toSec(12, 0, 0)) || (nowSec < seg1End);
        } else {
            wantA = (nowSec < seg1End);
        }
        if (param.seg2Enabled && dayAllowed(param.seg2WeekMask)) {
            int s2Start = toSec(param.seg2StartTimer, param.seg2StartMinutter, 0);
            int s2End   = toSec(param.seg2SlutTimer,  param.seg2SlutMinutter,  0);
            if (inRangeSec(nowSec, s2Start, s2End)) { outEndSec = s2End; return true; }
        }
        if (param.seg3Enabled && dayAllowed(param.seg3WeekMask)) {
            int s3Start = toSec(param.seg3StartTimer, param.seg3StartMinutter, 0);
            int s3End   = toSec(param.seg3SlutTimer,  param.seg3SlutMinutter,  0);
            if (inRangeSec(nowSec, s3Start, s3End)) { outEndSec = s3End; return true; }
        }
        outEndSec = seg1End;
        return wantA;
    }

    // ---------- Segment (Astro) ----------
    bool astroWantAAndEnd(time_t ntpTid, int& outEndSec) {
        tm timeinfo;
        if (!toLocalTm(ntpTid, timeinfo)) {
            outEndSec = toSec(param.slutKlokkeTimer, param.slutKlokkeMinutter, 0);
            return true;
        }
        int nowSec = toSec(timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        int wdayEff = effectiveNightWday(timeinfo.tm_wday, nowSec);
        auto dayAllowed = [&](uint8_t mask) -> bool {
            if (wdayEff < 0 || wdayEff > 6) return true;
            return (mask & (1u << wdayEff)) != 0;
        };
        int sunriseMin = 0, sunsetMin = 0;
        if (!getAstroRiseSetMin(ntpTid, sunriseMin, sunsetMin)) {
            return klokWantAAndEnd(ntpTid, outEndSec);
        }
        const int sunsetSec = sunsetMin * 60;
        const int seg1End   = toSec(param.slutKlokkeTimer, param.slutKlokkeMinutter, 0);
        bool wantA = inRangeSec(nowSec, sunsetSec, seg1End);
        if (param.seg2Enabled && dayAllowed(param.seg2WeekMask)) {
            int s2Start = toSec(param.seg2StartTimer, param.seg2StartMinutter, 0);
            int s2End   = toSec(param.seg2SlutTimer,  param.seg2SlutMinutter,  0);
            if (inRangeSec(nowSec, s2Start, s2End)) { outEndSec = s2End; return true; }
        }
        if (param.seg3Enabled && dayAllowed(param.seg3WeekMask)) {
            int s3Start = toSec(param.seg3StartTimer, param.seg3StartMinutter, 0);
            int s3End   = toSec(param.seg3SlutTimer,  param.seg3SlutMinutter,  0);
            if (inRangeSec(nowSec, s3Start, s3End)) { outEndSec = s3End; return true; }
        }
        outEndSec = seg1End;
        return wantA;
    }

    // ---------- Segment router ----------
    // I astro-dag: hvis nataktiv (via lux) → brug klokken-logik (seg1End som slut)
    // I astro-nat: brug astro-segmenter normalt
    bool segmentWantAAndEnd(time_t ntpTid, int& outEndSec) {
        if (param.styringsvalg == "Astro") {
            int sunriseMin = 0, sunsetMin = 0;
            if (getAstroRiseSetMin(ntpTid, sunriseMin, sunsetMin)) {
                tm ti;
                if (toLocalTm(ntpTid, ti)) {
                    int nowMin = toMin(ti.tm_hour, ti.tm_min);
                    bool astroNight = inRangeMin(nowMin, sunsetMin, sunriseMin);
                    if (!astroNight && nataktiv) {
                        // Astro-dag men nataktiv via lux → brug klokken-logik
                        return klokWantAAndEnd(ntpTid, outEndSec);
                    }
                }
            }
            return astroWantAAndEnd(ntpTid, outEndSec);
        }
        return klokWantAAndEnd(ntpTid, outEndSec);
    }

    void setTimerAToEnd(time_t ntpTid, int endSec) {
        tm timeinfo;
        if (!toLocalTm(ntpTid, timeinfo)) return;
        int nowSec = toSec(timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        timerA = endSec - nowSec;
        if (timerA < 0) timerA += 24L * 3600L;
        if (timerA > 12L * 3600L) timerA = 0;
        if (timerA < 0) timerA = 0;
    }

    // ---------- Lux nat/dag ----------
    void updateLuxNat(float lux) {
        // Dag → nat
        if (!nataktiv) {
            dagNatDelayTimer = 0;
            if (lux < param.luxstartvaerdi && lastLuxOver) {
                natdagdelayTimer = param.natdagdelay;
                lastLuxOver = false;
            }
            if (lux >= param.luxstartvaerdi) lastLuxOver = true;
            if (natdagdelayTimer > 0) {
                natdagdelayTimer--;
                if (natdagdelayTimer == 0) setNataktiv(true);
            }
        }
        // Nat → dag
        if (nataktiv) {
            natdagdelayTimer = 0;
            if (lux >= param.luxstartvaerdi) {
                if (dagNatDelayTimer == 0) dagNatDelayTimer = param.natdagdelay;
            } else {
                dagNatDelayTimer = 0;
                lastLuxOver = false;
            }
            if (dagNatDelayTimer > 0) {
                dagNatDelayTimer--;
                if (dagNatDelayTimer == 0) setNataktiv(false);
            }
        }
    }

    // ---------- Astro nat/dag ----------
    // SIMPEL: astro-nat = nataktiv true ALTID
    //         astro-dag = lux styrer frit via updateLuxNat()
    void updateAstroMode(float lux, time_t ntpTid) {
        int sunriseMin = 0, sunsetMin = 0;
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
    LysAutomatik(LysParam& p, dimmerfunktion* d) : param(p), dimmer(d) {}

    void initFromNow(float lux, time_t ntpTid) {
        currentState = OFF;
        slukActiveret = false;
        timerA = timerC = timerE = 0;
        resetLuxTimers();

        if (param.styringsvalg == "Astro" && param.astroEnabled) {
            int sunriseMin = 0, sunsetMin = 0;
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
            dimmer->sluk();
            currentState = OFF;
            return;
        }

        if (isSegmentMode()) {
            int endSec = 0;
            bool wantA = segmentWantAAndEnd(ntpTid, endSec);
            if (wantA) {
                currentState = TIMER_A;
                setTimerAToEnd(ntpTid, endSec);
                dimmer->setlysiprocentSoft(param.pwmA);
            } else {
                currentState = NIGHT_GLOW;
                dimmer->setlysiprocentSoft(param.pwmG);
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
            switch (currentState) {
                case TIMER_A:    dimmer->setlysiprocentSoft(param.pwmA); break;
                case TIMER_C:    dimmer->setlysiprocentSoft(param.pwmC); break;
                case TIMER_E:    dimmer->setlysiprocentSoft(param.pwmE); break;
                case NIGHT_GLOW: dimmer->setlysiprocentSoft(param.pwmG); break;
                default: break;
            }
        } else {
            slukActiveret = false;
        }

        // 3) Hovedlogik
        if (nataktiv) {
            if (pirEvent) startC();

            if (isSegmentMode()) {
                int endSec = 0;
                bool wantA = segmentWantAAndEnd(ntpTid, endSec);

                if (currentState == OFF) {
                    if (wantA) {
                        currentState = TIMER_A;
                        setTimerAToEnd(ntpTid, endSec);
                        dimmer->setlysiprocentSoft(param.pwmA);
                    } else {
                        currentState = NIGHT_GLOW;
                        dimmer->setlysiprocentSoft(param.pwmG);
                    }
                } else if (currentState == TIMER_A) {
                    if (!wantA) {
                        currentState = NIGHT_GLOW;
                        dimmer->setlysiprocentSoft(param.pwmG);
                    } else {
                        setTimerAToEnd(ntpTid, endSec);
                    }
                } else if (currentState == NIGHT_GLOW) {
                    if (wantA) {
                        currentState = TIMER_A;
                        setTimerAToEnd(ntpTid, endSec);
                        dimmer->setlysiprocentSoft(param.pwmA);
                    }
                }
            } else {
                if (currentState == OFF) startA(ntpTid);
            }
        } else {
            if (currentState != OFF) {
                dimmer->sluk();
                currentState = OFF;
            }
        }

        // 4) Countdown
        switch (currentState) {
            case TIMER_A:
                if (timerA > 0) timerA--;
                if (timerA <= 0) {
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
                    if (isSegmentMode()) {
                        int endSec = 0;
                        bool wantA = segmentWantAAndEnd(ntpTid, endSec);
                        if (wantA) {
                            currentState = TIMER_A;
                            setTimerAToEnd(ntpTid, endSec);
                            dimmer->setlysiprocentSoft(param.pwmA);
                        } else {
                            currentState = NIGHT_GLOW;
                            dimmer->setlysiprocentSoft(param.pwmG);
                        }
                    } else {
                        if (timerA > 0) resumeA();
                        else {
                            currentState = NIGHT_GLOW;
                            dimmer->setlysiprocentSoft(param.pwmG);
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
        currentState = TIMER_A;
        if (isSegmentMode()) {
            int endSec = 0;
            (void)segmentWantAAndEnd(ntpTid, endSec);
            setTimerAToEnd(ntpTid, endSec);
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
