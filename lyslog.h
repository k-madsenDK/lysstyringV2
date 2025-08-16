/**
 * @file lyslog.h
 * @brief SD-logning af nataktivitet, PIR-events og hardware-events.
 *
 * Bruges til at logge events til SD-kortet. Aktiveres/deaktiveres via parametre.
 */
#pragma once

#include <Arduino.h>
#include <SdFat.h>
#include "hardware/rtc.h"

#define NATLOG_FILENAME      "/nataktiv.log"
#define PIRLOG_FILENAME      "/pir.log"
#define HARDWARELOG_FILENAME "/hardware.log"

/**
 * @class LysLog
 * @brief Logger nataktivitet, PIR og hardware-events til SD-kort.
 */
class LysLog {
public:
    LysLog(SdFat &sd, bool natLogEnabled = true, bool pirLogEnabled = true)
        : sd(sd), natLogEnabled(natLogEnabled), pirLogEnabled(pirLogEnabled) {}

    // Nataktivitet on/off
    void logNatAktiv(bool aktiv) {
        if (!natLogEnabled) return;
        String line = makeTimeLine("NAT_AKTIV_" + String(aktiv ? "ON" : "OFF"));
        appendToFile(NATLOG_FILENAME, line);
    }

    // PIR-event
    void logPIR(const String& pirNavn) {
        if (!pirLogEnabled) return;
        String line = makeTimeLine(pirNavn + "_ACTIVATED");
        appendToFile(PIRLOG_FILENAME, line);
    }

    // Hardware-events (WIFI_RECONNECT, WATCHDOG_RESET, BOOT_REBOOT ...)
    void logHardware(const String& text) {
        String line = makeTimeLine(text);
        appendToFile(HARDWARELOG_FILENAME, line);
    }

    void setLogNatAktiv(bool enabled) { natLogEnabled = enabled; }
    void setLogPIRAktiv(bool enabled) { pirLogEnabled = enabled; }

private:
    SdFat &sd;
    bool natLogEnabled;
    bool pirLogEnabled;

    // Tidslinje med UNSYNCED fallback når RTC ikke er valid
    String makeTimeLine(const String& event) {
        datetime_t t;
        rtc_get_datetime(&t);
        if (!isValidRTC(t)) {
            return "[UNSYNCED] " + event + "\n";
        }
        char tidBuf[32];
        snprintf(tidBuf, sizeof(tidBuf), "%04d-%02d-%02d %02d:%02d:%02d",
                 t.year, t.month, t.day, t.hour, t.min, t.sec);
        return "[" + String(tidBuf) + "] " + event + "\n";
    }

    bool isValidRTC(const datetime_t& x) {
        if (x.year < 2023 || x.year > 2099) return false;
        if (x.month < 1 || x.month > 12) return false;
        if (x.day < 1 || x.day > 31) return false;
        if (x.hour > 23 || x.min > 59 || x.sec > 59) return false;
        return true;
    }

    void appendToFile(const char *filename, const String& line) {
        FsFile file = sd.open(filename, FILE_WRITE); // FILE_WRITE appender
        if (file) {
            file.seekEnd();
            file.print(line);
            file.close();
        }
    }
};