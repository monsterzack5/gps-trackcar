#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, CONFIG_TRACKCAR_DEFAULT_LOG_LEVEL);

#include "battery.h"
#include "connectivity.h"
#include "device_ui.h"
#include "gps.h"
#include "low_power.h"
#include "network_info.h"
#include "network_requests.h"
#include "pmic.h"
#include "sdcard.h"
#include "temp_sensor.h"

#include <modem/lte_lc.h>
#include <modem/nrf_modem_lib.h>

int main()
{
    nrf_modem_lib_init();
    temp_sensor_init();
    device_ui_init();
    sdcard_init();
    battery_init();

    LOG_INF("IMEI: %s", get_imei());

    if (IS_ENABLED(CONFIG_TRACKCAR_LOW_POWER)) {
        k_sleep(K_SECONDS(5));
        set_power_mode(PowerMode::Low);
    }

    int rc = networking_init();
    network_requests_init();

    if (rc != 0) {
        LOG_ERR("Failed to init LTE, check sim card? rc = %d", rc);
        k_sleep(K_SECONDS(5));
        k_oops();
    }

    rc = gps_init();

    if (rc != 0) {
        LOG_ERR("Failed to init GPS, not under earths orbit? rc = %d", rc);
        k_sleep(K_SECONDS(5));
        k_oops();
    }

    LOG_INF("Init Done, main thread sleeping forever....");

    while (1) {
        k_sleep(K_FOREVER);
    }
}