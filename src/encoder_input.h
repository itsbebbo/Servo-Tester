#pragma once

#include <cstdint>

// Reads the M5Dial's rotary ring and converts raw quadrature motion into a
// percentage delta, applying an acceleration curve based on how fast the
// ring is being turned (faster turns = bigger steps per detent).
class EncoderInput {
public:
    void begin();

    // Call every loop iteration. Returns the accumulated +/- percent change
    // since the last call (0 if the ring hasn't moved a full detent).
    float poll();

private:
    long last_raw_count_ = 0;
    uint32_t last_detent_ms_ = 0;
};
