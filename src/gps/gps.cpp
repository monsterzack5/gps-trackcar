/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include "gps.h"

#include <date_time.h>
#include <math.h>
#include <modem/lte_lc.h>
#include <modem/nrf_modem_lib.h>
#include <nrf_modem_at.h>
#include <nrf_modem_gnss.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

// TODO: Not this .. stuff
#include "../lte/lte_manager.h"

LOG_MODULE_REGISTER(gps, LOG_LEVEL_DBG);

#define PI 3.14159265358979323846
#define EARTH_RADIUS_METERS (6371.0 * 1000.0)

static struct k_work_q gnss_work_q;

#define GNSS_WORKQ_THREAD_STACK_SIZE 2304
#define GNSS_WORKQ_THREAD_PRIORITY 5

K_THREAD_STACK_DEFINE(gnss_workq_stack_area, GNSS_WORKQ_THREAD_STACK_SIZE);

// Use the nordic factory almanac
#include "assistance.h"
static struct nrf_modem_gnss_agnss_data_frame last_agnss;
static struct k_work agnss_data_get_work;
static volatile bool requesting_assistance;

static const char update_indicator[] = { '\\', '|', '/', '-' };

static struct nrf_modem_gnss_pvt_data_frame last_pvt;
static uint64_t fix_timestamp;

/* Reference position. */
static bool ref_used;
static double ref_latitude;
static double ref_longitude;

K_MSGQ_DEFINE(nmea_queue, sizeof(struct nrf_modem_gnss_nmea_data_frame*), 10, 4);
static K_SEM_DEFINE(pvt_data_sem, 0, 1);

static struct k_poll_event events[2] = {
    K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE,
        K_POLL_MODE_NOTIFY_ONLY,
        &pvt_data_sem, 0),
    K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_MSGQ_DATA_AVAILABLE,
        K_POLL_MODE_NOTIFY_ONLY,
        &nmea_queue, 0),
};

BUILD_ASSERT(IS_ENABLED(CONFIG_LTE_NETWORK_MODE_LTE_M_GPS) || IS_ENABLED(CONFIG_LTE_NETWORK_MODE_NBIOT_GPS) || IS_ENABLED(CONFIG_LTE_NETWORK_MODE_LTE_M_NBIOT_GPS),
    "CONFIG_LTE_NETWORK_MODE_LTE_M_GPS, "
    "CONFIG_LTE_NETWORK_MODE_NBIOT_GPS or "
    "CONFIG_LTE_NETWORK_MODE_LTE_M_NBIOT_GPS must be enabled");

// BUILD_ASSERT((sizeof(CONFIG_GNSS_REFERENCE_LATITUDE) == 1 && sizeof(CONFIG_GNSS_REFERENCE_LONGITUDE) == 1) || (sizeof(CONFIG_GNSS_REFERENCE_LATITUDE) > 1 && sizeof(CONFIG_GNSS_REFERENCE_LONGITUDE) > 1),
//     "CONFIG_GNSS_REFERENCE_LATITUDE and "
//     "CONFIG_GNSS_REFERENCE_LONGITUDE must be both either set or empty");

/* Returns the distance between two coordinates in meters. The distance is calculated using the
 * haversine formula.
 */
static double distance_calculate(double lat1, double lon1,
    double lat2, double lon2)
{
    double d_lat_rad = (lat2 - lat1) * PI / 180.0;
    double d_lon_rad = (lon2 - lon1) * PI / 180.0;

    double lat1_rad = lat1 * PI / 180.0;
    double lat2_rad = lat2 * PI / 180.0;

    double a = pow(sin(d_lat_rad / 2), 2) + pow(sin(d_lon_rad / 2), 2) * cos(lat1_rad) * cos(lat2_rad);

    double c = 2 * asin(sqrt(a));

    return EARTH_RADIUS_METERS * c;
}

static void print_distance_from_reference(struct nrf_modem_gnss_pvt_data_frame* pvt_data)
{
    if (!ref_used) {
        return;
    }

    double distance = distance_calculate(pvt_data->latitude, pvt_data->longitude,
        ref_latitude, ref_longitude);

    if (IS_ENABLED(CONFIG_GNSS_MODE_TTFF_TEST)) {
        LOG_INF("Distance from reference: %.01f", distance);
    } else {
        printf("\nDistance from reference: %.01f\n", distance);
    }
}

