#include "gps.h"

#include <modem/lte_lc.h>
#include <nrf_modem_gnss.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "assistance.h"
#include "battery.h"
#include "modem_antenna_signal.h"
#include "network_info.h"
#include "network_requests.h"

LOG_MODULE_REGISTER(gps, CONFIG_TRACCAR_DEFAULT_LOG_LEVEL);

// ---
// NOTE: GPS Timeout and Restart
// - On NRF_MODEM_GNSS_EVT_PERIODIC_WAKEUP
//     - Start a timer for GPS_TIMEOUT
//     - If the timer expires, stop GPS for GPS_RESTART_DELAY
// - Reset timer if/when NRF_MODEM_GNSS_EVT_SLEEP_AFTER_FIX

static const k_timeout_t GPS_TIMEOUT = K_SECONDS(CONFIG_GPS_TIMEOUT_SECONDS);
static const k_timeout_t GPS_RESTART_DELAY = K_SECONDS(CONFIG_GPS_RESTART_DELAY_SECONDS);

void handle_gps_timed_out_fn(k_work* work);
void handle_restart_gps_fn(k_work* work);

K_WORK_DELAYABLE_DEFINE(gps_timed_out_work, handle_gps_timed_out_fn);
K_WORK_DELAYABLE_DEFINE(restart_gps_work, handle_restart_gps_fn);

// ---

K_THREAD_STACK_DEFINE(gps_work_queue_stack, (1024 * 3));
static k_work_q gps_work_queue;

// For handling GPS fix events
void pvt_data_handler_fn(k_work* work);
struct pvt_work_struct {
    k_work work;
    nrf_modem_gnss_pvt_data_frame pvt_frame;
};
static pvt_work_struct pvt_data_work;

// For handling assistance data
struct assistance_work_struct {
    k_work work;
    nrf_modem_gnss_agnss_data_frame agnss_frame;
};
static assistance_work_struct assistance_work;

// ----

void pvt_data_handler_fn(k_work* work_item)
{
    static int64_t last_uptime_sent = 0;

    pvt_work_struct* data = CONTAINER_OF(work_item, struct pvt_work_struct, work);

    int64_t uptime = k_uptime_get();
    if (uptime - last_uptime_sent >= (int64_t)30000) {
        send_gps_update(data->pvt_frame);
        last_uptime_sent = uptime;
    } else {
        LOG_WRN("Not sending request due to timeout");
    }
}

void assistance_handler_fn(k_work* work_item)
{
    assistance_work_struct* data = CONTAINER_OF(work_item, struct assistance_work_struct, work);

    int err = assistance_request(&data->agnss_frame);
    if (err) {
        LOG_ERR("Failed to request assistance data, err = %d", err);
    }
}

void handle_gps_timed_out_fn(k_work* work)
{
    ARG_UNUSED(work);
    LOG_WRN("GPS Timed out!");
    nrf_modem_gnss_stop();
    k_work_schedule_for_queue(&gps_work_queue, &restart_gps_work, GPS_RESTART_DELAY);
}

void handle_restart_gps_fn(k_work* work)
{
    ARG_UNUSED(work);
    LOG_INF("Starting GPS again");
    nrf_modem_gnss_start();
}

static void check_for_modem_pvt_errors(const nrf_modem_gnss_pvt_data_frame& frame)
{
    if (frame.flags & NRF_MODEM_GNSS_PVT_FLAG_DEADLINE_MISSED) {
        LOG_WRN("!! Missed Deadline");
    }
    if (frame.flags & NRF_MODEM_GNSS_PVT_FLAG_NOT_ENOUGH_WINDOW_TIME) {
        LOG_WRN("!! Not Enough Window Time");
    }
    if (frame.flags & NRF_MODEM_GNSS_SV_FLAG_UNHEALTHY) {
        LOG_WRN("!! Sat Unhealthy");
    }
}

