#pragma once
#include <Arduino.h>

/*
  I2CBusRecover.h - Simpel bus recovery for RP2040 (eller generelt).
  Brug:
     #include "I2CBusRecover.h"
     I2CBusRecover::recover(5,4); // (SCL,SDA)
  Kald BEFORE Wire.begin().

  Returnerer true hvis SDA er høj (bus frigivet).
*/

namespace I2CBusRecover {

  // Toggler SCL op til 9 gange for at frigive en evt. hængt slave.
  inline bool recover(int sclPin, int sdaPin, bool pullupsInternal = true) {
    pinMode(sclPin, OUTPUT);
    if (pullupsInternal) {
      pinMode(sdaPin, INPUT_PULLUP);
    } else {
      pinMode(sdaPin, INPUT);
    }

    // Hvis SDA allerede høj → bus ok
    if (digitalRead(sdaPin) == HIGH) {
      return true;
    }

    for (int i = 0; i < 9; i++) {
      digitalWrite(sclPin, HIGH);
      delayMicroseconds(8);
      digitalWrite(sclPin, LOW);
      delayMicroseconds(8);
      if (digitalRead(sdaPin) == HIGH) {
        break;
      }
    }

    // Generér et STOP hvis SDA nu er høj
    bool ok = (digitalRead(sdaPin) == HIGH);
    if (ok) {
      digitalWrite(sclPin, HIGH);
      delayMicroseconds(8);
      // SDA allerede højt = STOP
    }
    return ok;
  }

  // Simpel scan (kan bruges efter init)
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