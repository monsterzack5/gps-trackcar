#pragma once

#include <nrf_modem_gnss.h>

int network_requests_init();

int send_gps_update(const nrf_modem_gnss_pvt_data_frame& frame);
