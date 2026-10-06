#pragma once

#include "config.h"

// Current-draw classification shared by the live mA readout, the test's arc
// bands and the end-of-travel verdicts (see CURRENT_AMBER_MA/CURRENT_RED_MA).
enum class CurrentLevel {
    Unknown, // no reading available
    Low,     // green, "OK"
    Medium,  // amber, "STICKING"
    High,    // red, "STICKING"
};

inline CurrentLevel current_level_for(float milliamps) {
    if (milliamps >= CURRENT_RED_MA) {
        return CurrentLevel::High;
    }
    if (milliamps >= CURRENT_AMBER_MA) {
        return CurrentLevel::Medium;
    }
    return CurrentLevel::Low;
}
