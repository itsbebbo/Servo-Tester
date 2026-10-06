#include "ui.h"

#include <lvgl.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "config.h"

namespace {

// Main arc: 222px across, 18px wide -> occupies radius 93-111.
constexpr lv_coord_t kArcSize = 222;
constexpr lv_coord_t kArcWidth = 18;

// Test current-draw bands, inside the main arc. The arc's knob reaches in to
// radius ~87, so the outer band starts just inside that.
constexpr lv_coord_t kBandWidth = 7;
constexpr lv_coord_t kDownBandOuterRadius = 86;
constexpr lv_coord_t kUpBandOuterRadius = 77;
constexpr int kMaxSegmentsPerBand = TEST_STEP_COUNT;

constexpr uint32_t kColorGreen = 0x00ff00;
constexpr uint32_t kColorAmber = 0xffbf00;
constexpr uint32_t kColorRed = 0xff0000;
constexpr uint32_t kColorGrey = 0x808080;
constexpr uint32_t kColorNoReading = 0x505050;

struct BandSegment {
    uint16_t start_angle;
    uint16_t end_angle;
    CurrentLevel level;
};

struct Band {
    lv_coord_t outer_radius;
    BandSegment segments[kMaxSegmentsPerBand];
    int count;
};

Band s_bands[] = {
    {kDownBandOuterRadius, {}, 0}, // TestBand::Down
    {kUpBandOuterRadius, {}, 0},   // TestBand::Up
};

lv_obj_t *s_arc = nullptr;
lv_obj_t *s_bands_obj = nullptr;
lv_obj_t *s_percent_label = nullptr;
lv_obj_t *s_pulse_label = nullptr;
lv_obj_t *s_current_label = nullptr;
lv_obj_t *s_verdict_labels[2] = {nullptr, nullptr}; // indexed by TestEnd
lv_obj_t *s_ok_to_fit_label = nullptr;
lv_obj_t *s_sensor_fault_label = nullptr;

// Last values written to the labels. The setters are often called with
// unchanged values, and re-setting unchanged label text still forces a redraw.
int s_shown_percent = -1;
int s_shown_pulse_us = -1;
int s_shown_current_ma = -1;
CurrentLevel s_shown_current_level = CurrentLevel::Unknown;
bool s_current_shown_once = false;

bool s_touch_enabled = true;

UiPercentChangedCb s_user_cb = nullptr;

lv_color_t color_for(CurrentLevel level) {
    switch (level) {
    case CurrentLevel::Low:
        return lv_color_hex(kColorGreen);
    case CurrentLevel::Medium:
        return lv_color_hex(kColorAmber);
    case CurrentLevel::High:
        return lv_color_hex(kColorRed);
    default:
        return lv_color_hex(kColorNoReading);
    }
}

// Screen-space angle for a percent position: 0% = 180 deg (9 o'clock),
// 100% = 360 deg (3 o'clock), matching the main arc's bg angles.
uint16_t angle_for_percent(float percent) {
    const float fraction = (percent - OUTPUT_PERCENT_MIN) / (OUTPUT_PERCENT_MAX - OUTPUT_PERCENT_MIN);
    return static_cast<uint16_t>(std::lround(180.0f + std::clamp(fraction, 0.0f, 1.0f) * 180.0f));
}

lv_point_t bands_center() {
    lv_area_t coords;
    lv_obj_get_coords(s_bands_obj, &coords);
    return {static_cast<lv_coord_t>(coords.x1 + lv_area_get_width(&coords) / 2),
            static_cast<lv_coord_t>(coords.y1 + lv_area_get_height(&coords) / 2)};
}

// Only touch the HIDDEN flag on an actual state change: LVGL invalidates
// (forces a redraw of) the object whenever LV_OBJ_FLAG_HIDDEN is touched,
// even if it was already set to that value, and some callers run on every
// loop iteration.
void set_visible(lv_obj_t *obj, bool visible) {
    const bool currently_hidden = lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN);
    if (visible && currently_hidden) {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else if (!visible && !currently_hidden) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

void bands_draw_cb(lv_event_t *e) {
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);
    const lv_point_t center = bands_center();

    lv_draw_arc_dsc_t dsc;
    lv_draw_arc_dsc_init(&dsc);
    dsc.width = kBandWidth;
    dsc.rounded = 0; // square ends so adjacent segments butt together

    for (const Band &band : s_bands) {
        for (int i = 0; i < band.count; ++i) {
            const BandSegment &seg = band.segments[i];
            dsc.color = color_for(seg.level);
            lv_draw_arc(draw_ctx, &dsc, &center, band.outer_radius, seg.start_angle, seg.end_angle);
        }
    }
}

void arc_event_cb(lv_event_t *e) {
    lv_obj_t *arc = static_cast<lv_obj_t *>(lv_event_get_target(e));
    const int16_t value = lv_arc_get_value(arc);
    if (s_user_cb) {
        s_user_cb(static_cast<float>(value));
    }
}

// LVGL 8.4's lv_arc has a bug in its own touch-drag handling for arcs whose
// background sweep doesn't start at angle 0 (ours starts at 180): the angle
// it computes for the touch point gets compared against the wrong frame of
// reference, so touches on the visible (top) half are rejected as "outside"
// (no-op) while touches on the empty (bottom) half are wrongly accepted and
// clamped to the max value. We bypass lv_arc's own dragging logic entirely
// (the arc is left non-clickable) and compute the angle ourselves here,
// listening on the screen so drags starting anywhere still work.
constexpr int kArcTouchInnerRadius = 60;  // ignore taps over the center labels
constexpr int kArcTouchOuterRadius = 130; // ignore stray taps near the bezel

void arc_touch_cb(lv_event_t *e) {
    if (!s_touch_enabled) {
        return;
    }

    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) {
        return;
    }

