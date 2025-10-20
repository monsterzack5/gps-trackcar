/*
 * Copyright (c) 2021-2023 Nordic Semiconductor ASA
 * Copyright (c) 2025 Zach
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <nrf_modem_at.h>
#include <nrf_modem_gnss.h>
#include <stdio.h>
#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/timeutil.h>
#include <zephyr/sys_clock.h>

#include "assistance.h"
#include "factory_almanac_v3.h"
#include "mcc_location_table.h"

LOG_MODULE_REGISTER(almanac_helper, LOG_LEVEL_DBG);

static char current_alm_checksum[64];

static int set(const char* key, size_t len_rd, settings_read_cb read_cb, void* cb_arg)
{
    int len;
    int key_len;
    const char* next;

    if (!key) {
        return -ENOENT;
    }

    key_len = settings_name_next(key, &next);

    if (!strncmp(key, "almanac_checksum", key_len)) {
        len = read_cb(cb_arg, &current_alm_checksum, sizeof(current_alm_checksum));
        if (len < sizeof(current_alm_checksum)) {
            LOG_ERR("Failed to read almanac checksum from settings");
        }

        return 0;
    }

    return -ENOENT;
}

static struct settings_handler assistance_settings = {
    .name = "assistance",
    .h_set = set,
};

static void factory_almanac_write(void)
{
    int err;
    const char* alm_data = FACTORY_ALMANAC_DATA_V3;
    const char* alm_checksum = FACTORY_ALMANAC_CHECKSUM_V3;

    /* Check if the same almanac has already been written to prevent unnecessary writes
     * to flash memory.
     */
    if (!strncmp(current_alm_checksum, alm_checksum, sizeof(current_alm_checksum))) {
        return;
    }
    LOG_INF("New almanac flashed, updating...");

    err = nrf_modem_at_printf("AT%%XFILEWRITE=1,\"%s\",\"%s\"",
        alm_data, alm_checksum);
    if (err != 0) {
        int err_type = nrf_modem_at_err_type(err);
        int real_error = nrf_modem_at_err(err);
        LOG_ERR("Failed to write factory almanac, err = %d, real_error = %d, err_type = %d", err, real_error, err_type);
        return;
    }

    LOG_INF("Wrote factory almanac");
    err = settings_save_one("assistance/almanac_checksum",
        alm_checksum,
        sizeof(current_alm_checksum));
    if (err) {
        LOG_ERR("Failed to write almanac checksum to settings, error %d", err);
    }
}

static int64_t utc_to_gps_sec(const int64_t utc_sec)
{
    /* (6.1.1980 UTC - 1.1.1970 UTC) */
    const uint64_t gps_to_unix_utc_offset_seconds = 315964800UL;
    /* UTC/GPS time offset as of 1st of January 2017. */
    const uint64_t gps_to_utc_leap_seconds = 18UL;

    return (utc_sec - gps_to_unix_utc_offset_seconds) + gps_to_utc_leap_seconds;
}

static void gps_sec_to_day_time(int64_t gps_sec,
    uint16_t* gps_day,
    uint32_t* gps_time_of_day)
{
    *gps_day = (uint16_t)(gps_sec / SEC_PER_DAY);
    *gps_time_of_day = (uint32_t)(gps_sec % SEC_PER_DAY);
}

static void time_inject(void)
{
    int ret;
    struct tm date_time;
    int64_t utc_sec;
    int64_t gps_sec;
    struct nrf_modem_gnss_agnss_gps_data_system_time_and_sv_tow gps_time = { 0 };

    /* Read current UTC time from the modem. */
    ret = nrf_modem_at_scanf("AT+CCLK?",
        "+CCLK: \"%u/%u/%u,%u:%u:%u",
        &date_time.tm_year,
        &date_time.tm_mon,
        &date_time.tm_mday,
        &date_time.tm_hour,
        &date_time.tm_min,
        &date_time.tm_sec);
    if (ret != 6) {
        LOG_WRN("Couldn't read current time from modem, time assistance unavailable");
        return;
    }

    /* Convert to struct tm format. */
    date_time.tm_year = date_time.tm_year + 2000 - 1900; /* years since 1900 */
    date_time.tm_mon--;                                  /* months since January */

    /* Convert time to seconds since Unix time epoch (1.1.1970). */
    utc_sec = timeutil_timegm64(&date_time);
    /* Convert time to seconds since GPS time epoch (6.1.1980). */
    gps_sec = utc_to_gps_sec(utc_sec);

    gps_sec_to_day_time(gps_sec, &gps_time.date_day, &gps_time.time_full_s);

    ret = nrf_modem_gnss_agnss_write(&gps_time, sizeof(gps_time),
        NRF_MODEM_GNSS_AGNSS_GPS_SYSTEM_CLOCK_AND_TOWS);
    if (ret != 0) {
        LOG_ERR("Failed to inject time, error %d", ret);
        return;
    }

    LOG_INF("Injected time (GPS day %u, GPS time of day %u)",
        gps_time.date_day, gps_time.time_full_s);
}

