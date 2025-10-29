#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, CONFIG_TRACCAR_DEFAULT_LOG_LEVEL);

#include "battery.h"
#include "connectivity.h"
#include "gps.h"
#include "network_info.h"
#include "network_requests.h"
#include "sdcard.h"

int main()
{
    sdcard_init();
    battery_init();

    int rc = networking_init();
    network_info_init();
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
        // get_battery_stats();
    }
}