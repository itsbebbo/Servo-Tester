#pragma once

// Standard hobby servo PWM driver: 50Hz carrier, pulse width mapped linearly
// from SERVO_PULSE_MIN_US (0%) to SERVO_PULSE_MAX_US (100%). Uses the ESP32 LEDC peripheral so the pulse is
// generated entirely in hardware once configured.
void servo_init();

// percent is clamped to [OUTPUT_PERCENT_MIN, OUTPUT_PERCENT_MAX].
void servo_set_percent(float percent);

// Returns the pulse width, in microseconds, that corresponds to the last
// value passed to servo_set_percent(). Useful for UI readouts.
int servo_last_pulse_us();