// TODO: Refactor this to not use malloc
static void gnss_event_handler(int event)
{
    int retval;
    struct nrf_modem_gnss_nmea_data_frame* nmea_data;

    switch (event) {
    case NRF_MODEM_GNSS_EVT_PVT:
        retval = nrf_modem_gnss_read(&last_pvt, sizeof(last_pvt), NRF_MODEM_GNSS_DATA_PVT);
        if (retval == 0) {
            k_sem_give(&pvt_data_sem);
        }
        break;

    case NRF_MODEM_GNSS_EVT_NMEA:
        nmea_data = reinterpret_cast<nrf_modem_gnss_nmea_data_frame*>((sizeof(struct nrf_modem_gnss_nmea_data_frame)));
        if (nmea_data == NULL) {
            LOG_ERR("Failed to allocate memory for NMEA");
            break;
        }

        retval = nrf_modem_gnss_read(nmea_data,
            sizeof(struct nrf_modem_gnss_nmea_data_frame),
            NRF_MODEM_GNSS_DATA_NMEA);
        if (retval == 0) {
            retval = k_msgq_put(&nmea_queue, &nmea_data, K_NO_WAIT);
        }

        if (retval != 0) {
            k_free(nmea_data);
        }
        break;

    case NRF_MODEM_GNSS_EVT_AGNSS_REQ:
#if !defined(CONFIG_GNSS_ASSISTANCE_NONE)
        retval = nrf_modem_gnss_read(&last_agnss,
            sizeof(last_agnss),
            NRF_MODEM_GNSS_DATA_AGNSS_REQ);
        if (retval == 0) {
            k_work_submit_to_queue(&gnss_work_q, &agnss_data_get_work);
        }
#endif /* !CONFIG_GNSS_ASSISTANCE_NONE */
        break;

    default:
        break;
    }
}

#if !defined(CONFIG_GNSS_ASSISTANCE_NONE)
static const char* get_system_string(uint8_t system_id)
{
    switch (system_id) {
    case NRF_MODEM_GNSS_SYSTEM_INVALID:
        return "invalid";

    case NRF_MODEM_GNSS_SYSTEM_GPS:
        return "GPS";

    case NRF_MODEM_GNSS_SYSTEM_QZSS:
        return "QZSS";

    default:
        return "unknown";
    }
}

static void agnss_data_get_work_fn(struct k_work* item)
{
    ARG_UNUSED(item);

    int err;

    /* GPS data need is always expected to be present and first in list. */
    __ASSERT(last_agnss.system_count > 0,
        "GNSS system data need not found");
    __ASSERT(last_agnss.system[0].system_id == NRF_MODEM_GNSS_SYSTEM_GPS,
        "GPS data need not found");

#    if defined(CONFIG_GNSS_ASSISTANCE_MINIMAL)
    /* With minimal assistance, the request should be ignored if no GPS time or position
     * is requested.
     */
    if (!(last_agnss.data_flags & NRF_MODEM_GNSS_AGNSS_GPS_SYS_TIME_AND_SV_TOW_REQUEST) && !(last_agnss.data_flags & NRF_MODEM_GNSS_AGNSS_POSITION_REQUEST)) {
        LOG_INF("Ignoring assistance request because no GPS time or position is requested");
        return;
    }
#    endif /* CONFIG_GNSS_ASSISTANCE_MINIMAL */

    if (last_agnss.data_flags == 0 && last_agnss.system[0].sv_mask_ephe == 0 && last_agnss.system[0].sv_mask_alm == 0) {
        LOG_INF("Ignoring assistance request because only QZSS data is requested");
        return;
    }

    requesting_assistance = true;

    LOG_INF("Assistance data needed: data_flags: 0x%02x", last_agnss.data_flags);
    for (int i = 0; i < last_agnss.system_count; i++) {
        LOG_INF("Assistance data needed: %s ephe: 0x%llx, alm: 0x%llx",
            get_system_string(last_agnss.system[i].system_id),
            last_agnss.system[i].sv_mask_ephe,
            last_agnss.system[i].sv_mask_alm);
    }

    // TODO: This is ugly
    lte::set_mode(LteMode::Connected);

    err = assistance_request(&last_agnss);
    if (err) {
        LOG_ERR("Failed to request assistance data");
    }
    // TODO: This is ugly
    lte::set_mode(LteMode::Disconnected);

    requesting_assistance = false;
}
#endif /* !CONFIG_GNSS_ASSISTANCE_NONE */

static int workqueue_init(void)
{
    int err = 0;

    struct k_work_queue_config cfg = {
        .name = "gnss_work_q",
        .no_yield = false
    };

    k_work_queue_start(
        &gnss_work_q,
        gnss_workq_stack_area,
        K_THREAD_STACK_SIZEOF(gnss_workq_stack_area),
        GNSS_WORKQ_THREAD_PRIORITY,
        &cfg);

    k_work_init(&agnss_data_get_work, agnss_data_get_work_fn);

    // Setup assistance, currently just uses an almanac for minimal
    // TODO: Support assistance none
    err = assistance_init(&gnss_work_q);

    return err;
}

