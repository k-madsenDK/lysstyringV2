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
    /**
     * @brief Log nataktivitet on/off.
     * @param aktiv true = ON, false = OFF
     */
    void logNatAktiv(bool aktiv) {
        if (!natLogEnabled) return;
        String line = makeTimeLine("NAT_AKTIV_" + String(aktiv ? "ON" : "OFF"));
        appendToFile(NATLOG_FILENAME, line);
    }
    /**
     * @brief Log en PIR-event.
     * @param pirNavn Navn på PIR (fx "pir 1")
     */
    void logPIR(const String& pirNavn) {
        if (!pirLogEnabled) return;
        String line = makeTimeLine(pirNavn + "_ACTIVATED");
        appendToFile(PIRLOG_FILENAME, line);
    }
    /**
     * @brief Aktiver/deaktiver nataktiv-logning.
     */
    void setLogNatAktiv(bool enabled) { natLogEnabled = enabled; }
    /**
     * @brief Aktiver/deaktiver PIR-logning.
     */    
    void setLogPIRAktiv(bool enabled) { pirLogEnabled = enabled; }

private:
    SdFat &sd;
    bool natLogEnabled;
    bool pirLogEnabled;
    /**
     * @brief Lav tidsstempel-linje.
     * @param event Event-beskrivelse
     * @return Linje til log
     */
    String makeTimeLine(const String& event) {
        datetime_t t;
        rtc_get_datetime(&t);
        char tidBuf[32];
        snprintf(tidBuf, sizeof(tidBuf), "%04d-%02d-%02d %02d:%02d:%02d",
                 t.year, t.month, t.day, t.hour, t.min, t.sec);
        return "[" + String(tidBuf) + "] " + event + "\n";
    }
    /**
     * @brief Appender linje til fil på SD-kort.
     */
    void appendToFile(const char *filename, const String& line) {
        FsFile file = sd.open(filename, FILE_WRITE); // FILE_WRITE appender!
        if (file) {
            file.seekEnd(); // Gå til slutningen af filen
            file.print(line);
            file.close();
        }
    }
};
