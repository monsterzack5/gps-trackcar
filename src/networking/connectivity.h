#pragma once

#include <nrf_modem_gnss.h>

enum class NetworkState {
    Connected,
    Disconnected,
    Activated,
    Deactivated,
};

int networking_init();

int set_networking_state(NetworkState state);
