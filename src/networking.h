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

int send_packet(const nrf_modem_gnss_pvt_data_frame& frame);