static int gnss_init_and_start(void)
{

#if defined(CONFIG_GNSS_ASSISTANCE_NONE)
    /* Enable GNSS. */
    if (lte_lc_func_mode_set(LTE_LC_FUNC_MODE_ACTIVATE_GNSS) != 0) {
        LOG_ERR("Failed to activate GNSS functional mode");
        return -1;
    }
#endif /* CONFIG_GNSS_ASSISTANCE_NONE  */

    /* Configure GNSS. */
    if (nrf_modem_gnss_event_handler_set(gnss_event_handler) != 0) {
        LOG_ERR("Failed to set GNSS event handler");
        return -1;
    }

    /* Enable all supported NMEA messages. */
    uint16_t nmea_mask = NRF_MODEM_GNSS_NMEA_RMC_MASK | NRF_MODEM_GNSS_NMEA_GGA_MASK | NRF_MODEM_GNSS_NMEA_GLL_MASK | NRF_MODEM_GNSS_NMEA_GSA_MASK | NRF_MODEM_GNSS_NMEA_GSV_MASK;

    if (nrf_modem_gnss_nmea_mask_set(nmea_mask) != 0) {
        LOG_ERR("Failed to set GNSS NMEA mask");
        return -1;
    }

    /* Make QZSS satellites visible in the NMEA output. */
    if (nrf_modem_gnss_qzss_nmea_mode_set(NRF_MODEM_GNSS_QZSS_NMEA_MODE_CUSTOM) != 0) {
        LOG_WRN("Failed to enable custom QZSS NMEA mode");
    }

    /* This use case flag should always be set. */
    uint8_t use_case = NRF_MODEM_GNSS_USE_CASE_MULTIPLE_HOT_START;

    if (IS_ENABLED(CONFIG_GNSS_MODE_PERIODIC) && !IS_ENABLED(CONFIG_GNSS_ASSISTANCE_NONE)) {
        /* Disable GNSS scheduled downloads when assistance is used. */
        use_case |= NRF_MODEM_GNSS_USE_CASE_SCHED_DOWNLOAD_DISABLE;
    }

    if (nrf_modem_gnss_use_case_set(use_case) != 0) {
        LOG_WRN("Failed to set GNSS use case");
    }

#if defined(CONFIG_GNSS_MODE_CONTINUOUS)
    /* Default to no power saving. */
    uint8_t power_mode = NRF_MODEM_GNSS_PSM_DISABLED;

#    if defined(CONFIG_GNSS_POWER_SAVING_MODERATE)
    power_mode = NRF_MODEM_GNSS_PSM_DUTY_CYCLING_PERFORMANCE;
#    elif defined(CONFIG_GNSS_POWER_SAVING_HIGH)
    power_mode = NRF_MODEM_GNSS_PSM_DUTY_CYCLING_POWER;
#    endif

    if (nrf_modem_gnss_power_mode_set(power_mode) != 0) {
        LOG_ERR("Failed to set GNSS power saving mode");
        return -1;
    }
#endif /* CONFIG_GNSS_MODE_CONTINUOUS */

    /* Default to continuous tracking. */
    uint16_t fix_retry = 0;
    uint16_t fix_interval = 1;

#if defined(CONFIG_GNSS_MODE_PERIODIC)
    fix_retry = CONFIG_GNSS_PERIODIC_TIMEOUT;
    fix_interval = CONFIG_GNSS_PERIODIC_INTERVAL;
#endif

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

    return 0;
}

static bool output_paused(void)
{
    return (requesting_assistance || assistance_is_active());
}

static void print_satellite_stats(struct nrf_modem_gnss_pvt_data_frame* pvt_data)
{
    uint8_t tracked = 0;
    uint8_t in_fix = 0;
    uint8_t unhealthy = 0;

    for (int i = 0; i < NRF_MODEM_GNSS_MAX_SATELLITES; ++i) {
        if (pvt_data->sv[i].sv > 0) {
            tracked++;

            if (pvt_data->sv[i].flags & NRF_MODEM_GNSS_SV_FLAG_USED_IN_FIX) {
                in_fix++;
            }

            if (pvt_data->sv[i].flags & NRF_MODEM_GNSS_SV_FLAG_UNHEALTHY) {
                unhealthy++;
            }
        }
    }

    printf("Tracking: %2d Using: %2d Unhealthy: %d\n", tracked, in_fix, unhealthy);
}

static void print_flags(struct nrf_modem_gnss_pvt_data_frame* pvt_data)
{
    if (pvt_data->flags & NRF_MODEM_GNSS_PVT_FLAG_DEADLINE_MISSED) {
        printf("GNSS operation blocked by LTE\n");
    }
    if (pvt_data->flags & NRF_MODEM_GNSS_PVT_FLAG_NOT_ENOUGH_WINDOW_TIME) {
        printf("Insufficient GNSS time windows\n");
    }
    if (pvt_data->flags & NRF_MODEM_GNSS_PVT_FLAG_SLEEP_BETWEEN_PVT) {
        printf("Sleep period(s) between PVT notifications\n");
    }
    if (pvt_data->flags & NRF_MODEM_GNSS_PVT_FLAG_SCHED_DOWNLOAD) {
        printf("Scheduled navigation data download\n");
    }
}

