/**
 * @file mitjason.h
 * @brief Klasse til at læse/skrive WiFi- og LysParam-konfiguration fra/til JSON.
 *        Understøtter både SPIFFS/LittleFS (File) og SdFat (FsFile).
 */
#pragma once
#include <ArduinoJson.h>
#include <FS.h>
#include <SdFat.h>
#include "LysParam.h"

/**
 * @class MitJsonWiFi
 * @brief Håndterer indlæsning og lagring af WiFi- og systemparametre fra SD.
 */
class MitJsonWiFi {
public:
    const char* default_ssid = "SSID";
    const char* default_password = "Password";
    const char* default_kontrollernavn = "controller";
    char ssid[33];
    char password[65];
    char kontrollernavn[33];

    MitJsonWiFi() {
        strncpy(ssid, default_ssid, sizeof(ssid));
        strncpy(password, default_password, sizeof(password));
        strncpy(kontrollernavn, default_kontrollernavn, sizeof(kontrollernavn));
    }

    /**
     * @brief Læs WiFi fra SPIFFS/LittleFS
     */
    bool loadWiFi(fs::FS& fs, const char* filename) {
        File file = fs.open(filename, "r");
        if (!file) return false;
        bool ok = loadWiFiFromFile(file);
        file.close();
        return ok;
    }
    /**
     * @brief Læs WiFi fra SD/FsFile
     */
    // For SdFat/FsFile
    bool loadWiFi(SdFat& sd, const char* filename) {
        FsFile file = sd.open(filename, FILE_READ);
        if (!file) return false;
        bool ok = loadWiFiFromFile(file);
        file.close();
        return ok;
    }

private:
    // Template for both File and FsFile
    template<typename FileType>
    bool loadWiFiFromFile(FileType& file) {
        StaticJsonDocument<256> doc;
        DeserializationError err = deserializeJson(doc, file);
        if (err) return false;
        strlcpy(ssid, doc["ssid"] | default_ssid, sizeof(ssid));
        strlcpy(password, doc["password"] | default_password, sizeof(password));
        strlcpy(kontrollernavn, doc["kontrollernavn"] | default_kontrollernavn, sizeof(kontrollernavn));
        return true;
    }
//strlcpy(param->styringsvalg, d["styringsvalg"].is<const char*>() ? d["styringsvalg"].as<const char*>() : "Klokken", sizeof(param->styringsvalg));

public:
    /**
     * @brief Indlæs LysParam (default) fra SD.
     * @param sd SD-kort reference
     * @param param Pointer til LysParam
     */
bool loadDefault(SdFat& sd, LysParam* param) {
    FsFile file = sd.open("Default.json", FILE_READ);
    if (!file) return false;
    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    if (err) return false;
    JsonObject d = doc["Default"];
    // Styringsvalg som bool
    if (d["styringsvalg"].as<bool>()) {
      param->styringsvalg = "Klokken";
    } else {
      param->styringsvalg = "Tid";
    } 
    param->luxstartvaerdi = d["luxstartvaerdi"] | 8;
    param->timerA = d["TimerA"] | 7200;
    param->timerC = d["TimerC"] | 60;
    param->timerE = d["TimerE"] | 60;
    param->pwmA = d["timerApwmvaerdi"] | 45;
    param->pwmC = d["timerCpwmvaerdi"] | 100;
    param->pwmE = d["timerEpwmvaerdi"] | 55;
    param->pwmG = d["timerGpwmvaerdi"] | 0;
    param->natdagdelay = d["natdagdelay"] | 15;
    param->slutKlokkeTimer = d["slutKlokkeTimer"] | 16;
    param->slutKlokkeMinutter = d["slutKlokkeMinutter"] | 26;
    param->lognataktiv = d["lognataktiv"].as<bool>();
    param->logpirdetection = d["logpirdetection"].as<bool>();
    param->aktuelStepfrekvens = d["aktuelStepfrekvens"] | 5;
    return true;
}
    /**
     * @brief Gem LysParam (default) til SD.
     * @param sd SD-kort reference
     * @param param Pointer til LysParam
     */
bool saveDefault(SdFat& sd, const LysParam* param) {
    StaticJsonDocument<512> doc;
    JsonObject d = doc["Default"].to<JsonObject>();
    d["styringsvalg"] = (param->styringsvalg == "Klokken");
    d["luxstartvaerdi"] = param->luxstartvaerdi;
    d["TimerA"] = param->timerA;
    d["TimerC"] = param->timerC;
    d["TimerE"] = param->timerE;
    d["timerApwmvaerdi"] = param->pwmA;
    d["timerCpwmvaerdi"] = param->pwmC;
    d["timerEpwmvaerdi"] = param->pwmE;
    d["timerGpwmvaerdi"] = param->pwmG;
    d["natdagdelay"] = param->natdagdelay;
    d["slutKlokkeTimer"] = param->slutKlokkeTimer;
    d["slutKlokkeMinutter"] = param->slutKlokkeMinutter;
    d["lognataktiv"] = param->lognataktiv;
    d["logpirdetection"] = param->logpirdetection;
    d["aktuelStepfrekvens"] = param->aktuelStepfrekvens;

    FsFile file = sd.open("Default.json", O_WRITE | O_CREAT | O_TRUNC);
    if (!file) return false;
    bool ok = (serializeJsonPretty(doc, file) > 0);
    file.close();
    return ok;
}

};
