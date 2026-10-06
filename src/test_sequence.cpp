#include "test_sequence.h"

#include "config.h"
#include "current_level.h"
#include "ui.h"

static_assert(TEST_STEP_COUNT > 0, "TEST_STEP_COUNT must be at least 1");
static_assert(TEST_STEP_READ_WINDOW_MS <= TEST_STEP_SETTLE_MS,
              "the step reading window must fit inside the settle pause");
static_assert(TEST_DWELL_SETTLE_MS < TEST_DWELL_MS, "the dwell must leave time to measure after settling");

namespace {

enum class Phase { Idle, Preposition, StepsDown, DwellBottom, StepsUp, DwellTop };

Phase s_phase = Phase::Idle;
// Start of the current phase or, during StepsDown/StepsUp, the current step.
uint32_t s_phase_start_ms = 0;
TestSetPercentFn s_set_percent = nullptr;

// Peak current in the window currently being measured: the end of a step's
// settle pause, or the post-settle part of a dwell.
float s_peak_ma = 0.0f;
bool s_have_peak = false;

int s_step = 0;           // index of the step in progress
bool s_step_read = false; // whether that step's reading has been taken
bool s_bottom_ok = false;

bool is_step_phase() {
    return s_phase == Phase::StepsDown || s_phase == Phase::StepsUp;
}

void enter_phase(Phase phase, uint32_t now_ms) {
    s_phase = phase;
    s_phase_start_ms = now_ms;
    s_have_peak = false;
}

// Servo position after step index (-1 = where the steps start from).
float step_percent(bool down, int index) {
    const float fraction = static_cast<float>(index + 1) / TEST_STEP_COUNT;
    const float travel = fraction * (OUTPUT_PERCENT_MAX - OUTPUT_PERCENT_MIN);
    return down ? OUTPUT_PERCENT_MAX - travel : OUTPUT_PERCENT_MIN + travel;
}

void begin_step(int index, uint32_t now_ms) {
    s_step = index;
    s_step_read = false;
    s_phase_start_ms = now_ms;
    s_have_peak = false;
    s_set_percent(step_percent(s_phase == Phase::StepsDown, index));
}

void enter_steps(Phase phase, uint32_t now_ms) {
    enter_phase(phase, now_ms);
    begin_step(0, now_ms);
}

bool is_measuring(uint32_t now_ms) {
    const uint32_t elapsed_ms = now_ms - s_phase_start_ms;
    if (is_step_phase()) {
        return !s_step_read && elapsed_ms >= TEST_STEP_SETTLE_MS - TEST_STEP_READ_WINDOW_MS &&
               elapsed_ms < TEST_STEP_SETTLE_MS;
    }
    if (s_phase == Phase::DwellBottom || s_phase == Phase::DwellTop) {
        return elapsed_ms >= TEST_DWELL_SETTLE_MS;
    }
    return false;
}

// Returns the peak for the window just finished and starts a new window.
CurrentLevel take_peak_level() {
    const CurrentLevel level = s_have_peak ? current_level_for(s_peak_ma) : CurrentLevel::Unknown;
    s_have_peak = false;
    return level;
}

// Runs the current step: settle, take the reading and paint the segment
// travelled, pause, then move on. Returns true once the last step is done.
bool run_steps(uint32_t now_ms) {
    const bool down = s_phase == Phase::StepsDown;
    const uint32_t elapsed_ms = now_ms - s_phase_start_ms;

    if (!s_step_read && elapsed_ms >= TEST_STEP_SETTLE_MS) {
        ui_paint_test_segment(down ? TestBand::Down : TestBand::Up, step_percent(down, s_step - 1),
                              step_percent(down, s_step), take_peak_level());
        s_step_read = true;
    }

    if (s_step_read && elapsed_ms >= TEST_STEP_SETTLE_MS + TEST_STEP_GAP_MS) {
        if (s_step + 1 >= TEST_STEP_COUNT) {
            return true;
        }
        begin_step(s_step + 1, now_ms);
    }
    return false;
}

} // namespace

void test_sequence_start(float current_percent, uint32_t now_ms, TestSetPercentFn set_percent) {
    s_set_percent = set_percent;
    s_bottom_ok = false;

    // The test always starts from the top limit. If the operator left the
    // servo somewhere else, send it there and give it time to arrive.
    s_set_percent(OUTPUT_PERCENT_MAX);
    if (current_percent >= OUTPUT_PERCENT_MAX) {
        enter_steps(Phase::StepsDown, now_ms);
    } else {
        enter_phase(Phase::Preposition, now_ms);
    }
}

void test_sequence_on_current(float milliamps, uint32_t now_ms) {
    if (!is_measuring(now_ms)) {
        return;
    }
    if (!s_have_peak || milliamps > s_peak_ma) {
        s_peak_ma = milliamps;
        s_have_peak = true;
    }
}

TestStatus test_sequence_update(uint32_t now_ms) {
    const uint32_t elapsed_ms = now_ms - s_phase_start_ms;

    switch (s_phase) {
    case Phase::Idle:
        return TestStatus::Idle;

    case Phase::Preposition:
        if (elapsed_ms >= TEST_PREPOSITION_MS) {
            enter_steps(Phase::StepsDown, now_ms);
        }
        break;

    case Phase::StepsDown:
        if (run_steps(now_ms)) {
            enter_phase(Phase::DwellBottom, now_ms);
        }
        break;

    case Phase::StepsUp:
        if (run_steps(now_ms)) {
            enter_phase(Phase::DwellTop, now_ms);
        }
        break;

    case Phase::DwellBottom:
    case Phase::DwellTop: {
        if (elapsed_ms < TEST_DWELL_MS) {
            break;
        }
        const bool at_bottom = s_phase == Phase::DwellBottom;
        const CurrentLevel level = take_peak_level();
        if (level == CurrentLevel::Unknown) {
            // A full measuring window with no readings - no verdict is possible.
            s_phase = Phase::Idle;
            return TestStatus::SensorFault;
        }
        ui_show_test_verdict(at_bottom ? TestEnd::Bottom : TestEnd::Top, level);

        const bool ok = level == CurrentLevel::Low;
        if (at_bottom) {
            s_bottom_ok = ok;
            enter_steps(Phase::StepsUp, now_ms);
            break;
        }
        s_phase = Phase::Idle;
        return (s_bottom_ok && ok) ? TestStatus::Passed : TestStatus::Failed;
    }
    }

    return TestStatus::Running;
}

void test_sequence_abort() {
    s_phase = Phase::Idle;
}

bool test_sequence_is_running() {
    return s_phase != Phase::Idle;
}