static void print_fix_data(struct nrf_modem_gnss_pvt_data_frame* pvt_data)
{
    printf("Latitude:          %.06f\n", pvt_data->latitude);
    printf("Longitude:         %.06f\n", pvt_data->longitude);
    printf("Accuracy:          %.01f m\n", (double)pvt_data->accuracy);
    printf("Altitude:          %.01f m\n", (double)pvt_data->altitude);
    printf("Altitude accuracy: %.01f m\n", (double)pvt_data->altitude_accuracy);
    printf("Speed:             %.01f m/s\n", (double)pvt_data->speed);
    printf("Speed accuracy:    %.01f m/s\n", (double)pvt_data->speed_accuracy);
    printf("V. speed:          %.01f m/s\n", (double)pvt_data->vertical_speed);
    printf("V. speed accuracy: %.01f m/s\n", (double)pvt_data->vertical_speed_accuracy);
    printf("Heading:           %.01f deg\n", (double)pvt_data->heading);
    printf("Heading accuracy:  %.01f deg\n", (double)pvt_data->heading_accuracy);
    printf("Date:              %04u-%02u-%02u\n",
        pvt_data->datetime.year,
        pvt_data->datetime.month,
        pvt_data->datetime.day);
    printf("Time (UTC):        %02u:%02u:%02u.%03u\n",
        pvt_data->datetime.hour,
        pvt_data->datetime.minute,
        pvt_data->datetime.seconds,
        pvt_data->datetime.ms);
    printf("PDOP:              %.01f\n", (double)pvt_data->pdop);
    printf("HDOP:              %.01f\n", (double)pvt_data->hdop);
    printf("VDOP:              %.01f\n", (double)pvt_data->vdop);
    printf("TDOP:              %.01f\n", (double)pvt_data->tdop);
}

// ---------------

int init2()
{
    uint8_t cnt = 0;
    struct nrf_modem_gnss_nmea_data_frame* nmea_data;

    LOG_INF("Starting GNSS sample");

    /* Initialize reference coordinates (if used). */
    // if (sizeof(CONFIG_GNSS_REFERENCE_LATITUDE) > 1 && sizeof(CONFIG_GNSS_REFERENCE_LONGITUDE) > 1) {
    //     ref_used = true;
    //     ref_latitude = atof(CONFIG_GNSS_REFERENCE_LATITUDE);
    //     ref_longitude = atof(CONFIG_GNSS_REFERENCE_LONGITUDE);
    // }

    if (workqueue_init() != 0) {
        LOG_ERR("Failed to initialize workqueue");
        return -1;
    }

    if (gnss_init_and_start() != 0) {
        LOG_ERR("Failed to initialize and start GNSS");
        return -1;
    }

    fix_timestamp = k_uptime_get();

    // todo: needs to go into separate thread!
    for (;;) {
        (void)k_poll(events, 2, K_FOREVER);

        if (events[0].state == K_POLL_STATE_SEM_AVAILABLE && k_sem_take(events[0].sem, K_NO_WAIT) == 0) {
            /* New PVT data available */

            /* PVT and NMEA output mode. */

            if (output_paused()) {
                goto handle_nmea;
            }

            printf("\033[1;1H");
            printf("\033[2J");
            print_satellite_stats(&last_pvt);
            print_flags(&last_pvt);
            printf("-----------------------------------\n");

            if (last_pvt.flags & NRF_MODEM_GNSS_PVT_FLAG_FIX_VALID) {
                fix_timestamp = k_uptime_get();
                print_fix_data(&last_pvt);
                print_distance_from_reference(&last_pvt);
            } else {
                printf("Seconds since last fix: %d\n",
                    (uint32_t)((k_uptime_get() - fix_timestamp) / 1000));
                cnt++;
                printf("Searching [%c]\n", update_indicator[cnt % 4]);
            }

            printf("\nNMEA strings:\n\n");
        }

    handle_nmea:
        if (events[1].state == K_POLL_STATE_MSGQ_DATA_AVAILABLE && k_msgq_get(events[1].msgq, &nmea_data, K_NO_WAIT) == 0) {
            /* New NMEA data available */

            if (!output_paused()) {
                printf("%s", nmea_data->nmea_str);
            }
            k_free(nmea_data);
        }

        events[0].state = K_POLL_STATE_NOT_READY;
        events[1].state = K_POLL_STATE_NOT_READY;
    }

    return 0;
}
