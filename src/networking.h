#pragma once

#include <nrf_modem_gnss.h>

int send_packet(const nrf_modem_gnss_pvt_data_frame& frame);