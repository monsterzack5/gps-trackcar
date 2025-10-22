#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

/* Structure */
// Main:
//  - Init lte
//  - Init gps
//  (TODO)
//  - check if almanac was updated
//      - update if needed
//  - Acquire GPS Lock
//      - Use assistance, prioritize GPS
//  - Report GPS to traccar
//  - Report extra information if required
//  - Repeat
//      - if moving, repeat every 5 minutes
//      - if stationary, repeat every 20 minutes

/* Code Points */
// LTEHandler
// GPSHandler

// Previous control flow:
// init modem, add time handler if needed
// add handler to catch modem state changes, wait for a sem from that function
// init gps workqueue
// set modem to GPS Mode
// add event handler which reads gps events (nmea data)
// setup proper mask and gnss states with modem util
// setup sample rate, prio mode
// GPS Data acquisition structure:
//   - Create 1 sem, PVT data
//   - Create a MessageQueue, fill when we have nmea data
//   - when given PVT Sem, handle it
//   - wait on NMEA data in the queue
// NOTES:
//  - gnss event handler gives sems
//  - ONLY Assistance data is handled in a workqueue
//  - Assistance workqueue is called when getting a AGNESS _REQUEST_ from the modem
//  - The assistance workqueue is used SOLELY to not block the main thread, since it can take some time
//      - I am not sure if we need this system, it seems like a lot.

#include "battery.h"
#include "connectivity.h"
#include "gps.h"

int main()
{
    battery_init();

    int rc = networking_init();

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
        k_sleep(K_SECONDS(5));
        get_battery_stats();
    }
}