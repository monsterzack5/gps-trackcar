#pragma once

enum class ModemMode {
    connect_lte,
    deactivate_lte,
    connect_gps,
    deactivate_gps,
};

int lte_init();

int lte_set_modem_mode(ModemMode mode);