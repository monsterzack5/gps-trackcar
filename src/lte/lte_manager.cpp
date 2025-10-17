
#include "lte_manager.h"

#include <modem/lte_lc.h>
#include <modem/nrf_modem_lib.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(lte_manager, LOG_LEVEL_DBG);

#ifdef CONFIG_DATE_TIME
#    include "date_time.h"
static K_SEM_DEFINE(time_sem, 0, 1);
static void date_time_evt_handler(const struct date_time_evt* evt)
{
    k_sem_give(&time_sem);
}
#endif

K_SEM_DEFINE(lte_ready, 0, 1);
static void lte_lc_event_handler(const struct lte_lc_evt* const evt)
{
    switch (evt->type) {
    case LTE_LC_EVT_NW_REG_STATUS:
        if ((evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_HOME) || (evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_ROAMING)) {
            LOG_INF("Connected to LTE network");
            k_sem_give(&lte_ready);
        }
        break;

    default:
        break;
    }
}

int init()
{
    int err = 0;

    err = nrf_modem_lib_init();

    if (err) {
        LOG_ERR("Modem library initialization failed, error: %d", err);
        return err;
    }

#ifdef CONFIG_DATE_TIME
    date_time_register_handler(date_time_evt_handler);
#endif

    lte_lc_register_handler(lte_lc_event_handler);

#ifdef CONFIG_GNSS_ASSISTANCE_MINIMAL
    lte_lc_psm_req(true);

    LOG_INF("Connecting to LTE network");

    if (lte_lc_connect() != 0) {
        LOG_ERR("Failed to connect to LTE network");
        return -1;
    }

    LOG_INF("Connected to LTE network");
#endif

#ifdef CONFIG_DATE_TIME
    LOG_INF("Waiting for current time");

    /* Wait for an event from the Date Time library. */
    k_sem_take(&time_sem, K_MINUTES(10));

    if (!date_time_is_valid()) {
        LOG_WRN("Failed to get current time, continuing anyway");
    }
#endif

    return err;
}

int set_mode(LteMode mode)
{
    int err = 0;

    switch (mode) {
    case LteMode::Connected:
        LOG_INF("Connecting to LTE network");

        err = lte_lc_func_mode_set(LTE_LC_FUNC_MODE_ACTIVATE_LTE);
        if (err) {
            LOG_ERR("Failed to activate LTE, error: %d", err);
            return err;
        }

        // TODO: Should probably have a timeout function
        // Maybe if this takes a while, cache gps values?
        k_sem_take(&lte_ready, K_FOREVER);

        // TODO: Do we need this?
        /* Wait for a while, because with IPv4v6 PDN the IPv6 activation takes a bit more time. */
        k_sleep(K_SECONDS(1));

        break;
    case LteMode::Disconnected:

        err = lte_lc_func_mode_set(LTE_LC_FUNC_MODE_DEACTIVATE_LTE);
        if (err) {
            LOG_ERR("Failed to deactivate LTE, error: %d", err);
            return err;
        }
        LOG_INF("LTE disconnected");

        break;
    }

    return err;
}
