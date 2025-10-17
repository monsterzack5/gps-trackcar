#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "gps/gps.h"
#include "lte/lte_manager.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

int main()
{

    int err = lte::init();

    if (err) {
        LOG_ERR("Fatal: Failed to start the modem, is something wrong? rc: %d", err);
        k_sleep(K_MSEC(20));
        k_oops();
    }

    gps::init2();
    return 0;
}