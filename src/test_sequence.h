#pragma once

#include <cstdint>

// Automated sticking test: (move to the top limit if needed) -> step down
// to the bottom limit -> dwell + verdict -> step up to the top limit ->
// dwell + verdict. Paints the arc's current-draw bands and verdict labels
// via ui.h as it goes. See "Automated test sequence" in CLAUDE.md.

// Drives the servo (and its on-screen position) to percent.
using TestSetPercentFn = void (*)(float percent);

enum class TestStatus {
    Idle,        // no test running
    Running,
    Passed,      // just finished, both ends OK (returned once)
    Failed,      // just finished, at least one end STICKING (returned once)
    SensorFault, // stopped: no current readings during a dwell (returned once)
};

// Starts a test. The caller is responsible for clearing any previous
// results from the screen and for checking the sensor works beforehand.
void test_sequence_start(float current_percent, uint32_t now_ms, TestSetPercentFn set_percent);

// Feed every successful INA219 reading taken while a test is running.
void test_sequence_on_current(float milliamps, uint32_t now_ms);

// Advances the test; call every loop iteration.
TestStatus test_sequence_update(uint32_t now_ms);

// Stops a running test immediately, leaving the servo where it is.
void test_sequence_abort();

bool test_sequence_is_running();
