#pragma once
/**
 * @file mitjason.h
 * @brief JSON load/save af WiFi-konfiguration og LysParam til SD-kort.
 *
 * Håndterer:
 *   wifi.json    – SSID, password, kontrollernavn.
 *   Default.json – Alle automatik/dimmer/segment/astro parametre.
 *
 * styringsvalg er bagudkompatibel: accepterer både string ("Tid"/"Klokken"/"Astro")
 * og bool (true=Klokken, false=Tid) fra ældre JSON-filer.
 */

#include <ArduinoJson.h>
#include <FS.h>
#include <SdFat.h>
#include <cstring>

#include "LysParam.h"

class MitJsonWiFi {
public:
    const char* default_ssid = "MySSid";
    const char* default_password = "myPass";
    const char* default_kontrollernavn = "controller";

    char ssid[33];
    char password[65];
    char kontrollernavn[33];

    MitJsonWiFi() {
        strncpy(ssid, default_ssid, sizeof(ssid));
        ssid[sizeof(ssid) - 1] = '\0';
        strncpy(password, default_password, sizeof(password));
        password[sizeof(password) - 1] = '\0';
        strncpy(kontrollernavn, default_kontrollernavn, sizeof(kontrollernavn));
        kontrollernavn[sizeof(kontrollernavn) - 1] = '\0';
    }

    /** Læs WiFi fra SPIFFS/LittleFS. */
    bool loadWiFi(fs::FS& fs, const char* filename) {
        File file = fs.open(filename, "r");
        if (!file) return false;
        bool ok = loadWiFiFromFile(file);
        file.close();
        return ok;
    }

    /** Læs WiFi fra SD/SdFat. */
    bool loadWiFi(SdFat& sd, const char* filename) {
        FsFile file = sd.open(filename, FILE_READ);
        if (!file) return false;
        bool ok = loadWiFiFromFile(file);
        file.close();
        return ok;
    }

private:
    template <typename FileType>
    bool loadWiFiFromFile(FileType& file) {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, file);
        if (err) return false;

        const char* ssidIn = doc["ssid"] | default_ssid;
        const char* passIn = doc["password"] | default_password;
        const char* nameIn = doc["kontrollernavn"] | default_kontrollernavn;

        strncpy(ssid, ssidIn, sizeof(ssid));
        ssid[sizeof(ssid) - 1] = '\0';
        strncpy(password, passIn, sizeof(password));
        password[sizeof(password) - 1] = '\0';
        strncpy(kontrollernavn, nameIn, sizeof(kontrollernavn));
        kontrollernavn[sizeof(kontrollernavn) - 1] = '\0';
        return true;
    }

