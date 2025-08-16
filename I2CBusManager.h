#pragma once
#include <Arduino.h>
#include <Wire.h>

#ifndef I2C_MANAGER_MAX_RETRIES
#define I2C_MANAGER_MAX_RETRIES 3
#endif

class I2CBusManager {
public:
    I2CBusManager(int sdaPin, int sclPin)
        : _sda(sdaPin), _scl(sclPin) {}

    void setTargetClock(uint32_t fastHz = 400000, uint32_t slowHz = 100000) {
        _fast = fastHz;
        _slow = slowHz;
    }

    // Kald én gang i setup1
    bool init(bool scan = true) {
        if (!recoverBus()) {
            Serial.println(F("[I2C] Bus kunne ikke frigives (SDA lav)"));
        }
        Wire.setSDA(_sda);
        Wire.setSCL(_scl);
        Wire.begin();
        applyClock();
        delay(5);
        if (scan) scanBus();
        return true;
    }

    // Brug ved sensorfejl; returnerer nuværende clock-mode
    uint32_t registerFailure() {
        _failCount++;
        if (_failCount >= I2C_MANAGER_MAX_RETRIES && !_slowMode) {
            Serial.println(F("[I2C] For mange fejl – skifter til slow mode (100 kHz)"));
            _slowMode = true;
            applyClock();
        }
        return WireGetCurrentClock();
    }

    void registerSuccess() {
        if (_failCount > 0) _failCount--;
        // (Valgfrit) automatisk tilbage til fast efter lang tids succes
        if (_slowMode && _failCount == 0 && _successStreak++ > 50) {
            Serial.println(F("[I2C] Stabil igen – tilbage til fast mode"));
            _slowMode = false;
            applyClock();
        }
    }

    void scanBus() {
        Serial.println(F("[I2C] Scan:"));
        uint8_t found = 0;
        for (uint8_t a = 0x08; a < 0x78; a++) {
            Wire.beginTransmission(a);
            if (Wire.endTransmission() == 0) {
                Serial.print(F("  0x"));
                Serial.println(a, HEX);
                found++;
            }
        }
        if (!found) Serial.println(F("  Ingen enheder."));
    }

private:
    int _sda, _scl;
    uint32_t _fast = 400000;
    uint32_t _slow = 100000;
    bool _slowMode = false;
    uint8_t _failCount = 0;
    uint16_t _successStreak = 0;

    bool recoverBus() {
        pinMode(_scl, OUTPUT);
        pinMode(_sda, INPUT_PULLUP);
        if (digitalRead(_sda) == HIGH) return true;
        for (int i = 0; i < 9; i++) {
            digitalWrite(_scl, HIGH);
            delayMicroseconds(8);
            digitalWrite(_scl, LOW);
            delayMicroseconds(8);
            if (digitalRead(_sda) == HIGH) break;
        }
        return (digitalRead(_sda) == HIGH);
    }

    void applyClock() {
        uint32_t clk = _slowMode ? _slow : _fast;
        Wire.setClock(clk);
        Serial.print(F("[I2C] Clock sat til "));
        Serial.print(clk / 1000);
        Serial.println(F(" kHz"));
    }

    // Philhower Wire har ingen getter; vi holder bare state selv
    uint32_t WireGetCurrentClock() const {
        return _slowMode ? _slow : _fast;
    }
};