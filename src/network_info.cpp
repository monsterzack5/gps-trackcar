#include "network_info.h"

#include <modem/modem_info.h>
#include <stdlib.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(network_info, LOG_LEVEL_DBG);

static char imei[16] = { 0 };

int network_info_init()
{
    // TODO:
    // We should maybe do this with AT Commands?
    // Not sure why this needs to be enabled in prj.conf
    int rc = modem_info_init();
    if (rc != 0) {
        LOG_ERR("modem_info_init failed, rc = %d", rc);
        return -1;
    }

    return 0;
}

const char* get_imei()
{
    static bool did_already_run = false;

    if (did_already_run) {
        return imei;
    }

    // Not sure how to error handle this right now

    int rc = modem_info_string_get(MODEM_INFO_IMEI, imei, sizeof(imei));
    if (rc < 0) {
        LOG_ERR("modem_info_string_get failed, rc = %d", rc);
    }

    did_already_run = true;
    return imei;
}

static int get_modem_str_and_convert_int(char* buffer, size_t buffer_size, modem_info modem_identifier)
{
    int length = modem_info_string_get(modem_identifier, buffer, buffer_size);
    if (length < 0) {
        LOG_ERR("modem_info_string_get failed, rc = %d", length);
        return 0;
    }

    char* end_ptr = NULL;
    int converted = strtol(buffer, &end_ptr, 10);
    return converted;
}

static float get_modem_str_and_convert_float(char* buffer, size_t buffer_size, modem_info modem_identifier)
{
    int length = modem_info_string_get(modem_identifier, buffer, buffer_size);
    if (length < 0) {
        LOG_ERR("modem_info_string_get failed, rc = %d", length);
        return 0;
    }

    char* end_ptr = NULL;
    int converted = strtod(buffer, &end_ptr);
    return converted;
}

ProviderInfo get_provider_info()
{
    ProviderInfo info = {};

    char buffer[128] = { 0 };
    info.mcc = get_modem_str_and_convert_int(buffer, sizeof(buffer), MODEM_INFO_MCC);
    info.mnc = get_modem_str_and_convert_int(buffer, sizeof(buffer), MODEM_INFO_MNC);
    info.lac = get_modem_str_and_convert_int(buffer, sizeof(buffer), MODEM_INFO_AREA_CODE);
    info.cellid = get_modem_str_and_convert_int(buffer, sizeof(buffer), MODEM_INFO_CELLID);
    info.signal_strength = get_modem_str_and_convert_int(buffer, sizeof(buffer), MODEM_INFO_RSRP);
    info.temperature = get_modem_str_and_convert_float(buffer, sizeof(buffer), MODEM_INFO_TEMP);

    return info;
}