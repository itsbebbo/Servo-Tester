#include <M5Dial.h>
#include <algorithm>
#include <cmath>

#include "config.h"
#include "display_driver.h"
#include "encoder_input.h"
#include "ina219.h"
#include "servo.h"
#include "test_sequence.h"
#include "ui.h"

namespace {

float g_target_percent = OUTPUT_PERCENT_STARTUP;
EncoderInput g_encoder;
uint32_t g_last_ina219_poll_ms = 0;
uint32_t g_last_ina219_good_ms = 0;
uint32_t g_last_current_display_ms = 0;
bool g_ina219_ever_read = false;

// Whether the last completed test passed. Cleared when the button is pressed.
bool g_test_passed = false;

// "OK TO FIT CAMERA" shows only after a passed test, and only while the
// servo is back at the top limit.
void update_ok_to_fit() {
    const bool at_top = std::lround(g_target_percent) == std::lround(OUTPUT_PERCENT_MAX);
    ui_set_ok_to_fit_visible(g_test_passed && !test_sequence_is_running() && at_top);
}

void apply_percent(float percent) {
    percent = std::clamp(percent, OUTPUT_PERCENT_MIN, OUTPUT_PERCENT_MAX);
    g_target_percent = percent;

    servo_set_percent(percent);
    ui_set_percent(percent);
    ui_set_pulse_us(servo_last_pulse_us());
    update_ok_to_fit();
}

// Fired when the user drags the on-screen arc directly.
void on_arc_dragged(float percent) {
    apply_percent(percent);
}

void record_good_reading(uint32_t now_ms, float current_ma) {
    g_last_ina219_good_ms = now_ms;
    g_ina219_ever_read = true;
    test_sequence_on_current(current_ma, now_ms);
}

// Stops a running test where it is; bands already drawn stay on screen.
void stop_test_with_sensor_fault() {
    test_sequence_abort();
    ui_set_touch_enabled(true);
    ui_set_sensor_fault_visible(true);
}

void start_test() {
    ui_clear_test_results();
    g_test_passed = false;
    update_ok_to_fit();
    M5Dial.Speaker.tone(8000, 30);

    // Re-initialise the sensor rather than trusting the last reading: it may
    // have been connected since boot, or lost its calibration in a power blip
    // (after which it reads a plausible-looking 0mA).
    float current_ma = 0.0f;
    if (!ina219_init() || !ina219_read_current_ma(&current_ma)) {
        ui_set_sensor_fault_visible(true); // stays until a test starts
        return;
    }

    const uint32_t now_ms = millis();
    record_good_reading(now_ms, current_ma);
    ui_set_touch_enabled(false);
    test_sequence_start(g_target_percent, now_ms, apply_percent);
}

void poll_current_sensor(uint32_t now_ms) {
    const uint32_t interval_ms = test_sequence_is_running() ? TEST_CURRENT_POLL_MS : INA219_SAMPLE_INTERVAL_MS;
    if (now_ms - g_last_ina219_poll_ms < interval_ms) {
        return;
    }
    g_last_ina219_poll_ms = now_ms;

    float current_ma = 0.0f;
    if (ina219_read_current_ma(&current_ma)) {
        record_good_reading(now_ms, current_ma);
        // During a test the sensor is polled far faster than the readout
        // needs refreshing.
        if (now_ms - g_last_current_display_ms >= INA219_SAMPLE_INTERVAL_MS) {
            g_last_current_display_ms = now_ms;
            ui_set_current_ma(true, current_ma);
        }
        return;
    }

    if (g_ina219_ever_read && now_ms - g_last_ina219_good_ms < INA219_STALE_TIMEOUT_MS) {
        return; // one-off failure - keep showing the last good reading
    }
    // Sensor never came up, or has been failing for a while now.
    ui_set_current_ma(false, 0.0f);
    if (test_sequence_is_running()) {
        stop_test_with_sensor_fault();
    }
}

} // namespace

void setup() {
    Serial.begin(115200);

    auto cfg = M5.config();
    M5Dial.begin(cfg, /*enableEncoder=*/true, /*enableRFID=*/false);

    display_driver_init();
    ui_init(on_arc_dragged);
    servo_init();
    g_encoder.begin();
    ina219_init();

    apply_percent(OUTPUT_PERCENT_STARTUP);
}

void loop() {
    M5Dial.update();
    display_driver_task_handler();

    // Always drain the encoder and button, so input made during a test is
    // discarded rather than applied once it finishes.
    const float delta_percent = g_encoder.poll();
    const bool button_pressed = M5Dial.BtnA.wasPressed();

    if (!test_sequence_is_running()) {
        if (delta_percent != 0.0f) {
            apply_percent(g_target_percent + delta_percent);
        }
        if (button_pressed) {
            start_test();
        }
    }

    const uint32_t now_ms = millis();
    poll_current_sensor(now_ms);

    const TestStatus status = test_sequence_update(now_ms);
    switch (status) {
    case TestStatus::Passed:
    case TestStatus::Failed:
        g_test_passed = status == TestStatus::Passed;
        ui_set_touch_enabled(true);
        update_ok_to_fit();
        break;
    case TestStatus::SensorFault:
        stop_test_with_sensor_fault();
        break;
    default:
        break;
    }

    delay(5);
}
