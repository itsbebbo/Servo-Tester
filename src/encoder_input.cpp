#include "encoder_input.h"

#include <M5Dial.h>
#include <cstdlib>

#include "config.h"

void EncoderInput::begin() {
    last_raw_count_ = M5Dial.Encoder.read();
    last_detent_ms_ = 0;
}

float EncoderInput::poll() {
    const long raw = M5Dial.Encoder.read();
    const long raw_delta = raw - last_raw_count_;
    const int detents = static_cast<int>(raw_delta / ENCODER_COUNTS_PER_DETENT);
    if (detents == 0) {
        return 0.0f;
    }
    last_raw_count_ += static_cast<long>(detents) * ENCODER_COUNTS_PER_DETENT;

    const uint32_t now = millis();
    // Time per detent, so a burst of several detents in one poll (fast spin)
    // is still scored against the acceleration table correctly.
    const uint32_t interval_ms =
        (last_detent_ms_ == 0) ? UINT32_MAX : (now - last_detent_ms_) / std::abs(detents);
    last_detent_ms_ = now;

    float step_percent = ENCODER_ACCEL_TABLE[0].step_percent;
    for (const auto &entry : ENCODER_ACCEL_TABLE) {
        if (interval_ms <= entry.max_interval_ms) {
            step_percent = entry.step_percent;
            break;
        }
    }

    const int direction = ENCODER_INVERT_DIRECTION ? -1 : 1;
    return step_percent * static_cast<float>(detents) * static_cast<float>(direction);
}
