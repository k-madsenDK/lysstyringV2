/**
 * @file lyslog.h
 * @brief SD-logning af nataktivitet og PIR-events.
 *
 * Bruges til at logge events til SD-kortet. Aktiveres/deaktiveres via parametre.
 */
#pragma once

#include <Arduino.h>
#include <SdFat.h>
#include "hardware/rtc.h"

#define NATLOG_FILENAME "/nataktiv.log"
#define PIRLOG_FILENAME "/pir.log"
#define HWLOG_FILENAME  "/hardware.log"   // NYT: system/hardware log

/**
 * @class LysLog
 * @brief Logger nataktivitet og PIR-events til SD-kort.
 */
class LysLog {
public:
    /**
     * @param sd Reference til SdFat-objekt
     * @param natLogEnabled Aktiver nataktiv-logning
     * @param pirLogEnabled Aktiver PIR-logning
     */
    LysLog(SdFat &sd, bool natLogEnabled = true, bool pirLogEnabled = true)
        : sd(sd), natLogEnabled(natLogEnabled), pirLogEnabled(pirLogEnabled) {}

    void logNatAktiv(bool aktiv) {
        if (!natLogEnabled) return;
        String line = makeTimeLine("NAT_AKTIV_" + String(aktiv ? "ON" : "OFF"));
        appendToFile(NATLOG_FILENAME, line);
    }

    void logPIR(const String& pirNavn) {
        if (!pirLogEnabled) return;
        String line = makeTimeLine(pirNavn + "_ACTIVATED");
        appendToFile(PIRLOG_FILENAME, line);
    }

    // NYT: generel hardware/system-log (WATCHDOG_RESET, WIFI_RECONNECT, osv.)
    void logHardware(const String& msg) {
        String line = makeTimeLine(msg);
        appendToFile(HWLOG_FILENAME, line);
    }

    void setLogNatAktiv(bool enabled) { natLogEnabled = enabled; }
    void setLogPIRAktiv(bool enabled) { pirLogEnabled = enabled; }

private:
    SdFat &sd;
    bool natLogEnabled;
    bool pirLogEnabled;

    String makeTimeLine(const String& event) {
        datetime_t t;
        rtc_get_datetime(&t);
        char tidBuf[32];
        snprintf(tidBuf, sizeof(tidBuf), "%04d-%02d-%02d %02d:%02d:%02d",
                 t.year, t.month, t.day, t.hour, t.min, t.sec);
        return "[" + String(tidBuf) + "] " + event + "\n";
    }

    void appendToFile(const char *filename, const String& line) {
        FsFile file = sd.open(filename, FILE_WRITE); // FILE_WRITE appender!
        if (file) {
            file.seekEnd();
            file.print(line);
            file.close();
        }
    }
};
