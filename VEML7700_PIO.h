#pragma once
/**
 * @file VEML7700_PIO.h
 * @brief Minimal VEML7700 ambient light sensor driver via standard TwoWire.
 *
 * Kommunikerer via beginTransmission/write/endTransmission/requestFrom.
 * Kompatibel med hardware Wire på RP2040.
 * Ingen eksterne afhængigheder ud over Wire.h.
 *
 * VEML7700 I2C-adresse: 0x10 (fast).
 * Datasheet: Vishay VEML7700.
 */

#include <Arduino.h>
#include <Wire.h>

class VEML7700_PIO {
public:
    static constexpr uint8_t I2C_ADDR = 0x10;

    // Register-adresser
    static constexpr uint8_t REG_ALS_CONF = 0x00;  // Konfigurationsregister
    static constexpr uint8_t REG_ALS      = 0x04;  // ALS data (ambient light)
    static constexpr uint8_t REG_WHITE    = 0x05;  // White channel data

    // Gain (bits 12:11 i ALS_CONF)
    enum Gain : uint16_t {
        GAIN_1   = 0x0000,   // 1×
        GAIN_2   = 0x0800,   // 2×
        GAIN_d4  = 0x1000,   // 1/4×
        GAIN_d8  = 0x1800,   // 1/8×
    };

    // Integration time (bits 9:6 i ALS_CONF)
    enum IntTime : uint16_t {
        IT_25MS  = 0x0300,
        IT_50MS  = 0x0200,
        IT_100MS = 0x0000,   // Default
        IT_200MS = 0x0040,
        IT_400MS = 0x0080,
        IT_800MS = 0x00C0,
    };

    VEML7700_PIO() : _wire(nullptr), _gain(GAIN_1), _it(IT_100MS) {}

    /**
     * @brief Initialisér sensoren. Test I2C-forbindelse og konfigurer gain/IT/power-on.
     * @param wire Pointer til TwoWire instans (hardware Wire).
     * @return true hvis sensoren svarer på I2C-adressen.
     */
    bool begin(TwoWire* wire) {
        _wire = wire;
        if (!_wire) return false;

        _wire->beginTransmission(I2C_ADDR);
        if (_wire->endTransmission() != 0) return false;

        return writeConf();
    }

    /** @brief Sæt gain. Skriver konfiguration til sensoren. */
    void setGain(Gain g) {
        _gain = g;
        if (_wire) writeConf();
    }

    /** @brief Sæt integration time. Skriver konfiguration til sensoren. */
    void setIntegrationTime(IntTime it) {
        _it = it;
        if (_wire) writeConf();
    }

    /** @brief Sæt sensoren i shutdown (strømspare). ALS_SD bit = 1. */
    void shutdown() {
        if (!_wire) return;
        uint16_t conf = (uint16_t)_gain | (uint16_t)_it | 0x0001;
        writeReg(REG_ALS_CONF, conf);
    }

    /**
     * @brief Læs rå ALS count (16-bit).
     * @return Rå ALS værdi, eller -1 ved I2C-fejl.
     */
    int32_t readALSRaw() {
        uint16_t raw;
        if (!readReg(REG_ALS, raw)) return -1;
        return (int32_t)raw;
    }

    /**
     * @brief Læs rå white channel count (16-bit).
     * @return Rå white værdi, eller -1 ved I2C-fejl.
     */
    int32_t readWhiteRaw() {
        uint16_t raw;
        if (!readReg(REG_WHITE, raw)) return -1;
        return (int32_t)raw;
    }

    /**
     * @brief Læs lux med resolution-faktor og Vishay korrektionsformel.
     * @return Lux-værdi, eller NAN ved I2C-fejl.
     */
    float readLux() {
        int32_t raw = readALSRaw();
        if (raw < 0) return NAN;
        float lux = (float)raw * resolution();
        // Vishay korrektionsformel for høje lux-værdier (appnote)
        if (lux > 1000.0f) {
            lux = 6.0135e-13f * lux * lux * lux * lux
                - 9.3924e-09f * lux * lux * lux
                + 8.1488e-05f * lux * lux
                + 1.0023f * lux;
        }
        return lux;
    }

    /** @brief Læs white channel i lux-lignende enhed. */
    float readWhite() {
        int32_t raw = readWhiteRaw();
        if (raw < 0) return NAN;
        return (float)raw * resolution();
    }

private:
    TwoWire* _wire;
    Gain     _gain;
    IntTime  _it;

    /**
     * @brief Beregn resolution (lux per count) ud fra gain og integration time.
     *        Base: 0.0576 lux/count ved gain=1×, IT=100ms (Vishay datasheet).
     */
    float resolution() const {
        float base = 0.0576f;

        switch (_gain) {
            case GAIN_2:  base *= 0.5f;  break;
            case GAIN_1:  base *= 1.0f;  break;
            case GAIN_d4: base *= 4.0f;  break;
            case GAIN_d8: base *= 8.0f;  break;
        }

        switch (_it) {
            case IT_800MS: base *= (100.0f / 800.0f); break;
            case IT_400MS: base *= (100.0f / 400.0f); break;
            case IT_200MS: base *= (100.0f / 200.0f); break;
            case IT_100MS: base *= 1.0f;              break;
            case IT_50MS:  base *= (100.0f / 50.0f);  break;
            case IT_25MS:  base *= (100.0f / 25.0f);  break;
        }

        return base;
    }

    /** @brief Skriv ALS_CONF register (gain + IT + power-on, ALS_SD=0). */
    bool writeConf() {
        uint16_t conf = (uint16_t)_gain | (uint16_t)_it;
        return writeReg(REG_ALS_CONF, conf);
    }

    /** @brief Skriv 16-bit register (LSB first). */
    bool writeReg(uint8_t reg, uint16_t value) {
        _wire->beginTransmission(I2C_ADDR);
        _wire->write(reg);
        _wire->write((uint8_t)(value & 0xFF));
        _wire->write((uint8_t)((value >> 8) & 0xFF));
        return (_wire->endTransmission() == 0);
    }

    /** @brief Læs 16-bit register (LSB first, repeated start). */
    bool readReg(uint8_t reg, uint16_t& out) {
        _wire->beginTransmission(I2C_ADDR);
        _wire->write(reg);
        if (_wire->endTransmission(false) != 0) return false;

        if (_wire->requestFrom(I2C_ADDR, (size_t)2) != 2) return false;

        uint8_t lsb = _wire->read();
        uint8_t msb = _wire->read();
        out = ((uint16_t)msb << 8) | lsb;
        return true;
    }
};