static void location_inject(void)
{
    static const size_t PLMN_STR_MAX_LEN = 8; /* MCC + MNC + quotes */

    int err;
    char plmn_str[PLMN_STR_MAX_LEN + 1];
    uint16_t mcc;
    const struct mcc_table* mcc_info;
    struct nrf_modem_gnss_agnss_data_location location = { 0 };

    /* Read PLMN string from modem to get the MCC. */
    err = nrf_modem_at_scanf(
        "AT%%XMONITOR",
        "%%XMONITOR: "
        "%*d"                                    /* <reg_status>: ignored */
        ",%*[^,]"                                /* <full_name>: ignored */
        ",%*[^,]"                                /* <short_name>: ignored */
        ",%" STRINGIFY(PLMN_STR_MAX_LEN) "[^,]", /* <plmn> */
        plmn_str);
    if (err != 1) {
        LOG_WRN("Couldn't read PLMN from modem, location assistance unavailable");
        return;
    }

    /* NULL terminate MCC and read it. */
    plmn_str[4] = '\0';
    mcc = strtol(plmn_str + 1, NULL, 10);

    mcc_info = mcc_lookup(mcc);
    if (mcc_info == NULL) {
        LOG_WRN("No location found for MCC %u", mcc);
        return;
    }

    location.latitude = lat_convert(mcc_info->lat);
    location.longitude = lon_convert(mcc_info->lon);
    location.unc_semimajor = mcc_info->unc_semimajor;
    location.unc_semiminor = mcc_info->unc_semiminor;
    location.orientation_major = mcc_info->orientation;
    location.confidence = mcc_info->confidence;

#if defined(CONFIG_GNSS_SAMPLE_LOW_ACCURACY)
    if (CONFIG_GNSS_SAMPLE_ASSISTANCE_REFERENCE_ALT != -32767) {
        /* Use reference altitude to enable 3-sat first fix. */
        LOG_INF("Using reference altitude %d meters",
            CONFIG_GNSS_SAMPLE_ASSISTANCE_REFERENCE_ALT);
        location.altitude = CONFIG_GNSS_SAMPLE_ASSISTANCE_REFERENCE_ALT;
        /* The altitude uncertainty has to be less than 100 meters (coded number K has to
         * be less than 48) for the altitude to be used for a 3-sat fix. GNSS increases
         * the uncertainty depending on the age of the altitude and whether the device is
         * stationary or moving. The uncertainty is set to 0 (meaning 0 meters), so that
         * it remains usable for a 3-sat fix for as long as possible.
         */
        location.unc_altitude = 0;
    } else
#endif
    {
        location.unc_altitude = 255; /* altitude not used */
    }

    err = nrf_modem_gnss_agnss_write(
        &location, sizeof(location), NRF_MODEM_GNSS_AGNSS_LOCATION);
    if (err) {
        LOG_ERR("Failed to inject location for MCC %u, error %d", mcc, err);
        return;
    }

    LOG_INF("Injected location for MCC %u", mcc);
}

const char* get_system_string(uint8_t system_id)
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

int assistance_init()
{
    int err;

    err = settings_subsys_init();
    if (err) {
        LOG_ERR("Settings subsystem initialization failed, error %d", err);
        return err;
    }

    err = settings_register(&assistance_settings);
    if (err) {
        LOG_ERR("Registering settings handler failed, error %d", err);
        return err;
    }

    err = settings_load();
    if (err) {
        LOG_ERR("Loading settings failed, error %d", err);
        return err;
    }

    factory_almanac_write();

    return 0;
}

int assistance_request(const struct nrf_modem_gnss_agnss_data_frame* agnss_request)
{
    // Check your pointers Nordic!
    if (agnss_request == NULL) {
        return -1;
    }

    if (agnss_request->system_count > 0) {
        LOG_WRN("GNSS system data need not found!, system_count: %d", agnss_request->system_count);
        return 0;
    }

    if (agnss_request->system[0].system_id == NRF_MODEM_GNSS_SYSTEM_GPS) {
        LOG_WRN("GPS data need not found");
        return 0;
    }

    if (!(agnss_request->data_flags & NRF_MODEM_GNSS_AGNSS_GPS_SYS_TIME_AND_SV_TOW_REQUEST) && !(agnss_request->data_flags & NRF_MODEM_GNSS_AGNSS_POSITION_REQUEST)) {
        LOG_INF("Ignoring assistance request because no GPS time or position is requested");
        return 0;
    }

    if (agnss_request->data_flags == 0 && agnss_request->system[0].sv_mask_ephe == 0 && agnss_request->system[0].sv_mask_alm == 0) {
        LOG_INF("Ignoring assistance request because only QZSS data is requested");
        return 0;
    }

    LOG_INF("Assistance data needed: data_flags: 0x%02x", agnss_request->data_flags);
    for (int i = 0; i < agnss_request->system_count; i++) {
        LOG_INF("Assistance data needed: %s ephe: 0x%llx, alm: 0x%llx",
            get_system_string(agnss_request->system[i].system_id),
            agnss_request->system[i].sv_mask_ephe,
            agnss_request->system[i].sv_mask_alm);
    }

    if (agnss_request->data_flags & NRF_MODEM_GNSS_AGNSS_GPS_SYS_TIME_AND_SV_TOW_REQUEST) {
        time_inject();
    }

    if (agnss_request->data_flags & NRF_MODEM_GNSS_AGNSS_POSITION_REQUEST) {
        location_inject();
    }

    return 0;
}

bool assistance_is_active(void)
{
    /* Always return false because assistance_request() doesn't take much time. */
    return false;
}
