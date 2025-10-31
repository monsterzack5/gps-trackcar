#include "network_info.h"

#include <modem/modem_info.h>
#include <nrf_modem_at.h>
#include <stdio.h>
#include <stdlib.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(network_info, CONFIG_TRACKCAR_DEFAULT_LOG_LEVEL);

// NOTE: IMEI comes as "<15 Digit IMEI>\r\nOK\r\n"
static char imei[24] = { 0 };

const char* get_imei()
{
    static bool did_already_run = false;

    if (did_already_run) {
        return imei;
    }

    nrf_modem_at_cmd(imei, sizeof(imei), "AT+CGSN");

    imei[15] = '\0';

    return imei;
}

ProviderInfo get_provider_info()
{
    ProviderInfo info = {};

    char buffer[256] = { 0 };

    // Get network operator (MCC/MNC)
    nrf_modem_at_cmd(buffer, sizeof(buffer), "AT+COPS?");
    char plmn[8];
    if (sscanf(buffer, "+COPS: %*d,%*d,\"%[^\"]\"", plmn) == 1) {
        info.mcc = (plmn[0] - '0') * 100 + (plmn[1] - '0') * 10 + (plmn[2] - '0');
        info.mnc = atoi(plmn + 3);
    }

    // Get LAC and Cell ID
    nrf_modem_at_cmd(buffer, sizeof(buffer), "AT+CEREG?");
    sscanf(buffer, "+CEREG: %*d,%*d,\"%x\",\"%x\"", &info.lac, &info.cellid);

    // Get signal strength
    // nrf_modem_at_cmd(buffer, sizeof(buffer), "AT+CESQ");
    // sscanf(buffer, "+CESQ: %*d,%*d,%*d,%*d,%d,%d", &rsrq, &rsrp);

    // Get temperature
    nrf_modem_at_cmd(buffer, sizeof(buffer), "AT%%XTEMP?");
    sscanf(buffer, "%%XTEMP: %lf", &info.temperature);

    return info;
}