public:
    /** Indlæs alle parametre fra Default.json til LysParam. */
    bool loadDefault(SdFat& sd, LysParam* param) {
        FsFile file = sd.open("Default.json", FILE_READ);
        if (!file) return false;

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, file);
        file.close();
        if (err) return false;

        JsonObject d = doc["Default"];

        // styringsvalg: string format, bagudkompatibel med bool
        if (d.containsKey("styringsvalg")) {
            JsonVariant v = d["styringsvalg"];
            if (v.is<const char*>()) {
                String mode = v.as<const char*>();
                mode.trim();
                if (mode == "Tid" || mode == "Klokken" || mode == "Astro") {
                    param->styringsvalg = mode;
                } else {
                    param->styringsvalg = "Tid";
                }
            } else if (v.is<bool>()) {
                param->styringsvalg = v.as<bool>() ? "Klokken" : "Tid";
            } else {
                param->styringsvalg = "Tid";
            }
        } else {
            param->styringsvalg = "Tid";
        }

        // Basis
        param->luxstartvaerdi = d["luxstartvaerdi"] | 8.0f;
        param->timerA = d["TimerA"] | 7200L;
        param->timerC = d["TimerC"] | 60L;
        param->timerE = d["TimerE"] | 60L;
        param->pwmA = d["timerApwmvaerdi"] | 45;
        param->pwmC = d["timerCpwmvaerdi"] | 100;
        param->pwmE = d["timerEpwmvaerdi"] | 55;
        param->pwmG = d["timerGpwmvaerdi"] | 0;
        param->natdagdelay = d["natdagdelay"] | 15L;

        // Segment 1
        param->slutKlokkeTimer    = d["slutKlokkeTimer"] | 22;
        param->slutKlokkeMinutter = d["slutKlokkeMinutter"] | 0;

        // Segment 2
        param->seg2Enabled       = d["seg2Enabled"] | false;
        param->seg2StartTimer    = d["seg2StartTimer"] | 5;
        param->seg2StartMinutter = d["seg2StartMinutter"] | 30;
        param->seg2SlutTimer     = d["seg2SlutTimer"] | 7;
        param->seg2SlutMinutter  = d["seg2SlutMinutter"] | 30;
        param->seg2WeekMask      = (uint8_t)(d["seg2WeekMask"] | 127);

        // Segment 3
        param->seg3Enabled       = d["seg3Enabled"] | false;
        param->seg3StartTimer    = d["seg3StartTimer"] | 0;
        param->seg3StartMinutter = d["seg3StartMinutter"] | 0;
        param->seg3SlutTimer     = d["seg3SlutTimer"] | 0;
        param->seg3SlutMinutter  = d["seg3SlutMinutter"] | 0;
        param->seg3WeekMask      = (uint8_t)(d["seg3WeekMask"] | 127);

        // Log
        param->lognataktiv        = d["lognataktiv"] | true;
        param->logpirdetection    = d["logpirdetection"] | true;
        param->aktuelStepfrekvens = d["aktuelStepfrekvens"] | 5;

        // Astro
        param->astroEnabled          = d["astroEnabled"] | false;
        param->astroLat              = d["astroLat"] | param->astroLat;
        param->astroLon              = d["astroLon"] | param->astroLon;
        param->astroSunsetOffsetMin  = d["astroSunsetOffsetMin"] | 0;
        param->astroSunriseOffsetMin = d["astroSunriseOffsetMin"] | 0;
        param->astroLuxEarlyStart    = d["astroLuxEarlyStart"] | true;

        return true;
    }

    /** Gem alle parametre fra LysParam til Default.json. */
    bool saveDefault(SdFat& sd, const LysParam* param) {
        JsonDocument doc;
        JsonObject d = doc["Default"].to<JsonObject>();

        d["styringsvalg"] = param->styringsvalg;
        d["luxstartvaerdi"] = param->luxstartvaerdi;

        d["TimerA"] = param->timerA;
        d["TimerC"] = param->timerC;
        d["TimerE"] = param->timerE;

        d["timerApwmvaerdi"] = param->pwmA;
        d["timerCpwmvaerdi"] = param->pwmC;
        d["timerEpwmvaerdi"] = param->pwmE;
        d["timerGpwmvaerdi"] = param->pwmG;

        d["natdagdelay"] = param->natdagdelay;

        d["slutKlokkeTimer"]    = param->slutKlokkeTimer;
        d["slutKlokkeMinutter"] = param->slutKlokkeMinutter;

        d["seg2Enabled"]       = param->seg2Enabled;
        d["seg2StartTimer"]    = param->seg2StartTimer;
        d["seg2StartMinutter"] = param->seg2StartMinutter;
        d["seg2SlutTimer"]     = param->seg2SlutTimer;
        d["seg2SlutMinutter"]  = param->seg2SlutMinutter;
        d["seg2WeekMask"]      = param->seg2WeekMask;

        d["seg3Enabled"]       = param->seg3Enabled;
        d["seg3StartTimer"]    = param->seg3StartTimer;
        d["seg3StartMinutter"] = param->seg3StartMinutter;
        d["seg3SlutTimer"]     = param->seg3SlutTimer;
        d["seg3SlutMinutter"]  = param->seg3SlutMinutter;
        d["seg3WeekMask"]      = param->seg3WeekMask;

        d["lognataktiv"]        = param->lognataktiv;
        d["logpirdetection"]    = param->logpirdetection;
        d["aktuelStepfrekvens"] = param->aktuelStepfrekvens;

        d["astroEnabled"]          = param->astroEnabled;
        d["astroLat"]              = param->astroLat;
        d["astroLon"]              = param->astroLon;
        d["astroSunsetOffsetMin"]  = param->astroSunsetOffsetMin;
        d["astroSunriseOffsetMin"] = param->astroSunriseOffsetMin;
        d["astroLuxEarlyStart"]    = param->astroLuxEarlyStart;

        FsFile file = sd.open("Default.json", O_WRITE | O_CREAT | O_TRUNC);
        if (!file) return false;
        bool ok = (serializeJsonPretty(doc, file) > 0);
        file.close();
        return ok;
    }
};
