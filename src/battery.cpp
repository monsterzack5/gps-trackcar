#include "battery.h"

#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(battery, LOG_LEVEL_DBG);

const struct device* pmic = DEVICE_DT_GET(DT_NODELABEL(npm1300_charger));

int battery_init()
{
    if (!device_is_ready(pmic)) {
        LOG_ERR("Device not ready!");
    }

    return 0;
}

// Returns estimated State of Charge (0.0–100.0%) for a single Li-ion cell
// Input: voltage in volts
// Output: percent charge (float)
static float liion_soc_from_voltage(float voltage)
{
    // Clamp voltage range
    if (voltage < 3.0f)
        voltage = 3.0f;
    if (voltage > 4.2f)
        voltage = 4.2f;

    // Approximate OCV-to-SoC curve using piecewise linear interpolation
    struct {
        float v;
        float soc;
    } table[] = {
        { 3.00f, 0.0f },
        { 3.20f, 5.0f },
        { 3.30f, 10.0f },
        { 3.40f, 20.0f },
        { 3.50f, 30.0f },
        { 3.60f, 45.0f },
        { 3.70f, 60.0f },
        { 3.80f, 75.0f },
        { 3.90f, 85.0f },
        { 4.00f, 92.0f },
        { 4.10f, 97.0f },
        { 4.20f, 100.0f }
    };

    int n = sizeof(table) / sizeof(table[0]);
    for (int i = 0; i < n - 1; i++) {
        if (voltage >= table[i].v && voltage <= table[i + 1].v) {
            float t = (voltage - table[i].v) / (table[i + 1].v - table[i].v);
            return table[i].soc + t * (table[i + 1].soc - table[i].soc);
        }
    }

    // Default (should never hit)
    return 0.0f;
}

float get_battery_soc()
{
    int ret = sensor_sample_fetch(pmic);
    if (ret < 0) {
        LOG_ERR("Failed to read sensor sample, rc = %d", ret);
        return 101.0f;
    }

    sensor_value value {};
    sensor_channel_get(pmic, SENSOR_CHAN_GAUGE_VOLTAGE, &value);
    double voltage = (float)value.val1 + ((float)value.val2 / 1000000);

    return liion_soc_from_voltage(voltage);
}

void get_battery_stats()
{

    int ret = sensor_sample_fetch(pmic);
    if (ret < 0) {
        LOG_ERR("Failed to read sensor sample, rc = %d", ret);
    }

    struct sensor_value value;
    sensor_channel_get(pmic, SENSOR_CHAN_GAUGE_VOLTAGE, &value);
    double voltage = (float)value.val1 + ((float)value.val2 / 1000000);

    sensor_channel_get(pmic, SENSOR_CHAN_GAUGE_AVG_CURRENT, &value);
    double current = (float)value.val1 + ((float)value.val2 / 1000000);

    LOG_INF("Battery Voltage: %f, Average Current: %f", voltage, current);
}