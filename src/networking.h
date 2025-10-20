#pragma once

#include <nrf_modem_gnss.h>

enum class NetworkState {
    Connected,
    Disconnected,
    Activated,
    Deactivated,
};

struct traccar_params {
    const char* imei;
    nrf_modem_gnss_pvt_data_frame& frame;
    double battery_percent;
};

int networking_init();

int set_networking_state(NetworkState state);

int send_packet(const traccar_params& params);