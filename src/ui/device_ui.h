#pragma once

#include "stdint.h"

namespace BlinkPattern {

// NOTE: Length is how many times the LED will turn ON!
// On Time - Off Time
const uint16_t LOW_POWER_MODE_ACTIVATED[] = { 250, 250 };
const uint8_t LOW_POWER_MODE_ACTIVATED_LENGTH = 6;

const uint16_t HIGH_POWER_MODE_ACTIVATED[] = { 1000, 1000 };
const uint8_t HIGH_POWER_MODE_ACTIVATED_LENGTH = 3;

} // namespace BlinkPattern

enum class BlinkCode {
    LowPowerModeActivated,
    HighPowerModeActivated,
};

void blink_pattern(BlinkCode code);

int device_ui_init();
