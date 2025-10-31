#include "pmic.h"

#include <zephyr/drivers/mfd/npm1300.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/logging/log.h>

#include "print_bin.h"

LOG_MODULE_REGISTER(pmic, CONFIG_TRACKCAR_DEFAULT_LOG_LEVEL);

/* Addresses */
#define NPM1300_BUCK_BASE 0x04U
#define NPM1300_BUCK_OFFSET_EN_CLR 0x01U
#define NPM1300_BUCK_STATUS 0x34U

#define NPM1300_BUCK_BUCKCTRL0 0x15U

/* Bits */
#define NPM1300_BUCK2_PULLDOWN_EN BIT(3)

static const device* buck2 = DEVICE_DT_GET(DT_NODELABEL(npm1300_buck2));
static const device* pmic = DEVICE_DT_GET(DT_NODELABEL(npm1300_pmic));

// NOTE: pmic1300 has two output power supplies, buck1 and buck2
// buck1 is what is powering us, the nrf9151, so don't touch that.
// buck2 is powering a rp2040, acting as a cmsis-dap programmer
// When we want actual low power, disable the RP2040, AND enable
// the buck2 pulldown, so that VCC on the rp2040 isn't floating.

// To Turn ON:
// - Disable Pulldown via setting BIT(3) LOW
// - Call Regulator Enable

// To Turn OFF:
// - Call Regulator Disable
// - Enable Pulldown via setting BIT(3) HIGH

int disable_rp2040()
{

    int rc = regulator_disable(buck2);

    if (rc < 0) {
        LOG_ERR("Failed to disable regulator!, rc = %d", rc);
        return rc;
    }

    rc = mfd_npm1300_reg_update(pmic, NPM1300_BUCK_BASE, NPM1300_BUCK_BUCKCTRL0, 1, NPM1300_BUCK2_PULLDOWN_EN);
    if (rc < 0) {
        LOG_ERR("Failed to enable pull down for PMIC, rc = %d", rc);
        return rc;
    }

    return 0;
}
int enable_rp2040()
{
    int rc = mfd_npm1300_reg_update(pmic, NPM1300_BUCK_BASE, NPM1300_BUCK_BUCKCTRL0, 0, NPM1300_BUCK2_PULLDOWN_EN);
    if (rc < 0) {
        LOG_ERR("Failed to disable pull down for PMIC, rc = %d", rc);
        return rc;
    }

    rc = regulator_enable(buck2);
    if (rc < 0) {
        LOG_ERR("Failed to enable regulator for PMIC, rc = %d", rc);
        return rc;
    }

    return 0;
}
