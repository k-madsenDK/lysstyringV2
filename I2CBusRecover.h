#pragma once
/**
 * @file I2CBusRecover.h
 * @brief I2C bus recovery for RP2040.
 *
 * Toggler SCL 9 gange for at frigive en hængt I2C-slave, efterfulgt af en STOP-condition.
 * Inkluderer en simpel bus-scanner til debug-output på Serial.
 *
 * Brug: Kald recover() FØR Wire.begin().
 */

#include <Wire.h>

namespace I2CBusRecover {

    /**
     * @brief Toggle SCL op til 9 gange for at frigive en hængt slave.
     * @param sclPin GPIO for SCL.
     * @param sdaPin GPIO for SDA.
     * @param pullupsInternal Brug interne pull-ups på SDA (default: true).
     * @return true hvis SDA er høj (bus frigivet) efter recovery.
     */
    inline bool recover(int sclPin, int sdaPin, bool pullupsInternal = true) {
        pinMode(sclPin, OUTPUT);
        if (pullupsInternal) {
            pinMode(sdaPin, INPUT_PULLUP);
        } else {
            pinMode(sdaPin, INPUT);
        }

        // Bus allerede OK
        if (digitalRead(sdaPin) == HIGH) {
            return true;
        }

        // Toggle SCL op til 9 gange
        for (int i = 0; i < 9; i++) {
            digitalWrite(sclPin, HIGH);
            delayMicroseconds(8);
            digitalWrite(sclPin, LOW);
            delayMicroseconds(8);
            if (digitalRead(sdaPin) == HIGH) {
                break;
            }
        }

        // Generer STOP-condition hvis SDA er frigivet
        if (digitalRead(sdaPin) == HIGH) {
            pinMode(sdaPin, OUTPUT);
            digitalWrite(sdaPin, LOW);
            delayMicroseconds(8);
            digitalWrite(sclPin, HIGH);
            delayMicroseconds(8);
            pinMode(sdaPin, INPUT_PULLUP);  // SDA goes high = STOP
            delayMicroseconds(8);
        }
        return false;
    }

    /**
     * @brief Scan I2C-bus for enheder (adresse 0x08..0x77).
     * @param w TwoWire instans at scanne.
     * @param ser Serial instans til output.
     */
    inline void scanTwoWire(TwoWire &w = Wire, HardwareSerial &ser = Serial) {
        ser.println(F("[I2C] Scanner start"));
        uint8_t found = 0;
        for (uint8_t a = 0x08; a < 0x78; a++) {
            w.beginTransmission(a);
            if (w.endTransmission() == 0) {
                ser.print(F("  Fundet adr 0x"));
                ser.println(a, HEX);
                found++;
            }
        }
        if (!found) ser.println(F("  Ingen enheder fundet."));
        ser.println(F("[I2C] Scanner slut"));
    }
}