static void print_satellite_stats(const nrf_modem_gnss_pvt_data_frame& pvt_data)
{
    uint8_t tracked = 0;
    uint8_t in_fix = 0;
    uint8_t unhealthy = 0;

    for (int i = 0; i < NRF_MODEM_GNSS_MAX_SATELLITES; ++i) {
        if (pvt_data.sv[i].sv > 0) {
            tracked++;

            if (pvt_data.sv[i].flags & NRF_MODEM_GNSS_SV_FLAG_USED_IN_FIX) {
                in_fix++;
            }

            if (pvt_data.sv[i].flags & NRF_MODEM_GNSS_SV_FLAG_UNHEALTHY) {
                unhealthy++;
            }
        }
    }

    LOG_DBG("Tracking: %2d Using: %2d Unhealthy: %d", tracked, in_fix, unhealthy);
}

static void print_gnss_event(int event)
{
    switch (event) {
    case NRF_MODEM_GNSS_EVT_PVT:
        // LOG_DBG("New Event: NRF_MODEM_GNSS_EVT_PVT");
        break;
    case NRF_MODEM_GNSS_EVT_FIX:
        LOG_DBG("New Event: NRF_MODEM_GNSS_EVT_FIX");
        break;
    case NRF_MODEM_GNSS_EVT_NMEA:
        // LOG_DBG("New Event: NRF_MODEM_GNSS_EVT_NMEA");
        break;
    case NRF_MODEM_GNSS_EVT_AGNSS_REQ:
        LOG_DBG("New Event: NRF_MODEM_GNSS_EVT_AGNSS_REQ");
        break;
    case NRF_MODEM_GNSS_EVT_BLOCKED:
        LOG_DBG("New Event: NRF_MODEM_GNSS_EVT_BLOCKED");
        break;
    case NRF_MODEM_GNSS_EVT_UNBLOCKED:
        LOG_DBG("New Event: NRF_MODEM_GNSS_EVT_UNBLOCKED");
        break;
    case NRF_MODEM_GNSS_EVT_PERIODIC_WAKEUP:
        LOG_DBG("New Event: NRF_MODEM_GNSS_EVT_PERIODIC_WAKEUP");
        break;
    case NRF_MODEM_GNSS_EVT_SLEEP_AFTER_TIMEOUT:
        LOG_DBG("New Event: NRF_MODEM_GNSS_EVT_SLEEP_AFTER_TIMEOUT");
        break;
    case NRF_MODEM_GNSS_EVT_SLEEP_AFTER_FIX:
        LOG_DBG("New Event: NRF_MODEM_GNSS_EVT_SLEEP_AFTER_FIX");
        break;
    case NRF_MODEM_GNSS_EVT_REF_ALT_EXPIRED:
        LOG_DBG("New Event: NRF_MODEM_GNSS_EVT_REF_ALT_EXPIRED");
        break;
    }
}

static void gnss_event_handler(int event)
{
    print_gnss_event(event);

    // TODO: I don't remember the events we get from scheduled downloads
    // Make sure we're handling those properly with our poller.
    switch (event) {
    case NRF_MODEM_GNSS_EVT_PERIODIC_WAKEUP:
        k_poll_signal_reset(&modem_is_free_signal);
        k_work_schedule_for_queue(&gps_work_queue, &gps_timed_out_work, GPS_TIMEOUT);
        break;

    case NRF_MODEM_GNSS_EVT_SLEEP_AFTER_FIX:
        k_poll_signal_raise(&modem_is_free_signal, 0);
        k_work_cancel_delayable(&gps_timed_out_work);
        break;

    case NRF_MODEM_GNSS_EVT_PVT: {
        static uint32_t pvt_events_handled = 0;
        pvt_events_handled += 1;

        if (!k_work_delayable_is_pending(&gps_timed_out_work)) {
            LOG_WRN("Timeout work not running while getting PVT events, starting");
            k_work_schedule_for_queue(&gps_work_queue, &gps_timed_out_work, GPS_TIMEOUT);
        }

        if (pvt_events_handled % 100 == 0) {
            LOG_DBG("Handled %u PVT events, so far", pvt_events_handled);
        }

        nrf_modem_gnss_pvt_data_frame pvt_frame;

        int pvt_rc = nrf_modem_gnss_read(&pvt_frame, sizeof(pvt_frame), event);

        check_for_modem_pvt_errors(pvt_frame);
        if (pvt_events_handled % 30 == 0) {
            print_satellite_stats(pvt_frame);
        }

        if (pvt_rc == 0 && (pvt_frame.flags & NRF_MODEM_GNSS_PVT_FLAG_FIX_VALID)) {
            pvt_data_work.pvt_frame = pvt_frame;
            k_work_submit_to_queue(&gps_work_queue, &pvt_data_work.work);
        }

        break;
    }

    // Modem is requesting assistance data.
    case NRF_MODEM_GNSS_EVT_AGNSS_REQ: {
        int retval = nrf_modem_gnss_read(&assistance_work.agnss_frame, sizeof(assistance_work.agnss_frame), NRF_MODEM_GNSS_DATA_AGNSS_REQ);
        if (retval == 0) {
            k_work_submit_to_queue(&gps_work_queue, &assistance_work.work);
        }
        break;
    }

    default:
        break;
    }
}

