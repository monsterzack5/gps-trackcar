#include "low_power.h"

#include "pmic.h"
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>

LOG_MODULE_REGISTER(low_power, CONFIG_TRACKCAR_DEFAULT_LOG_LEVEL);

static const device* const console_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
static const device* const sensor = DEVICE_DT_GET(DT_ALIAS(accel0));
static const device* const spi_nor = DEVICE_DT_GET(DT_ALIAS(ext_flash));

static int suspend_nor_storage(void)
{
    /* Disable external flash */
    int err = pm_device_action_run(spi_nor, PM_DEVICE_ACTION_SUSPEND);
    if (err < 0) {
        LOG_ERR("Unable to suspend SPI NOR flash. (err: %d)", err);
        return err;
    }

    return 0;
}

static int suspend_uart()
{
    int err = pm_device_action_run(console_dev, PM_DEVICE_ACTION_SUSPEND);
    if (err < 0) {
        LOG_ERR("Unable to suspend console UART. (err: %d)", err);
        return err;
    }

    return 0;
}

static int resume_uart()
{
    int err = pm_device_action_run(console_dev, PM_DEVICE_ACTION_RESUME);
    if (err < 0) {
        LOG_ERR("Unable to resume console UART. (err: %d)", err);
        return err;
    }

    return 0;
}

static int suspend_accelerometer()
{
    if (!device_is_ready(sensor)) {
        LOG_ERR("Could not get accel0 device");
        return -1;
    }

    // Disable the device
    sensor_value odr = {
        .val1 = 0,
        .val2 = 0,
    };

    int rc = sensor_attr_set(sensor, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, &odr);

    if (rc != 0) {
        LOG_ERR("Failed to set odr: %d", rc);
        return rc;
    }

    return 0;
}

int set_power_mode(PowerMode mode)
{
    int rc = 0;

    switch (mode) {
    case PowerMode::High:
        rc |= enable_rp2040();
        rc |= resume_uart();
        break;
    case PowerMode::Low:
        rc |= disable_rp2040();
        rc |= suspend_accelerometer();
        rc |= suspend_nor_storage();
        rc |= suspend_uart();
        break;
    }

    return 0;
}
