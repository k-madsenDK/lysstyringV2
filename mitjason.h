/**
 * @file mitjason.h
 * @brief Klasse til at læse/skrive WiFi- og LysParam-konfiguration fra/til JSON.
 *        Understøtter både SPIFFS/LittleFS (File) og SdFat (FsFile).
 *
 * Note: Migreret til ArduinoJson v7 API (JsonDocument) for at undgå deprecation-warnings.
 */
#pragma once
#include <ArduinoJson.h>
#include <FS.h>
#include <SdFat.h>
#include "LysParam.h"

class MitJsonWiFi {
public:
    const char* default_ssid = "MySSid";
    const char* default_password = "myPass";
    const char* default_kontrollernavn = "kontroller";

    char ssid[33];
    char password[65];
    char kontrollernavn[33];

    MitJsonWiFi() {
        strncpy(ssid, default_ssid, sizeof(ssid));
        ssid[sizeof(ssid)-1] = '\0';
        strncpy(password, default_password, sizeof(password));
        password[sizeof(password)-1] = '\0';
        strncpy(kontrollernavn, default_kontrollernavn, sizeof(kontrollernavn));
        kontrollernavn[sizeof(kontrollernavn)-1] = '\0';
    }

    // Læs WiFi fra SPIFFS/LittleFS
    bool loadWiFi(fs::FS& fs, const char* filename) {
        File file = fs.open(filename, "r");
        if (!file) return false;
        bool ok = loadWiFiFromFile(file);
        file.close();
        return ok;
    }

    // Læs WiFi fra SD/FsFile
    bool loadWiFi(SdFat& sd, const char* filename) {
        FsFile file = sd.open(filename, FILE_READ);
        if (!file) return false;
        bool ok = loadWiFiFromFile(file);
        file.close();
        return ok;
    }

private:
    // Template der virker for både File og FsFile
    template<typename FileType>
    bool loadWiFiFromFile(FileType& file) {
        JsonDocument doc;  // ArduinoJson v7 dynamisk dokument
        DeserializationError err = deserializeJson(doc, file);
        if (err) return false;

        const char* ssidIn  = doc["ssid"] | default_ssid;
        const char* passIn  = doc["password"] | default_password;
        const char* nameIn  = doc["kontrollernavn"] | default_kontrollernavn;

        strncpy(ssid, ssidIn, sizeof(ssid)); ssid[sizeof(ssid)-1] = '\0';
        strncpy(password, passIn, sizeof(password)); password[sizeof(password)-1] = '\0';
        strncpy(kontrollernavn, nameIn, sizeof(kontrollernavn)); kontrollernavn[sizeof(kontrollernavn)-1] = '\0';
        return true;
    }

public:
    // Indlæs LysParam (default) fra SD
    bool loadDefault(SdFat& sd, LysParam* param) {
        FsFile file = sd.open("Default.json", FILE_READ);
        if (!file) return false;

        JsonDocument doc;  // v7
        DeserializationError err = deserializeJson(doc, file);
        file.close();
        if (err) return false;

        JsonObject d = doc["Default"];

        // styringsvalg gemmes som bool i fil (true = "Klokken", false = "Tid")
        if (d.containsKey("styringsvalg")) {
            bool klok = d["styringsvalg"].as<bool>();
            param->styringsvalg = klok ? "Klokken" : "Tid";
        } else {
            param->styringsvalg = "Tid";
        }

        param->luxstartvaerdi      = d["luxstartvaerdi"]       | 8;
        param->timerA              = d["TimerA"]               | 7200;
        param->timerC              = d["TimerC"]               | 60;
        param->timerE              = d["TimerE"]               | 60;
        param->pwmA                = d["timerApwmvaerdi"]      | 45;
        param->pwmC                = d["timerCpwmvaerdi"]      | 100;
        param->pwmE                = d["timerEpwmvaerdi"]      | 55;
        param->pwmG                = d["timerGpwmvaerdi"]      | 0;
        param->natdagdelay         = d["natdagdelay"]          | 15;
        param->slutKlokkeTimer     = d["slutKlokkeTimer"]      | 16;
        param->slutKlokkeMinutter  = d["slutKlokkeMinutter"]   | 26;
        param->lognataktiv         = d["lognataktiv"].as<bool>();
        param->logpirdetection     = d["logpirdetection"].as<bool>();
        param->aktuelStepfrekvens  = d["aktuelStepfrekvens"]   | 5;

        return true;
    }

    // Gem LysParam (default) til SD
    bool saveDefault(SdFat& sd, const LysParam* param) {
        JsonDocument doc;  // v7
        JsonObject d = doc["Default"].to<JsonObject>();

        d["styringsvalg"]       = (param->styringsvalg == "Klokken");
        d["luxstartvaerdi"]     = param->luxstartvaerdi;
        d["TimerA"]             = param->timerA;
        d["TimerC"]             = param->timerC;
        d["TimerE"]             = param->timerE;
        d["timerApwmvaerdi"]    = param->pwmA;
        d["timerCpwmvaerdi"]    = param->pwmC;
        d["timerEpwmvaerdi"]    = param->pwmE;
        d["timerGpwmvaerdi"]    = param->pwmG;
        d["natdagdelay"]        = param->natdagdelay;
        d["slutKlokkeTimer"]    = param->slutKlokkeTimer;
        d["slutKlokkeMinutter"] = param->slutKlokkeMinutter;
        d["lognataktiv"]        = param->lognataktiv;
        d["logpirdetection"]    = param->logpirdetection;
        d["aktuelStepfrekvens"] = param->aktuelStepfrekvens;

        FsFile file = sd.open("Default.json", O_WRITE | O_CREAT | O_TRUNC);
        if (!file) return false;
        bool ok = (serializeJsonPretty(doc, file) > 0);
        file.close();
        return ok;
    }
};
