#pragma once

#include <cstddef>
#include <stdint.h>
#include <zephyr/logging/log.h>

int network_info_init();

const char* get_imei();

struct ProviderInfo {
    uint32_t mcc;
    uint32_t mnc;
    uint32_t lac;
    uint32_t cellid;
    uint32_t signal_strength;
    double temperature;

    void print()
    {
        LOG_MODULE_DECLARE(network_info, CONFIG_TRACCAR_DEFAULT_LOG_LEVEL);
        LOG_DBG("ProviderInfo: mcc: %u, mnc: %u, lac: %u, cellid: %u, signal_strength: %u, temperature: %f",
            mcc, mnc, lac, cellid, signal_strength, temperature);
    }
};

ProviderInfo get_provider_info();