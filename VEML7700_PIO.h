#pragma once
/**
 * @file VEML7700_PIO.h
 * @brief Minimal VEML7700 driver til PioI2C (ingen Adafruit BusIO dependency).
 *
 * Bruger kun standard TwoWire beginTransmission/write/endTransmission/requestFrom.
 * Kompatibel med PioI2C og hardware Wire.
 *
 * VEML7700 I2C-adresse: 0x10 (fast)
 */

#include <Arduino.h>
#include <Wire.h>

class VEML7700_PIO {
public:
    // I2C adresse (fast)
    static constexpr uint8_t I2C_ADDR = 0x10;

    // Register-adresser
    static constexpr uint8_t REG_ALS_CONF = 0x00;
    static constexpr uint8_t REG_ALS      = 0x04;
    static constexpr uint8_t REG_WHITE    = 0x05;

    // Gain settings (bits 12:11 i ALS_CONF)
    enum Gain : uint16_t {
        GAIN_1   = 0x0000,   // 1×
        GAIN_2   = 0x0800,   // 2×
        GAIN_d4  = 0x1000,   // ÷4 (1/4)
        GAIN_d8  = 0x1800,   // ÷8 (1/8)
    };

    // Integration time settings (bits 9:6 i ALS_CONF)
    enum IntTime : uint16_t {
        IT_25MS  = 0x0300,
        IT_50MS  = 0x0200,
        IT_100MS = 0x0000,   // default
        IT_200MS = 0x0040,
        IT_400MS = 0x0080,
        IT_800MS = 0x00C0,
    };

    VEML7700_PIO() : _wire(nullptr), _gain(GAIN_1), _it(IT_100MS) {}

    /**
     * @brief Initialiser sensoren.
     * @param wire Pointer til TwoWire (eller PioI2C)
     * @return true hvis sensoren svarer
     */
    bool begin(TwoWire* wire) {
        _wire = wire;
        if (!_wire) return false;

        // Test om sensoren er på bussen
        _wire->beginTransmission(I2C_ADDR);
        if (_wire->endTransmission() != 0) return false;

        // Konfigurer: gain, integration time, power on (ALS_SD=0)
        return writeConf();
    }

    /**
     * @brief Sæt gain.
     */
    void setGain(Gain g) {
        _gain = g;
        if (_wire) writeConf();
    }

    /**
     * @brief Sæt integration time.
     */
    void setIntegrationTime(IntTime it) {
        _it = it;
        if (_wire) writeConf();
    }

    /**
     * @brief Shutdown sensoren (strømspare).
     */
    void shutdown() {
        if (!_wire) return;
        uint16_t conf = (uint16_t)_gain | (uint16_t)_it | 0x0001; // ALS_SD=1
        writeReg(REG_ALS_CONF, conf);
    }

    /**
     * @brief Læs rå ALS count (16-bit).
     * @return Rå ALS værdi, eller -1 ved fejl.
     */
    int32_t readALSRaw() {
        uint16_t raw;
        if (!readReg(REG_ALS, raw)) return -1;
        return (int32_t)raw;
    }

    /**
     * @brief Læs rå white channel count (16-bit).
     * @return Rå white værdi, eller -1 ved fejl.
     */
    int32_t readWhiteRaw() {
        uint16_t raw;
        if (!readReg(REG_WHITE, raw)) return -1;
        return (int32_t)raw;
    }

    /**
     * @brief Læs lux (med resolution-faktor baseret på gain + integration time).
     * @return Lux-værdi, eller NAN ved fejl.
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

    /**
     * @brief Læs white channel i lux-lignende enhed.
     */
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
     * @brief Resolution (lux per count) baseret på gain og integration time.
     *        Fra Vishay VEML7700 datasheet tabel.
     */
    float resolution() const {
        // Base resolution ved gain=1, IT=100ms = 0.0576 lux/count
        float base = 0.0576f;

        // Gain multiplikator (lavere gain = højere resolution-divisor)
        switch (_gain) {
            case GAIN_2:  base *= 0.5f;  break;  // 2× gain → halver resolution
            case GAIN_1:  base *= 1.0f;  break;
            case GAIN_d4: base *= 4.0f;  break;
            case GAIN_d8: base *= 8.0f;  break;
        }

        // Integration time multiplikator
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

    bool writeConf() {
        uint16_t conf = (uint16_t)_gain | (uint16_t)_it; // ALS_SD=0 (power on)
        return writeReg(REG_ALS_CONF, conf);
    }

    bool writeReg(uint8_t reg, uint16_t value) {
        _wire->beginTransmission(I2C_ADDR);
        _wire->write(reg);
        _wire->write((uint8_t)(value & 0xFF));         // LSB first
        _wire->write((uint8_t)((value >> 8) & 0xFF));  // MSB
        return (_wire->endTransmission() == 0);
    }

    bool readReg(uint8_t reg, uint16_t& out) {
        _wire->beginTransmission(I2C_ADDR);
        _wire->write(reg);
        if (_wire->endTransmission(false) != 0) return false;  // repeated start OK

        if (_wire->requestFrom(I2C_ADDR, (size_t)2) != 2) return false;

        uint8_t lsb = _wire->read();
        uint8_t msb = _wire->read();
        out = ((uint16_t)msb << 8) | lsb;
        return true;
    }
};
