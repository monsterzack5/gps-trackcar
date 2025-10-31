#include "device_ui.h"

#include <stdint.h>
#include <zephyr/drivers/led.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "pmic.h"

#define BUTTON_KEY INPUT_KEY_0

// TODO: Make kconfig
static const k_timeout_t BUTTON_TIMEOUT = K_MSEC(250);

LOG_MODULE_REGISTER(device_ui, CONFIG_TRACCAR_DEFAULT_LOG_LEVEL);

void handle_button_data(k_work* work);
K_WORK_DEFINE(button_data_work, handle_button_data);

void handle_button_events(k_timer* timer);
void button_event_callback(input_event* evt, void* data);

K_TIMER_DEFINE(button_event_timer, handle_button_events, NULL);
INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_NODELABEL(buttons)), button_event_callback, NULL);

enum class ButtonAction : uint64_t {
    ShortPress,
    LongPress,
    Unknown,
};

enum class CurrentButtonState {
    Pressed,
    Released,
};

struct ButtonData {
    ButtonAction last_action;
    CurrentButtonState current_state;
    uint8_t presses;
    int64_t pressed_down_at;
};

static ButtonData button_data = { .last_action = ButtonAction::Unknown, .current_state = CurrentButtonState::Released, .presses = 0, .pressed_down_at = 0 };
static ButtonData button_data_copy = { .last_action = ButtonAction::Unknown, .current_state = CurrentButtonState::Released, .presses = 0, .pressed_down_at = 0 };

// LEDs
static const device* leds = DEVICE_DT_GET(DT_NODELABEL(npm1300_leds));
static const uint32_t POWER_INDICATOR_LED = 1;
static const uint8_t POWER_INDICATOR_LED_ON_BRIGHTNESS = 80;

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

void handle_button_data(k_work* work)
{
    ARG_UNUSED(work);

    if (button_data_copy.presses == 1 && button_data_copy.last_action == ButtonAction::ShortPress) {
        toggle_power_mode();
    }
}

void handle_button_events(k_timer* timer)
{
    ARG_UNUSED(timer);

    // LOG_DBG("button: Presses: %u, Current State: %s, Button Action: %s\n", button_data.presses, get_str_current_state(button_data.current_state), get_str_button_action(button_data.last_action));

    // Copy button data
    button_data_copy = button_data;

    // Reset button
    button_data.last_action = ButtonAction::Unknown;
    button_data.presses = 0;

    // TODO: Maybe some logic around if the work is pending or not?
    k_work_submit(&button_data_work);
}

void button_event_callback(input_event* evt, void* data)
{
    ButtonData* button = nullptr;

    if (evt->code == BUTTON_KEY) {
        button = &button_data;
    } else {
        LOG_ERR("Unknown input event received");
        return;
    }

    /* On Press */
    if (evt->value == 1) {
        button->current_state = CurrentButtonState::Pressed;
        button->presses += 1;
        button->last_action = ButtonAction::Unknown;
        button->pressed_down_at = k_uptime_get();

        k_timer_stop(&button_event_timer);
        return;
    }

    /* On Release */
    button->current_state = CurrentButtonState::Released;
    int64_t press_duration = k_uptime_delta(&button->pressed_down_at);

    if (press_duration >= 1000) {
        button->last_action = ButtonAction::LongPress;
    } else {
        button->last_action = ButtonAction::ShortPress;
    }

    k_timer_start(&button_event_timer, BUTTON_TIMEOUT, K_FOREVER);
}

/* LEDs */

struct led_work_struct {
    k_work_delayable work;
    const uint16_t* blink_pattern;
    uint8_t blink_length;
    uint16_t index;
};

static led_work_struct led_work;

void led_work_handler(k_work* work_item)
{
    // Handle LED stuff

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
    // led_set_brightness(leds, POWER_INDICATOR_LED, POWER_INDICATOR_LED_ON_BRIGHTNESS);
    led_off(leds, POWER_INDICATOR_LED);

    k_work_init_delayable(&led_work.work, led_work_handler);
    return 0;
}