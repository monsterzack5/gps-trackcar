#include "device_ui.h"

#include <stdint.h>
#include <zephyr/drivers/led.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "pmic.h"

#define BUTTON_KEY INPUT_KEY_0

// TODO: Make kconfig
static const k_timeout_t BUTTON_TIMEOUT = K_MSEC(CONFIG_BUTTON_RELEASE_TIMEOUT_MS);

LOG_MODULE_REGISTER(device_ui, CONFIG_TRACKCAR_DEFAULT_LOG_LEVEL);

/* ---- Buttons ---- */
enum class ButtonAction : uint64_t {
    ShortPress,
    LongPress,
    Unknown,
};

enum class ButtonState {
    Pressed,
    Released,
};

struct ButtonData {
    ButtonAction last_action;
    ButtonState current_state;
    uint8_t presses;
    int64_t pressed_down_at;
};

struct button_work_data {
    k_work_delayable work;
    ButtonData isr_data;
};
static button_work_data button_work;

void button_event_isr(input_event* evt, void* data);
INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_NODELABEL(buttons)), button_event_isr, (void*)&button_work);

static void toggle_power_mode()
{
    // High power is the default on boot
    static bool high_power = true;
    high_power = !high_power;

    if (high_power) {
        // High power
        enable_rp2040();
        blink_pattern(BlinkCode::HighPowerModeActivated);
        return;
    }

    // Low power mode
    disable_rp2040();
    blink_pattern(BlinkCode::LowPowerModeActivated);
}

void button_data_work_handler(k_work* work_item)
{
    button_work_data* data = CONTAINER_OF(work_item, struct button_work_data, work);

    // This is a race condition (too bad!)
    ButtonData copy = data->isr_data;
    data->isr_data.presses = 0;
    data->isr_data.pressed_down_at = 0;
    data->isr_data.last_action = ButtonAction::Unknown;

    if (copy.presses == 1 && copy.last_action == ButtonAction::ShortPress) {
        toggle_power_mode();
    }
}

void button_event_isr(input_event* evt, void* data)
{
    if (evt->code != BUTTON_KEY) {
        LOG_ERR("Unknown input event received");
        return;
    }

    auto* button = (button_work_data*)data;

    if (k_work_delayable_is_pending(&button->work)) {
        k_work_cancel_delayable(&button->work);
    }

    /* On Press */
    if (evt->value == 1) {
        button->isr_data.current_state = ButtonState::Pressed;
        button->isr_data.presses += 1;
        button->isr_data.last_action = ButtonAction::Unknown;
        button->isr_data.pressed_down_at = k_uptime_get();
        return;
    }

    /* On Release */
    button->isr_data.current_state = ButtonState::Released;
    int64_t press_duration = k_uptime_delta(&button->isr_data.pressed_down_at);

    button->isr_data.last_action = (press_duration >= 1000) ? ButtonAction::LongPress : ButtonAction::ShortPress;

    k_work_schedule(&button->work, K_MSEC(CONFIG_BUTTON_RELEASE_TIMEOUT_MS));
}

/* ---- LEDs ---- */
static const device* leds = DEVICE_DT_GET(DT_NODELABEL(npm1300_leds));
static const uint32_t POWER_INDICATOR_LED = 1;
static const uint8_t POWER_INDICATOR_LED_ON_BRIGHTNESS = 80;

struct led_work_struct {
    k_work_delayable work;
    const uint16_t* blink_pattern;
    uint8_t blink_length;
    uint16_t index;
};

static led_work_struct led_work;

void led_work_handler(k_work* work_item)
{
    led_work_struct* data = CONTAINER_OF(work_item, struct led_work_struct, work);
    LOG_INF("Handling index: %u", data->index);

    uint16_t sleep_duration = data->blink_pattern[data->index % 2];

    if (data->index % 2 == 0) {
        led_on(leds, POWER_INDICATOR_LED);
        LOG_INF("ON %u", sleep_duration);
    } else {
        led_off(leds, POWER_INDICATOR_LED);
        LOG_INF("OFF %u", sleep_duration);
    }

    if ((data->index + 1) >= data->blink_length) {
        data->index = 0;
        data->blink_length = 0;
        data->blink_pattern = nullptr;
        led_off(leds, POWER_INDICATOR_LED);
        return;
    }

    data->index += 1;

    k_work_schedule(&data->work, K_MSEC(sleep_duration));
}

void blink_pattern(BlinkCode code)
{
    LOG_INF("Blink? Blink? Blink?");

    k_work_cancel_delayable(&led_work.work);

    switch (code) {
    case BlinkCode::LowPowerModeActivated:
        led_work.blink_pattern = BlinkPattern::LOW_POWER_MODE_ACTIVATED;
        led_work.blink_length = BlinkPattern::LOW_POWER_MODE_ACTIVATED_LENGTH;
        break;
    case BlinkCode::HighPowerModeActivated:
        led_work.blink_pattern = BlinkPattern::HIGH_POWER_MODE_ACTIVATED;
        led_work.blink_length = BlinkPattern::HIGH_POWER_MODE_ACTIVATED_LENGTH;
        break;
    default:
        LOG_ERR("Unknown blink code");
        return;
    }

    led_work.index = 0;
    led_work.blink_length *= 2;
    k_work_schedule(&led_work.work, K_NO_WAIT);
}

int device_ui_init()
{
    // We are default in high power mode
    led_off(leds, POWER_INDICATOR_LED);

    k_work_init_delayable(&led_work.work, led_work_handler);
    k_work_init_delayable(&button_work.work, button_data_work_handler);
    return 0;
}