static void workqueue_init()
{
    struct k_work_queue_config cfg = {
        .name = "gps_work_queue",
        .no_yield = false
    };

    k_work_init(&pvt_data_work.work, pvt_data_handler_fn);
    k_work_init(&assistance_work.work, assistance_handler_fn);
    k_work_queue_init(&gps_work_queue);
    k_work_queue_start(&gps_work_queue, gps_work_queue_stack, K_THREAD_STACK_SIZEOF(gps_work_queue_stack), 10, &cfg);
}

int gps_start()
{
    int rc = nrf_modem_gnss_start();
    if (rc != 0) {
        LOG_ERR("Failed to start GNSS, rc = %d", rc);
        return -1;
    }

    return rc;
}

int gps_init()
{
    int rc = 0;

    workqueue_init();
    assistance_init();

    // Enable GPS mode in the modem
    rc = lte_lc_func_mode_set(LTE_LC_FUNC_MODE_ACTIVATE_GNSS);

    if (rc != 0) {
        LOG_ERR("Failed to set GPS mode in modem, rc = %d", rc);
        return -1;
    }

    if (nrf_modem_gnss_event_handler_set(gnss_event_handler) != 0) {
        LOG_ERR("Failed to set GNSS event handler");
        return -1;
    }

    uint16_t nmea_mask = NRF_MODEM_GNSS_NMEA_RMC_MASK | NRF_MODEM_GNSS_NMEA_GGA_MASK | NRF_MODEM_GNSS_NMEA_GLL_MASK | NRF_MODEM_GNSS_NMEA_GSA_MASK | NRF_MODEM_GNSS_NMEA_GSV_MASK;
    int mask_rc = nrf_modem_gnss_nmea_mask_set(nmea_mask);
    if (mask_rc != 0) {
        LOG_ERR("Failed to set GNSS NMEA mask, rc = %d", mask_rc);
        return -1;
    }

    uint8_t use_case = NRF_MODEM_GNSS_USE_CASE_MULTIPLE_HOT_START;
    if (nrf_modem_gnss_use_case_set(use_case) != 0) {
        LOG_WRN("Failed to set GNSS use case");
    }

    // TODO:
    // Using these defaults will give us continuous tracking
    uint16_t fix_retry = 0;
    uint16_t fix_interval = 0;

    fix_retry = 120;
    fix_interval = 120;

    // TODO: Set these based on if we're moving, if we're stopped, battery low, etc...
    if (nrf_modem_gnss_fix_retry_set(fix_retry) != 0) {
        LOG_ERR("Failed to set GNSS fix retry");
        return -1;
    }

    if (nrf_modem_gnss_fix_interval_set(fix_interval) != 0) {
        LOG_ERR("Failed to set GNSS fix interval");
        return -1;
    }

    int gps_rc = nrf_modem_gnss_start();
    if (gps_rc != 0) {
        LOG_ERR("Failed to start GPS!, rc = %d", gps_rc);
        return gps_rc;
    }

    LOG_INF("GPS Initalized");

    return 0;
}