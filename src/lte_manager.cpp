#include "lte_manager.h"

#include <modem/lte_lc.h>
#include <modem/nrf_modem_lib.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(lte_manager, LOG_LEVEL_DBG);

// Given when we connect to the network
K_SEM_DEFINE(network_connected, 0, 1);

static void lte_lc_event_handler(const lte_lc_evt* const event)
{
    // TODO: Move all printing to print func
    switch (event->type) {

        /** Event type. */
    case LTE_LC_EVT_NW_REG_STATUS:
        LOG_DBG("New Event: LTE_LC_EVT_NW_REG_STATUS");

        // We may have connected or disconnected

        if ((event->nw_reg_status == LTE_LC_NW_REG_REGISTERED_HOME) || (event->nw_reg_status == LTE_LC_NW_REG_REGISTERED_ROAMING)) {
            LOG_INF("Connected to LTE network");
            k_sem_give(&network_connected);
        }

        break;

#if defined(CONFIG_LTE_LC_PSM_MODULE)
    case LTE_LC_EVT_PSM_UPDATE:
        LOG_DBG("New Event: LTE_LC_EVT_PSM_UPDATE");
        break;
#endif /* CONFIG_LTE_LC_PSM_MODULE */

#if defined(CONFIG_LTE_LC_EDRX_MODULE)
    case LTE_LC_EVT_EDRX_UPDATE:
        LOG_DBG("New Event: LTE_LC_EVT_EDRX_UPDATE");
        break;
#endif /* CONFIG_LTE_LC_EDRX_MODULE */
    case LTE_LC_EVT_RRC_UPDATE:
        LOG_DBG("New Event: LTE_LC_EVT_RRC_UPDATE");
        break;
    case LTE_LC_EVT_CELL_UPDATE:
        LOG_DBG("New Event: LTE_LC_EVT_CELL_UPDATE");
        break;
    case LTE_LC_EVT_LTE_MODE_UPDATE:
        LOG_DBG("New Event: LTE_LC_EVT_LTE_MODE_UPDATE");
        break;

#if defined(CONFIG_LTE_LC_TAU_PRE_WARNING_MODULE)
    case LTE_LC_EVT_TAU_PRE_WARNING:
        LOG_DBG("New Event: LTE_LC_EVT_TAU_PRE_WARNING");
        break;
#endif /* CONFIG_LTE_LC_TAU_PRE_WARNING_MODULE */

#if defined(CONFIG_LTE_LC_NEIGHBOR_CELL_MEAS_MODULE)
        LTE_LC_EVT_NEIGHBOR_CELL_MEAS
#endif /* CONFIG_LTE_LC_NEIGHBOR_CELL_MEAS_MODULE */

#if defined(CONFIG_LTE_LC_MODEM_SLEEP_MODULE)
    case LTE_LC_EVT_MODEM_SLEEP_EXIT_PRE_WARNING:
        LOG_DBG("New Event: LTE_LC_EVT_MODEM_SLEEP_EXIT_PRE_WARNING");
        break;
    case LTE_LC_EVT_MODEM_SLEEP_EXIT:
        LOG_DBG("New Event: LTE_LC_EVT_MODEM_SLEEP_EXIT");
        break;
    case LTE_LC_EVT_MODEM_SLEEP_ENTER:
        LOG_DBG("New Event: LTE_LC_EVT_MODEM_SLEEP_ENTER");
        break;
#endif /* CONFIG_LTE_LC_MODEM_SLEEP_MODULE */

    case LTE_LC_EVT_MODEM_EVENT:
        LOG_DBG("New Event: LTE_LC_EVT_MODEM_EVENT");
        break;

#if defined(CONFIG_LTE_LC_RAI_MODULE)
    case LTE_LC_EVT_RAI_UPDATE:
        LOG_DBG("New Event: LTE_LC_EVT_RAI_UPDATE");
        break;
#endif /* CONFIG_LTE_LC_RAI_MODULE */
    default:
        __builtin_unreachable();
        // TODO: During prod mode, we should not die here.
        k_oops();
    }
}

int lte_init()
{

    int rc = nrf_modem_lib_init();
    if (rc) {
        LOG_ERR("Modem library initialization failed, error: %d", rc);
        return rc;
    }

    lte_lc_register_handler(lte_lc_event_handler);

    // Enable power-saving mode
    lte_lc_psm_req(true);

    /* We start and stop LTE mode when needed. */
    // LOG_INF("Connecting to LTE network");
    // // TODO: Maybe async so we can try and get GPS faster?
    // //       We won't always be able to connect.

    // if (lte_lc_connect() != 0) {
    //     LOG_ERR("Failed to connect to LTE network");
    //     return -1;
    // }

    // LOG_INF("Connected to LTE network");

    return 0;
}

int lte_set_modem_mode(ModemMode mode)
{
    int rc = 0;
    switch (mode) {
    case ModemMode::connect_lte:
        // This "Activates LTE without modifying GNSS"
        rc = lte_lc_func_mode_set(LTE_LC_FUNC_MODE_ACTIVATE_LTE);

        if (rc != 0) {
            LOG_ERR("Failed to set modem to LTE Func Mode, rc = %d", rc);
            break;
        }

        // TODO: This really should not be K_FOREVER
        k_sem_take(&network_connected, K_FOREVER);

        // In the GNSS sample, with the comment:
        // Wait for a while, because with IPv4v6 PDN the IPv6 activation takes a bit more time.
        k_sleep(K_SECONDS(1));

        break;
    case ModemMode::deactivate_lte:
        rc = lte_lc_func_mode_set(LTE_LC_FUNC_MODE_DEACTIVATE_LTE);

        if (rc != 0) {
            LOG_ERR("Failed to deactivate LTE Func Mode, rc = %d", rc);
            break;
        }

        break;

    case ModemMode::connect_gps:
        rc = lte_lc_func_mode_set(LTE_LC_FUNC_MODE_ACTIVATE_GNSS);

        if (rc != 0) {
            LOG_ERR("Failed to activate GPS Func Mode, rc = %d", rc);
            break;
        }

        break;
    case ModemMode::deactivate_gps:
        rc = lte_lc_func_mode_set(LTE_LC_FUNC_MODE_DEACTIVATE_GNSS);

        if (rc != 0) {
            LOG_ERR("Failed to deactivate GPS Func Mode, rc = %d", rc);
            break;
        }

        break;
    }

    return rc;
}