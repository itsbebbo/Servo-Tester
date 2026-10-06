#include "servo.h"

#include <Arduino.h>
#include <algorithm>
#include <cmath>

#include "config.h"

namespace {
int s_last_pulse_us = SERVO_PULSE_MIN_US;
constexpr uint32_t kPeriodUs = 1000000UL / SERVO_PWM_FREQ_HZ;
constexpr uint32_t kMaxDuty = (1UL << SERVO_PWM_RESOLUTION_BITS) - 1UL;
} // namespace

void servo_init() {
    const bool attached = ledcAttach(SERVO_PWM_PIN, SERVO_PWM_FREQ_HZ, SERVO_PWM_RESOLUTION_BITS);
    Serial.printf("[servo] ledcAttach(pin=%d, freq=%dHz, bits=%d) -> %s\n", SERVO_PWM_PIN,
                  SERVO_PWM_FREQ_HZ, SERVO_PWM_RESOLUTION_BITS, attached ? "OK" : "FAILED");
    servo_set_percent(OUTPUT_PERCENT_STARTUP);
}

void servo_set_percent(float percent) {
    percent = std::clamp(percent, OUTPUT_PERCENT_MIN, OUTPUT_PERCENT_MAX);

    const float span_us = static_cast<float>(SERVO_PULSE_MAX_US - SERVO_PULSE_MIN_US);
    const float pulse_us = SERVO_PULSE_MIN_US + (percent / 100.0f) * span_us;
    s_last_pulse_us = static_cast<int>(std::lround(pulse_us));

    const uint32_t duty = static_cast<uint32_t>(
        std::lround((pulse_us / static_cast<float>(kPeriodUs)) * static_cast<float>(kMaxDuty)));

    const uint32_t clamped_duty = std::min(duty, kMaxDuty);
    ledcWrite(SERVO_PWM_PIN, clamped_duty);
}

int servo_last_pulse_us() {
    return s_last_pulse_us;
}