    lv_point_t p;
    lv_indev_get_point(indev, &p);

    const int dx = p.x - DISPLAY_WIDTH / 2;
    const int dy = p.y - DISPLAY_HEIGHT / 2;
    const int dist_sq = dx * dx + dy * dy;
    if (dist_sq < kArcTouchInnerRadius * kArcTouchInnerRadius ||
        dist_sq > kArcTouchOuterRadius * kArcTouchOuterRadius) {
        return;
    }
    if (dy > 0) {
        return; // bottom half - outside the arc's 180-360 deg sweep
    }

    // Screen-space angle, 0 deg = 3 o'clock, increasing clockwise (matches
    // lv_arc_set_bg_angles(180, 360): 9 o'clock -> 12 o'clock -> 3 o'clock).
    constexpr float kRadToDeg = 57.29577951308232f;
    float angle_deg = std::atan2(static_cast<float>(dy), static_cast<float>(dx)) * kRadToDeg;
    if (angle_deg < 180.0f) {
        angle_deg += 360.0f; // wrap the dy==0, dx>0 case (0 deg) up to 360
    }

    const float span = OUTPUT_PERCENT_MAX - OUTPUT_PERCENT_MIN;
    const float percent = OUTPUT_PERCENT_MIN + (angle_deg - 180.0f) / 180.0f * span;
    const int16_t value = static_cast<int16_t>(std::lround(percent));

    if (value != lv_arc_get_value(s_arc)) {
        lv_arc_set_value(s_arc, value);
        lv_event_send(s_arc, LV_EVENT_VALUE_CHANGED, nullptr);
    }
}

lv_obj_t *create_message_label(lv_obj_t *scr, const char *text, uint32_t color) {
    lv_obj_t *label = lv_label_create(scr);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_text(label, text);
    // -38, not -4: this close to the bottom of a *round* display the chord
    // width is only ~60px (nowhere near enough for this string) - -38 keeps
    // it inside the display's usable width (~175px there) while sitting just
    // below the pulse-width readout and the verdicts.
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -38);
    lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    return label;
}

} // namespace

