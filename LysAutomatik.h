#pragma once

#include <Arduino.h>
#include <time.h>

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

    // Lux delay/hysterese (sekunder, fordi update() kaldes 1Hz i dit loop1)
    long natdagdelayTimer = 0;
    long dagNatDelayTimer = 0;
    bool lastLuxOver = true;

    // Astro cache (beregn 1 gang pr dato)
    int cachedY = -1, cachedM = -1, cachedD = -1;
    AstroTimes cachedAstro;

    // Latch for "lux early-start" før solnedgang i Astro-mode
    bool astroEarlyLatched = false;

    static int toSec(int h, int m, int s = 0) { return h * 3600 + m * 60 + s; }
    static int toMin(int h, int m) { return h * 60 + m; }

    static bool inRangeSec(int nowSec, int startSec, int endSec) {
        if (startSec == endSec) return true;            // “altid”
        if (startSec < endSec)  return (nowSec >= startSec && nowSec < endSec);
        return (nowSec >= startSec || nowSec < endSec); // over midnat
    }

    static bool inRangeMin(int nowMin, int startMin, int endMin) {
        if (startMin == endMin) return true;
        if (startMin < endMin)  return (nowMin >= startMin && nowMin < endMin);
        return (nowMin >= startMin || nowMin < endMin); // over midnat
    }

    static int wrapMin(int m) {
        while (m < 0) m += 1440;
        while (m >= 1440) m -= 1440;
        return m;
    }

    static bool toLocalTm(time_t t, tm& out) {
        // localtime_r returnerer typisk &out (ikke nullptr) ved success
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

    // Returnerer sunrise/sunset (minutter fra midnat) med offsets anvendt.
    // Returnerer false hvis astro ikke kan beregnes.
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

    // "Nat-døgn" weekday: efter midnat hører til aftenen før (søndag = mandag morgen osv.)
    static int effectiveNightWday(int wday, int nowSec) {
        if (wday < 0 || wday > 6) return wday;
        if (nowSec < toSec(12, 0, 0)) return (wday + 6) % 7; // før 12:00 => i går
        return wday;
    }

    // ---------- Segment beslutning (Klokken-mode / generisk) ----------
    bool klokWantAAndEnd(time_t ntpTid, int& outEndSec) {
        tm timeinfo;
        if (!toLocalTm(ntpTid, timeinfo)) {
            // fail-open: behold lys som “nat” og sæt en “fornuftig” slut
            outEndSec = toSec(param.slutKlokkeTimer, param.slutKlokkeMinutter, 0);
            return true;
        }

        int nowSec = toSec(timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

        int wday = timeinfo.tm_wday; // 0=Sun..6=Sat
        auto dayAllowed = [&](uint8_t mask) -> bool {
            if (wday < 0 || wday > 6) return true; // fail-open
            return (mask & (1u << wday)) != 0;
        };

        const int seg1End = toSec(param.slutKlokkeTimer, param.slutKlokkeMinutter, 0);

        bool wantA;
        // Hvis seg1End < 12:00 => tolkes som "sluk næste dag om morgenen"
        if (seg1End < toSec(12, 0, 0)) {
            wantA = (nowSec >= toSec(12, 0, 0)) || (nowSec < seg1End);
        } else {
            wantA = (nowSec < seg1End);
        }

        if (param.seg2Enabled && dayAllowed(param.seg2WeekMask)) {
            int s2Start = toSec(param.seg2StartTimer, param.seg2StartMinutter, 0);
            int s2End   = toSec(param.seg2SlutTimer,  param.seg2SlutMinutter,  0);
            if (inRangeSec(nowSec, s2Start, s2End)) {
                outEndSec = s2End;
                return true;
            }
        }

        if (param.seg3Enabled && dayAllowed(param.seg3WeekMask)) {
            int s3Start = toSec(param.seg3StartTimer, param.seg3StartMinutter, 0);
            int s3End   = toSec(param.seg3SlutTimer,  param.seg3SlutMinutter,  0);
            if (inRangeSec(nowSec, s3Start, s3End)) {
                outEndSec = s3End;
                return true;
            }
        }

        outEndSec = seg1End;
        return wantA;
    }

    // ---------- Segment beslutning (Astro-mode) ----------
    // Segment 1 i Astro = A fra sunset -> seg1End (fx 22:00), ellers G.
    // Segment 2/3 kan override med deres intervaller. Weekmask tolkes som "nat-ugedag".
    bool astroWantAAndEnd(time_t ntpTid, int& outEndSec) {
        tm timeinfo;
        if (!toLocalTm(ntpTid, timeinfo)) {
            outEndSec = toSec(param.slutKlokkeTimer, param.slutKlokkeMinutter, 0);
            return true; // fail-open
        }

        int nowSec = toSec(timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

        // Weekmask: nat-logik
        int wdayEff = effectiveNightWday(timeinfo.tm_wday, nowSec);
        auto dayAllowed = [&](uint8_t mask) -> bool {
            if (wdayEff < 0 || wdayEff > 6) return true; // fail-open
            return (mask & (1u << wdayEff)) != 0;
        };

        int sunriseMin = 0, sunsetMin = 0;
        if (!getAstroRiseSetMin(ntpTid, sunriseMin, sunsetMin)) {
            // Hvis astro fejler, fald tilbage til Klokken-logik
            return klokWantAAndEnd(ntpTid, outEndSec);
        }

        const int sunsetSec = sunsetMin * 60;
        const int seg1End   = toSec(param.slutKlokkeTimer, param.slutKlokkeMinutter, 0);

        bool wantA = inRangeSec(nowSec, sunsetSec, seg1End);

        if (param.seg2Enabled && dayAllowed(param.seg2WeekMask)) {
            int s2Start = toSec(param.seg2StartTimer, param.seg2StartMinutter, 0);
            int s2End   = toSec(param.seg2SlutTimer,  param.seg2SlutMinutter,  0);
            if (inRangeSec(nowSec, s2Start, s2End)) {
                outEndSec = s2End;
                return true;
            }
        }

        if (param.seg3Enabled && dayAllowed(param.seg3WeekMask)) {
            int s3Start = toSec(param.seg3StartTimer, param.seg3StartMinutter, 0);
            int s3End   = toSec(param.seg3SlutTimer,  param.seg3SlutMinutter,  0);
            if (inRangeSec(nowSec, s3Start, s3End)) {
                outEndSec = s3End;
                return true;
            }
        }

        outEndSec = seg1End;
        return wantA;
    }

    bool segmentWantAAndEnd(time_t ntpTid, int& outEndSec) {
        if (param.styringsvalg == "Astro") return astroWantAAndEnd(ntpTid, outEndSec);
        return klokWantAAndEnd(ntpTid, outEndSec);
    }

    void setTimerAToEnd(time_t ntpTid, int endSec) {
        tm timeinfo;
        if (!toLocalTm(ntpTid, timeinfo)) return;

        int nowSec = toSec(timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

        timerA = endSec - nowSec;
        if (timerA < 0) timerA += 24L * 3600L;
        if (timerA > 12L * 3600L) timerA = 0;   // <-- safety clamp
        if (timerA < 0) timerA = 0;
    }

    // ---------- Lux nat/dag (original) ----------
    void updateLuxNat(float lux) {
        if (!nataktiv) {
            dagNatDelayTimer = 0;

            if (lux < param.luxstartvaerdi && lastLuxOver) {
                natdagdelayTimer = param.natdagdelay;
                lastLuxOver = false;
            }
            if (lux >= param.luxstartvaerdi) lastLuxOver = true;

            if (natdagdelayTimer > 0) {
                natdagdelayTimer--;
                if (natdagdelayTimer == 0) {
                    setNataktiv(true);
                }
            }
        }

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
                if (dagNatDelayTimer == 0) {
                    setNataktiv(false);
                }
            }
        }
    }

    // ---------- Astro-mode (Valg A) ----------
    void updateAstroMode(float lux, time_t ntpTid) {
        int sunriseMin = 0, sunsetMin = 0;
        if (!getAstroRiseSetMin(ntpTid, sunriseMin, sunsetMin)) {
            // Fail-safe: hvis astro ikke kan beregnes, brug lux-mode
            astroEarlyLatched = false;
            updateLuxNat(lux);
            return;
        }

        tm ti;
        if (!toLocalTm(ntpTid, ti)) return;

        int nowMin = toMin(ti.tm_hour, ti.tm_min);

        bool astroNight = inRangeMin(nowMin, sunsetMin, sunriseMin); // sunset->sunrise
        bool astroDay   = inRangeMin(nowMin, sunriseMin, sunsetMin); // sunrise->sunset

        if (astroNight) {
            // Rigtig nat: nataktiv = true (lux må ikke slukke om natten i Astro-mode)
            astroEarlyLatched = false;
            setNataktiv(true);
            resetLuxTimers();
            return;
        }

        if (astroDay) {
            // Astro-dag: lux fallback må tænde/slukke (A)
            astroEarlyLatched = false;
            updateLuxNat(lux);
            return;
        }

        // "Dusk"/edge case (pga wrap/offset): ikke nat og ikke dag.
        if (astroEarlyLatched) {
            setNataktiv(true);
            resetLuxTimers();
            return;
        }

        if (!param.astroLuxEarlyStart) {
            setNataktiv(false);
            resetLuxTimers();
            return;
        }

        // Lux early-start (kun tænd)
        if (!nataktiv) {
            if (lux < param.luxstartvaerdi && lastLuxOver) {
                natdagdelayTimer = param.natdagdelay;
                lastLuxOver = false;
            }
            if (lux >= param.luxstartvaerdi) lastLuxOver = true;

            if (natdagdelayTimer > 0) {
                natdagdelayTimer--;
                if (natdagdelayTimer == 0) {
                    astroEarlyLatched = true;
                    setNataktiv(true);
                }
            }
        } else {
            astroEarlyLatched = true;
            natdagdelayTimer = 0;
        }
    }

public:
    LysAutomatik(LysParam& p, dimmerfunktion* d) : param(p), dimmer(d) {}

public:
    void initFromNow(float lux, time_t ntpTid) {
        // Nulstil intern state
        currentState = OFF;
        slukActiveret = false;
        timerA = timerC = timerE = 0;
        astroEarlyLatched = false;
        resetLuxTimers();

        // 1) Fastlæg nataktiv "instant"
        if (param.styringsvalg == "Astro" && param.astroEnabled) {
            int sunriseMin = 0, sunsetMin = 0;
            if (getAstroRiseSetMin(ntpTid, sunriseMin, sunsetMin)) {
                tm ti;
                if (toLocalTm(ntpTid, ti)) {
                    int nowMin = toMin(ti.tm_hour, ti.tm_min);
                    bool astroNight = inRangeMin(nowMin, sunsetMin, sunriseMin);
                    bool astroDay   = inRangeMin(nowMin, sunriseMin, sunsetMin);

                    if (astroNight) {
                        setNataktiv(true);
                    } else if (astroDay) {
                        setNataktiv(lux < param.luxstartvaerdi);
                        lastLuxOver = (lux >= param.luxstartvaerdi);
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

        // 2) Sæt output/state straks
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
            startA(ntpTid); // Tid-mode
        }
    }

    void update(float lux, bool pirEvent, time_t ntpTid) {
        // 1) Nat/dag logik
        if (param.styringsvalg == "Astro" && param.astroEnabled) {
            updateAstroMode(lux, ntpTid);
        } else {
            astroEarlyLatched = false;
            updateLuxNat(lux);
        }

        // 2) Return from forceOff
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

        // 4) State machine
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
