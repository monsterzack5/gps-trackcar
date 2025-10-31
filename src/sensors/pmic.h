#pragma once

// NOTE: PMIC1300 is init'ed via startup.c defined in boards folder

int disable_rp2040();
int enable_rp2040();

enum class RPState {
    On,
    Off
};

int set_rp2040_state(RPState state);