#pragma once
/**
 * @file AstroSun.h
 * @brief Beregning af solopgang og solnedgang ud fra dato og GPS-koordinater.
 *
 * Baseret på NOAA/Meeus simplified sunrise/sunset equation.
 * Returnerer tidspunkter i minutter fra midnat (lokal tid inkl. CET/CEST).
 * Cache: beregnes kun én gang per dato i LysAutomatik.
 */

#include <cmath>
#include <ctime>
#include <Arduino.h>

/** Resultat fra astro-beregning: solopgang og solnedgang i minutter fra midnat. */
struct AstroTimes {
    int sunriseMin = -1;
    int sunsetMin  = -1;
    bool valid() const { return sunriseMin >= 0 && sunsetMin >= 0; }
};

class AstroSun {
public:
    /**
     * @brief Beregn solopgang/solnedgang i lokal tid.
     * @param year  Årstal (fx 2026).
     * @param month Måned (1–12).
     * @param day   Dag (1–31).
     * @param latDeg Latitude i grader (N positiv).
     * @param lonDeg Longitude i grader (E positiv).
     * @return AstroTimes med sunrise/sunset i minutter fra midnat (lokal tid).
     */
    static AstroTimes computeLocalTimes(int year, int month, int day, float latDeg, float lonDeg) {
        AstroTimes out;

        int N = dayOfYear(year, month, day);
        const double zenith = 90.833;

        double sunriseUTC = calcSunTimeUTC(true,  N, latDeg, lonDeg, zenith);
        double sunsetUTC  = calcSunTimeUTC(false, N, latDeg, lonDeg, zenith);
        if (!isfinite(sunriseUTC) || !isfinite(sunsetUTC)) return out;

        long tzOffsetSec = localUtcOffsetSeconds(year, month, day);

        int sunriseLocalMin = wrapMin((int)lround(sunriseUTC + (double)tzOffsetSec / 60.0));
        int sunsetLocalMin  = wrapMin((int)lround(sunsetUTC  + (double)tzOffsetSec / 60.0));

        out.sunriseMin = sunriseLocalMin;
        out.sunsetMin  = sunsetLocalMin;
        return out;
    }

private:
    /** Wrap minutter til 0..1439 intervallet. */
    static int wrapMin(int m) {
        while (m < 0) m += 1440;
        while (m >= 1440) m -= 1440;
        return m;
    }

    /** Beregn lokal UTC-offset i sekunder for en given dato (håndterer DST via TZ). */
    static long localUtcOffsetSeconds(int year, int month, int day) {
        tm localNoonTm = {};
        localNoonTm.tm_year = year - 1900;
        localNoonTm.tm_mon  = month - 1;
        localNoonTm.tm_mday = day;
        localNoonTm.tm_hour = 12;
        localNoonTm.tm_min  = 0;
        localNoonTm.tm_sec  = 0;
        localNoonTm.tm_isdst = -1;

        time_t localNoon = mktime(&localNoonTm);

        tm utcBroken;
        gmtime_r(&localNoon, &utcBroken);

        tm utcAsLocal = utcBroken;
        utcAsLocal.tm_isdst = -1;
        time_t utcInterpretedAsLocal = mktime(&utcAsLocal);

        return (long)(localNoon - utcInterpretedAsLocal);
    }

    static bool isLeap(int y) {
        return (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
    }

    /** Beregn dag-nummer i året (1..365/366). */
    static int dayOfYear(int y, int m, int d) {
        static const int cum[12] = {0,31,59,90,120,151,181,212,243,273,304,334};
        int N = cum[m-1] + d;
        if (m > 2 && isLeap(y)) N += 1;
        return N;
    }

    static double degToRad(double d) { return d * M_PI / 180.0; }
    static double radToDeg(double r) { return r * 180.0 / M_PI; }

    /**
     * @brief NOAA simplified sunrise/sunset beregning.
     * @param isSunrise true = solopgang, false = solnedgang.
     * @param N Dag-nummer i året.
     * @param latDeg Latitude (grader).
     * @param lonDeg Longitude (grader).
     * @param zenithDeg Zenith-vinkel (typisk 90.833°).
     * @return Tidspunkt i minutter fra midnat UTC, eller NAN ved polar nat/dag.
     */
    static double calcSunTimeUTC(bool isSunrise, int N, double latDeg, double lonDeg, double zenithDeg) {
        double lngHour = lonDeg / 15.0;

        double t = isSunrise
            ? (double)N + ((6.0  - lngHour) / 24.0)
            : (double)N + ((18.0 - lngHour) / 24.0);

        double M = (0.9856 * t) - 3.289;

        double L = M + (1.916 * sin(degToRad(M))) + (0.020 * sin(2 * degToRad(M))) + 282.634;
        L = fmod(L, 360.0);
        if (L < 0) L += 360.0;

        double RA = radToDeg(atan(0.91764 * tan(degToRad(L))));
        RA = fmod(RA, 360.0);
        if (RA < 0) RA += 360.0;

        double Lquadrant  = floor(L / 90.0) * 90.0;
        double RAquadrant = floor(RA / 90.0) * 90.0;
        RA = RA + (Lquadrant - RAquadrant);
        RA /= 15.0;

        double sinDec = 0.39782 * sin(degToRad(L));
        double cosDec = cos(asin(sinDec));

        double cosH = (cos(degToRad(zenithDeg)) - (sinDec * sin(degToRad(latDeg))))
                      / (cosDec * cos(degToRad(latDeg)));

        if (cosH > 1.0)  return NAN;   // Polar nat
        if (cosH < -1.0) return NAN;   // Midnatssol

        double H = isSunrise ? (360.0 - radToDeg(acos(cosH))) : radToDeg(acos(cosH));
        H /= 15.0;

        double T = H + RA - (0.06571 * t) - 6.622;
        double UT = T - lngHour;
        UT = fmod(UT, 24.0);
        if (UT < 0) UT += 24.0;

        return UT * 60.0;
    }
};
