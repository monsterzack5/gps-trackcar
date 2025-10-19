#include "gps.h"

#include <modem/lte_lc.h>
#include <nrf_modem_gnss.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "lte_manager.h"
#include "networking.h"

LOG_MODULE_REGISTER(gps, LOG_LEVEL_DBG);

static int64_t last_uptime_sent = 0;

static void print_flags(const nrf_modem_gnss_pvt_data_frame& pvt_data)
{
    if (pvt_data.flags & NRF_MODEM_GNSS_PVT_FLAG_DEADLINE_MISSED) {
        printf("GNSS operation blocked by LTE\n");
    }
    if (pvt_data.flags & NRF_MODEM_GNSS_PVT_FLAG_NOT_ENOUGH_WINDOW_TIME) {
        printf("Insufficient GNSS time windows\n");
    }
    if (pvt_data.flags & NRF_MODEM_GNSS_PVT_FLAG_SLEEP_BETWEEN_PVT) {
        printf("Sleep period(s) between PVT notifications\n");
    }
    if (pvt_data.flags & NRF_MODEM_GNSS_PVT_FLAG_SCHED_DOWNLOAD) {
        printf("Scheduled navigation data download\n");
    }
}

static void gnss_event_handler(int event)
{
    // TODO: Move to print func
    switch (event) {
    case NRF_MODEM_GNSS_EVT_PVT:
        LOG_DBG("New Event: NRF_MODEM_GNSS_EVT_PVT");
        break;
    case NRF_MODEM_GNSS_EVT_FIX:
        LOG_DBG("New Event: NRF_MODEM_GNSS_EVT_FIX");
        break;
    case NRF_MODEM_GNSS_EVT_NMEA:
        LOG_DBG("New Event: NRF_MODEM_GNSS_EVT_NMEA");
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

    // TODO: handle better
    int rc = 0;

    switch (event) {
    case NRF_MODEM_GNSS_EVT_NMEA: {

        // Ignore just NMEA Bytes for now, focus on PVT (PVT Packets contain post-processed NMEA bytes)
        break;
        // We got NMEA Data!
        // TODO: put that bad boy into a message queue
        // For now, just print it.

        // evil
        nrf_modem_gnss_nmea_data_frame frame_holder;

        rc = nrf_modem_gnss_read(&frame_holder, sizeof(frame_holder), NRF_MODEM_GNSS_DATA_NMEA);
        if (rc != 0) {
            LOG_ERR("Got an event for EVT_NMEA, but couldn't grab data!");
            break;
        }

        // LOG_DBG("----------\n%s\n----------\n", frame_holder.nmea_str);
        printk("NMEA Data: %s", frame_holder.nmea_str);

        break;
    }

    case NRF_MODEM_GNSS_EVT_PVT: {

        nrf_modem_gnss_pvt_data_frame pvt_frame;

        int pvt_rc = nrf_modem_gnss_read(&pvt_frame, sizeof(pvt_frame), event);

        if (pvt_rc != 0) {
            LOG_ERR("Failed to get pvt data!, rc = %d", pvt_rc);
            break;
        }

        print_flags(pvt_frame);

        if (pvt_rc == 0 && pvt_frame.flags & NRF_MODEM_GNSS_PVT_FLAG_FIX_VALID) {

            if (last_uptime_sent + (int64_t)60000 > k_uptime_get()) {
                send_packet(pvt_frame);
                last_uptime_sent = k_uptime_get();
                LOG_INF("Sent Packet!");
            }
        }

        break;
    }

    case NRF_MODEM_GNSS_EVT_FIX: {
        // nrf_modem_gnss_nmea_data_frame frame_holder;

        // rc = nrf_modem_gnss_read(&frame_holder, sizeof(frame_holder), NRF_MODEM_GNSS_DATA_NMEA);
        // if (rc != 0) {
        //     LOG_ERR("Got a FIX event for EVT_NMEA, but couldn't grab data!");
        //     break;
        // }

        // // LOG_DBG("----------\n%s\n----------\n", frame_holder.nmea_str);
        // printk("FIX Data: %s", frame_holder.nmea_str);

        break;
    }

    default:
        break;
    }
}

int gps_init()
{
    int rc = 0;
    rc = lte_lc_func_mode_set(LTE_LC_FUNC_MODE_ACTIVATE_GNSS);

    if (rc != 0) {
        // TODO: Log here or in super::?
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

    // TODO: Don't think we need this?
    /* Make QZSS satellites visible in the NMEA output. */
    // if (nrf_modem_gnss_qzss_nmea_mode_set(NRF_MODEM_GNSS_QZSS_NMEA_MODE_CUSTOM) != 0) {
    //     LOG_WRN("Failed to enable custom QZSS NMEA mode");
    // }

    uint8_t use_case = NRF_MODEM_GNSS_USE_CASE_MULTIPLE_HOT_START;

    if (nrf_modem_gnss_use_case_set(use_case) != 0) {
        LOG_WRN("Failed to set GNSS use case");
    }

    // TODO:
    // Timeout in seconds for tracking, should be set from kconfig

    // Using these defaults will give us continuous tracking
    uint16_t fix_retry = 0;
    uint16_t fix_interval = 0;

    fix_retry = 120;
    fix_interval = 120;

    if (nrf_modem_gnss_fix_retry_set(fix_retry) != 0) {
        LOG_ERR("Failed to set GNSS fix retry");
        return -1;
    }

    if (nrf_modem_gnss_fix_interval_set(fix_interval) != 0) {
        LOG_ERR("Failed to set GNSS fix interval");
        return -1;
    }

    if (nrf_modem_gnss_start() != 0) {
        LOG_ERR("Failed to start GNSS");
        return -1;
    }

    int prio_rc = nrf_modem_gnss_prio_mode_enable();

    if (prio_rc != 0) {
        LOG_ERR("Failed to set GPS Priority Mode");
    }

    LOG_INF("GPS Initalized");

    return 0;
}