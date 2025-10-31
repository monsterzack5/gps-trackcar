#include "temp_sensor.h"

#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(temp_sensor, CONFIG_TRACKCAR_DEFAULT_LOG_LEVEL);

const device* aht = DEVICE_DT_GET(DT_NODELABEL(aht20));

TempAndHumidity get_temp_and_humidity()
{
    int ret = sensor_sample_fetch(aht);
    if (ret < 0) {
        LOG_ERR("Failed to read sensor sample, rc = %d", ret);
        return {};
    }

    TempAndHumidity temp_humidity {};
    sensor_value value {};

    sensor_channel_get(aht, SENSOR_CHAN_AMBIENT_TEMP, &value);
    temp_humidity.temperature = (float)value.val1 + ((float)value.val2 / 1000000.0f);

    sensor_channel_get(aht, SENSOR_CHAN_HUMIDITY, &value);
    temp_humidity.humidity = (float)value.val1 + ((float)value.val2 / 1000000.0f);

    LOG_DBG("Temperature: %f, Humidity: %f", temp_humidity.temperature, temp_humidity.humidity);
    return temp_humidity;
}

int temp_sensor_init()
{
    if (!device_is_ready(aht)) {
        LOG_ERR("Device not ready!");
    }

    return 0;
}