#pragma once

// General things for enabling low power mode

enum class PowerMode {
    Low,
    High
};

int set_power_mode(PowerMode mode);