#pragma once

#include <cstddef>
#include <stdint.h>

int network_info_init();

const char* get_imei();

struct ProviderInfo {
    uint16_t mcc;
    uint16_t mnc;
    uint16_t lac;
    uint16_t cellid;
    uint8_t signal_strength;
    float temperature;
};

ProviderInfo get_provider_info();