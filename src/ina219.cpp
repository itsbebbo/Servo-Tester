#include "ina219.h"

#include <M5Unified.h>
#include <cmath>
#include <cstdint>

#include "config.h"

namespace {

constexpr uint8_t kRegConfig = 0x00;
constexpr uint8_t kRegCurrent = 0x04;
constexpr uint8_t kRegCalibration = 0x05;

// 32V bus range, +-320mV shunt range, 12-bit shunt+bus ADC, continuous mode.
constexpr uint16_t kConfigValue = 0x399F;

constexpr uint32_t kI2cFreqHz = 100000;

bool s_available = false;
float s_current_lsb_ma = 0.0f; // mA represented by 1 LSB of the Current register

bool read16(uint8_t reg, uint16_t *out) {
    uint8_t buf[2] = {0, 0};
    if (!M5.Ex_I2C.readRegister(INA219_I2C_ADDRESS, reg, buf, sizeof(buf), kI2cFreqHz)) {
        return false;
    }
    *out = (static_cast<uint16_t>(buf[0]) << 8) | buf[1];
    return true;
}

bool write16(uint8_t reg, uint16_t value) {
    const uint8_t buf[2] = {static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value & 0xFF)};
    return M5.Ex_I2C.writeRegister(INA219_I2C_ADDRESS, reg, buf, sizeof(buf), kI2cFreqHz);
}

} // namespace

bool ina219_init() {
    M5.Ex_I2C.begin();

    // Calibration procedure from the INA219 datasheet: pick the current LSB
    // from the configured max expected current, then derive the calibration
    // register so the Current register reads directly in current_LSB steps.
    const float current_lsb_a = INA219_MAX_EXPECTED_CURRENT_A / 32768.0f;
    const float cal_f = 0.04096f / (current_lsb_a * INA219_SHUNT_RESISTANCE_OHMS);
    // Bit 0 of the calibration register is not implemented in hardware (always
    // reads back as 0 per the datasheet), so clear it here to match what the
    // device will actually store - otherwise an odd rounded value would never
    // read back equal and init would report a false failure.
    const uint16_t calibration = static_cast<uint16_t>(std::lround(cal_f)) & 0xFFFEu;
    s_current_lsb_ma = current_lsb_a * 1000.0f;

    write16(kRegCalibration, calibration);
    write16(kRegConfig, kConfigValue);

    // Confirm the sensor is actually present by reading calibration back.
    uint16_t readback = 0;
    s_available = read16(kRegCalibration, &readback) && (readback == calibration);
    Serial.printf("[ina219] init addr=0x%02X calibration=%u -> %s\n", INA219_I2C_ADDRESS, calibration,
                  s_available ? "OK" : "FAILED");

    if (!s_available) {
        // Dump whatever actually responds on Port A so a wiring/address
        // mismatch is obvious from the serial log instead of just "FAILED".
        Serial.println("[ina219] scanning Port A (Ex_I2C) for any responding address...");
        bool found_any = false;
        for (uint8_t addr = 0x08; addr < 0x78; ++addr) {
            if (M5.Ex_I2C.scanID(addr, kI2cFreqHz)) {
                Serial.printf("[ina219]   found device at 0x%02X\n", addr);
                found_any = true;
            }
        }
        if (!found_any) {
            Serial.println("[ina219]   no I2C devices responded at all - check wiring/power on Port A");
        }
    }

    return s_available;
}

bool ina219_is_available() {
    return s_available;
}

bool ina219_read_current_ma(float *out_ma) {
    if (!s_available) {
        return false;
    }
    // A single I2C transaction occasionally glitches (bus noise, timing) -
    // retry a couple of times before reporting a real failure, so a
    // transient blip doesn't read as the sensor having disappeared.
    constexpr int kMaxAttempts = 3;
    uint16_t raw_bits = 0;
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        if (read16(kRegCurrent, &raw_bits)) {
            *out_ma = static_cast<float>(static_cast<int16_t>(raw_bits)) * s_current_lsb_ma;
            return true;
        }
    }
    return false;
}
