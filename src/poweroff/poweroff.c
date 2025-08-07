/**
 * @file poweroff.c
 * @brief Module for handling power button with debounce and triggering battery poweroff GPIO.
 *
 * Handles power button presses via GPIO input with debounce logic.
 * Provides ability to register a user-defined callback.
 * Includes control for a separate GPIO that disconnects battery power.
 *
 * @author Eugene K.
 * @copyright (C) 2025 Alnicko Lab OU. All rights reserved.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "poweroff.h"

#define POWER_OFF_VALUE       0
#define SHORT_PUSH_VALUE      1
#define MS_PW_GPIO_PULSE_TIME 100
#define BATTERY_ON_VALUE      0
#define BATTERY_OFF_VALUE     1

LOG_MODULE_REGISTER(poweroff, CONFIG_RPR_MODULE_POWEROFF_LOG_LEVEL);

#define DEBOUNCE_DELAY_MS 1500

#define POWER_BUTTON_NODE  DT_ALIAS(power_sw)
#define POWEROFF_CTRL_NODE DT_ALIAS(poweroff_gpio)

#define POWER_BUTTON_SPEC  GPIO_DT_SPEC_GET(POWER_BUTTON_NODE, gpios)
#define POWEROFF_GPIO_SPEC GPIO_DT_SPEC_GET(POWEROFF_CTRL_NODE, gpios)

struct poweroff_config {
    struct gpio_dt_spec     power_btn;
    struct gpio_dt_spec     poweroff_gpio;
    struct gpio_callback    gpio_cb;
    struct k_work_delayable debounce_work;
    power_button_callback_t poweroff_callback;
    power_button_callback_t short_push_callback;
    bool                    is_ready;
};

static struct poweroff_config poweroff_cfg = {
    .power_btn           = POWER_BUTTON_SPEC,
    .poweroff_gpio       = POWEROFF_GPIO_SPEC,
    .poweroff_callback   = NULL,
    .short_push_callback = NULL,
    .is_ready            = false,
};

/**
  * @brief Debounce handler for power button GPIO.
  */
static void debounce_handler(struct k_work *work)
{
    int state = gpio_pin_get_dt(&poweroff_cfg.power_btn);

    if (state < 0) {
        LOG_ERR("Failed to read power button (err %d)", state);
        return;
    }

    LOG_DBG("Power button debounced: %s", state ? "short push" : "long push");

    if ((state == POWER_OFF_VALUE) && poweroff_cfg.poweroff_callback) {
        poweroff_cfg.poweroff_callback();
    }

    if ((state == SHORT_PUSH_VALUE) && poweroff_cfg.short_push_callback) {
        poweroff_cfg.short_push_callback();
    }
}

/**
  * @brief GPIO interrupt handler for power button.
  */
static void power_button_irq_handler(const struct device  *port,
                                     struct gpio_callback *cb,
                                     uint32_t              pins)
{
    k_work_reschedule(&poweroff_cfg.debounce_work, K_MSEC(DEBOUNCE_DELAY_MS));
}

/**
  * @brief Initialize power button GPIO and poweroff control GPIO.
  *
  * @return 0 on success, negative error code otherwise.
  */
static int poweroff_init(void)
{
    if (!device_is_ready(poweroff_cfg.power_btn.port) ||
        !device_is_ready(poweroff_cfg.poweroff_gpio.port)) {
        LOG_ERR("GPIO device not ready");
        return -ENODEV;
    }

    int ret = gpio_pin_configure_dt(&poweroff_cfg.power_btn, GPIO_INPUT);
    if (ret < 0) {
        LOG_ERR("Failed to configure power button (err %d)", ret);
        return ret;
    }

    ret = gpio_pin_configure_dt(&poweroff_cfg.poweroff_gpio, GPIO_OUTPUT_LOW);
    if (ret < 0) {
        LOG_ERR("Failed to configure poweroff GPIO (err %d)", ret);
        return ret;
    }

    ret = gpio_pin_interrupt_configure_dt(&poweroff_cfg.power_btn,
                                          GPIO_INT_EDGE_FALLING);
    if (ret < 0) {
        LOG_ERR("Failed to configure interrupt (err %d)", ret);
        return ret;
    }

    gpio_init_callback(&poweroff_cfg.gpio_cb,
                       power_button_irq_handler,
                       BIT(poweroff_cfg.power_btn.pin));

    gpio_add_callback(poweroff_cfg.power_btn.port, &poweroff_cfg.gpio_cb);
    k_work_init_delayable(&poweroff_cfg.debounce_work, debounce_handler);

    poweroff_cfg.is_ready = true;
    LOG_DBG("Poweroff module initialized");
    return 0;
}

SYS_INIT(poweroff_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

/**
  * @brief Register a user callback for the power button long press.
  *
  * @param cb Callback function
  */
void poweroff_register_callback(power_button_callback_t cb)
{
    poweroff_cfg.poweroff_callback = cb;
}

/**
 * @brief Register a user callback for the power button short press.
 * 
 * @param cb Callback function
 */
void poweroff_register_short_push_callback(power_button_callback_t cb)
{
    poweroff_cfg.short_push_callback = cb;
}

/**
  * @brief Activate the poweroff GPIO to cut off battery.
  *
  * @return 0 on success, negative error code otherwise.
  */
int poweroff_activate(void)
{
    if (!poweroff_cfg.is_ready) {
        return -ENODEV;
    }

    int ret = gpio_pin_set_dt(&poweroff_cfg.poweroff_gpio, BATTERY_OFF_VALUE);
    if (ret < 0) {
        LOG_ERR("Failed to activate poweroff GPIO (err %d)", ret);
    }
    k_msleep(MS_PW_GPIO_PULSE_TIME);

    ret = gpio_pin_set_dt(&poweroff_cfg.poweroff_gpio, BATTERY_ON_VALUE);
    if (ret < 0) {
        LOG_ERR("Failed to activate poweroff GPIO (err %d)", ret);
    }

    LOG_INF("Power-off activated");

    return ret;
}

/**
 * @brief Enable power button GPIO interrupt.
 *
 * @return 0 on success, negative error code otherwise.
 */
int poweroff_irq_enable(void)
{
    if (!poweroff_cfg.is_ready) {
        return -ENODEV;
    }

    int ret = gpio_pin_interrupt_configure_dt(&poweroff_cfg.power_btn,
                                              GPIO_INT_EDGE_RISING);
    if (ret < 0) {
        LOG_ERR("Failed to enable interrupt (err %d)", ret);
    } else {
        LOG_DBG("Power button interrupt enabled");
    }
    return ret;
}

/**
 * @brief Disable power button GPIO interrupt.
 *
 * @return 0 on success, negative error code otherwise.
 */
int poweroff_irq_disable(void)
{
    if (!poweroff_cfg.is_ready) {
        return -ENODEV;
    }

    int ret = gpio_pin_interrupt_configure_dt(&poweroff_cfg.power_btn,
                                              GPIO_INT_DISABLE);
    if (ret < 0) {
        LOG_ERR("Failed to disable interrupt (err %d)", ret);
    } else {
        LOG_DBG("Power button interrupt disabled");
    }
    return ret;
}