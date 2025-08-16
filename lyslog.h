/**
 * @file lyslog.h
 * @brief SD-logning af nataktivitet, PIR-events og hardware-events.
 *
 * Bruges til at logge events til SD-kortet.
 */
#pragma once

#include <Arduino.h>
#include <SdFat.h>
#include "hardware/rtc.h"

#define NATLOG_FILENAME "/nataktiv.log"
#define PIRLOG_FILENAME "/pir.log"
#define HARDWARELOG_FILENAME "/hardware.log"  // altid aktiv hvis SD er til stede

class LysLog {
public:
    LysLog(SdFat &sd, bool natLogEnabled = true, bool pirLogEnabled = true)
        : sd(sd), natLogEnabled(natLogEnabled), pirLogEnabled(pirLogEnabled) {}

    // Nat-aktivitet (respekterer natLogEnabled)
    void logNatAktiv(bool aktiv) {
        if (!natLogEnabled) return;
        appendToFile(NATLOG_FILENAME, makeTimeLine("NAT_AKTIV_" + String(aktiv ? "ON" : "OFF")));
    }

    // PIR (respekterer pirLogEnabled)
    void logPIR(const String& pirNavn) {
        if (!pirLogEnabled) return;
        appendToFile(PIRLOG_FILENAME, makeTimeLine(pirNavn + "_ACTIVATED"));
    }

    // Hardware-log (ALTID, hvis SD er til stede)
    void logHardware(const String& event) {
        appendToFile(HARDWARELOG_FILENAME, makeTimeLine(event));
    }
    void logWatchdogReset()                { logHardware("WATCHDOG_RESET"); }
    void logI2CReset(const char* bus)      { logHardware(String("I2C_RESET_") + bus); }
    void logWiFiReconnect(const String& ip){ logHardware(String("WIFI_RECONNECT ") + ip); }
    void logBootReboot(const char* reason) { logHardware(String("BOOT_REBOOT ") + reason); }

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
        FsFile file = sd.open(filename, FILE_WRITE); // FILE_WRITE appender
        if (file) {
            file.seekEnd();
            file.print(line);
            file.close();
        }
    }
};
