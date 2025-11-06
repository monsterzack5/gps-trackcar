#include "watchdog.h"

#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(watchdog, CONFIG_TRACKCAR_DEFAULT_LOG_LEVEL);

static const device* const wdg = DEVICE_DT_GET(DT_NODELABEL(wdt0));
static int wdt_channel = -1;

void wdt_feed_fn(k_timer* timer);

K_TIMER_DEFINE(wdt_timer, wdt_feed_fn, NULL);

void wdt_feed_fn(k_timer* timer)
{
    ARG_UNUSED(timer);

    wdt_feed(wdg, wdt_channel);
}

void wdg_expired_fn(const device* dev, int channel_id)
{
    LOG_ERR("Watchdog expired! :(\n");
    k_sleep(K_MSEC(1000));
}

void watchdog_init()
{
    if (!device_is_ready(wdg)) {
        LOG_ERR("Watch dog is not ready.");
        return;
    }

    wdt_timeout_cfg wdt_config {};

    wdt_config.flags = WDT_FLAG_RESET_SOC;
    wdt_config.window.min = 0u;
    wdt_config.window.max = 10000ul;
    wdt_config.callback = wdg_expired_fn;

    int channel = wdt_install_timeout(wdg, &wdt_config);

    if (channel < 0) {
        LOG_ERR("Failed to install watchdog, rc = %d", channel);
        return;
    }

    int rc = wdt_setup(wdg, WDT_OPT_PAUSE_HALTED_BY_DBG);
    if (rc < 0) {
        LOG_ERR("Failed to setup watchdog (err: %d)\n", rc);
        return;
    }

    wdt_channel = channel;

    k_timer_start(&wdt_timer, K_SECONDS(1), K_SECONDS(5));
}