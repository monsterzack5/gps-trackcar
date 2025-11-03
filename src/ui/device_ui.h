#pragma once

#include "stdint.h"

namespace BlinkPattern {

// NOTE: Length is how many times the LED will turn ON!
// On Time - Off Time
const uint16_t LOW_POWER_MODE_ACTIVATED[] = { 100, 100 };
const uint8_t LOW_POWER_MODE_ACTIVATED_LENGTH = 10;

const uint16_t HIGH_POWER_MODE_ACTIVATED[] = { 1000, 1000 };
const uint8_t HIGH_POWER_MODE_ACTIVATED_LENGTH = 4;

} // namespace BlinkPattern

enum class BlinkCode {
    LowPowerModeActivated,
    HighPowerModeActivated,
};

void blink_pattern(BlinkCode code);

int device_ui_init();