void ui_init(UiPercentChangedCb on_user_percent_changed) {
    s_user_cb = on_user_percent_changed;

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    // --- Test current-draw bands, custom-drawn inside the main arc ---
    // Created before the arc so the arc's knob draws on top of them.
    s_bands_obj = lv_obj_create(scr);
    lv_obj_remove_style_all(s_bands_obj);
    lv_obj_set_size(s_bands_obj, kDownBandOuterRadius * 2, kDownBandOuterRadius * 2);
    lv_obj_center(s_bands_obj);
    lv_obj_clear_flag(s_bands_obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_bands_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_bands_obj, bands_draw_cb, LV_EVENT_DRAW_MAIN, nullptr);

    // --- 180-degree draggable arc gauge (9 o'clock -> 12 o'clock -> 3 o'clock) ---
    s_arc = lv_arc_create(scr);
    lv_obj_set_size(s_arc, kArcSize, kArcSize);
    lv_obj_center(s_arc);
    lv_arc_set_bg_angles(s_arc, 180, 360);
    lv_arc_set_range(s_arc, static_cast<int16_t>(OUTPUT_PERCENT_MIN),
                      static_cast<int16_t>(OUTPUT_PERCENT_MAX));
    lv_arc_set_value(s_arc, static_cast<int16_t>(OUTPUT_PERCENT_STARTUP));

    // lv_arc's built-in touch dragging is buggy for a 180-360 deg sweep (see
    // arc_touch_cb above), so leave the arc non-clickable and drive it from
    // our own screen-level touch handler instead.
    lv_obj_clear_flag(s_arc, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_set_style_arc_width(s_arc, kArcWidth, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_arc, lv_color_hex(0x303030), LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc, kArcWidth, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_arc, lv_color_hex(0x00c2a8), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_arc, lv_color_hex(0x00c2a8), LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_arc, 6, LV_PART_KNOB);

    lv_obj_add_event_cb(s_arc, arc_event_cb, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(scr, arc_touch_cb, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(scr, arc_touch_cb, LV_EVENT_PRESSING, nullptr);

    // --- Current-draw readout (INA219, Port A), inside the test bands ---
    s_current_label = lv_label_create(scr);
    lv_obj_set_style_text_font(s_current_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_current_label, lv_color_hex(kColorGrey), 0);
    lv_label_set_text(s_current_label, "-- mA");
    // 70 leaves clear space below the inner (step-up) band, which reaches
    // down to y ~55 at the label's corners.
    lv_obj_align(s_current_label, LV_ALIGN_TOP_MID, 0, 70);

    // --- Big center %, caption, and pulse-width readout ---
    s_percent_label = lv_label_create(scr);
    lv_obj_set_style_text_font(s_percent_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_percent_label, lv_color_white(), 0);
    lv_label_set_text(s_percent_label, "");
    lv_obj_align(s_percent_label, LV_ALIGN_CENTER, 0, -2);

    lv_obj_t *caption = lv_label_create(scr);
    lv_obj_set_style_text_font(caption, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(caption, lv_color_hex(kColorGrey), 0);
    lv_label_set_text(caption, "SERVO OUTPUT");
    lv_obj_align_to(caption, s_percent_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);

    s_pulse_label = lv_label_create(scr);
    lv_obj_set_style_text_font(s_pulse_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_pulse_label, lv_color_hex(0x606060), 0);
    lv_label_set_text(s_pulse_label, "0000 us");
    lv_obj_align_to(s_pulse_label, caption, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);
    // Re-anchor to the screen (lv_obj_align_to is a one-off placement) so
    // the label stays centred as its width changes, e.g. "850 us".
    lv_obj_update_layout(s_pulse_label);
    lv_obj_align(s_pulse_label, LV_ALIGN_TOP_MID, 0, lv_obj_get_y(s_pulse_label));

    // --- STICKING/OK verdicts, below each end of the arc ---
    // y +52 drops them below the caption; x 18 keeps "STICKING" inside the
    // round display while clearing the pulse-width readout.
    for (int i = 0; i < 2; ++i) {
        const bool bottom = static_cast<TestEnd>(i) == TestEnd::Bottom;
        lv_obj_t *label = lv_label_create(scr);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
        lv_label_set_text(label, "");
        lv_obj_align(label, bottom ? LV_ALIGN_LEFT_MID : LV_ALIGN_RIGHT_MID, bottom ? 18 : -18, 52);
        lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
        s_verdict_labels[i] = label;
    }

    // --- End-of-test / fault messages (mutually exclusive in practice) ---
    s_ok_to_fit_label = create_message_label(scr, "OK TO FIT CAMERA", kColorGreen);
    s_sensor_fault_label = create_message_label(scr, "SENSOR FAULT", kColorRed);
}

void ui_set_percent(float percent) {
    const int rounded = static_cast<int>(std::lround(percent));
    lv_arc_set_value(s_arc, static_cast<int16_t>(rounded)); // no-op if unchanged
    if (rounded != s_shown_percent) {
        s_shown_percent = rounded;
        lv_label_set_text_fmt(s_percent_label, "%d%%", rounded);
    }
}

void ui_set_pulse_us(int pulse_us) {
    if (pulse_us != s_shown_pulse_us) {
        s_shown_pulse_us = pulse_us;
        lv_label_set_text_fmt(s_pulse_label, "%d us", pulse_us);
    }
}

void ui_set_current_ma(bool available, float milliamps) {
    const int rounded = available ? static_cast<int>(std::lround(milliamps)) : -1;
    const CurrentLevel level = available ? current_level_for(milliamps) : CurrentLevel::Unknown;
    if (s_current_shown_once && rounded == s_shown_current_ma && level == s_shown_current_level) {
        return;
    }
    s_current_shown_once = true;
    s_shown_current_ma = rounded;
    s_shown_current_level = level;

    if (!available) {
        lv_label_set_text(s_current_label, "-- mA");
        lv_obj_set_style_text_color(s_current_label, lv_color_hex(kColorGrey), 0);
        return;
    }
    lv_label_set_text_fmt(s_current_label, "%d mA", rounded);
    lv_obj_set_style_text_color(s_current_label, color_for(level), 0);
}

void ui_set_touch_enabled(bool enabled) {
    s_touch_enabled = enabled;
}

void ui_paint_test_segment(TestBand band_id, float from_percent, float to_percent, CurrentLevel level) {
    Band &band = s_bands[static_cast<int>(band_id)];
    if (band.count >= kMaxSegmentsPerBand) {
        return;
    }

    const uint16_t a = angle_for_percent(from_percent);
    const uint16_t b = angle_for_percent(to_percent);
    BandSegment &seg = band.segments[band.count++];
    seg.start_angle = std::min(a, b);
    seg.end_angle = std::max(a, b);
    seg.level = level;

    // Redraw just this segment rather than the whole bands object.
    const lv_point_t center = bands_center();
    lv_area_t area;
    lv_draw_arc_get_area(center.x, center.y, band.outer_radius, seg.start_angle, seg.end_angle, kBandWidth,
                         false, &area);
    lv_obj_invalidate_area(s_bands_obj, &area);
}

void ui_show_test_verdict(TestEnd end, CurrentLevel level) {
    lv_obj_t *label = s_verdict_labels[static_cast<int>(end)];
    lv_label_set_text(label, level == CurrentLevel::Low ? "OK" : "STICKING");
    lv_obj_set_style_text_color(label, color_for(level), 0);
    set_visible(label, true);
}

void ui_clear_test_results() {
    for (Band &band : s_bands) {
        band.count = 0;
    }
    lv_obj_invalidate(s_bands_obj);

    for (lv_obj_t *label : s_verdict_labels) {
        set_visible(label, false);
    }
    set_visible(s_ok_to_fit_label, false);
    set_visible(s_sensor_fault_label, false);
}

void ui_set_ok_to_fit_visible(bool visible) {
    set_visible(s_ok_to_fit_label, visible);
}

void ui_set_sensor_fault_visible(bool visible) {
    set_visible(s_sensor_fault_label, visible);
}
