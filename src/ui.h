#pragma once

#include "current_level.h"

// Called whenever the user drags the on-screen arc to a new value.
// percent is in the arc's native 0-100 range.
using UiPercentChangedCb = void (*)(float percent);

// Which of the test's two current-draw bands (drawn inside the main arc).
enum class TestBand {
    Down, // outer band: top -> bottom steps
    Up,   // inner band: bottom -> top steps
};

// Which end of travel a verdict belongs to.
enum class TestEnd {
    Bottom, // 0%, left end of the arc
    Top,    // 100%, right end of the arc
};

// Builds the screen: a 180-degree draggable arc gauge, a big % readout,
// and a pulse-width (microseconds) readout. on_user_percent_changed is
// invoked when the user drags the arc directly on the touchscreen.
void ui_init(UiPercentChangedCb on_user_percent_changed);

// Reflects an externally-driven value (from the rotary ring or the test)
// onto the arc and labels. Does not re-trigger on_user_percent_changed.
void ui_set_percent(float percent);

// Updates the microsecond pulse-width readout.
void ui_set_pulse_us(int pulse_us);

// Updates the current-draw readout. When available is false, shows "--"
// instead (e.g. when the INA219 sensor isn't available) and milliamps is
// ignored. A small negative milliamps reading is normal ADC noise at
// near-zero load, not a sentinel - callers must signal unavailability via
// available, not by passing a negative value.
void ui_set_current_ma(bool available, float milliamps);

// Enables/disables dragging the arc (disabled while a test is running).
void ui_set_touch_enabled(bool enabled);

// Colours the part of a band between two percent positions (either order).
void ui_paint_test_segment(TestBand band, float from_percent, float to_percent, CurrentLevel level);

// Shows "OK" (Low) or "STICKING" (Medium/High) at an end of the arc.
void ui_show_test_verdict(TestEnd end, CurrentLevel level);

// Removes both bands, both verdicts, and the "OK TO FIT CAMERA" and
// "SENSOR FAULT" messages.
void ui_clear_test_results();

void ui_set_ok_to_fit_visible(bool visible);
void ui_set_sensor_fault_visible(bool visible);
