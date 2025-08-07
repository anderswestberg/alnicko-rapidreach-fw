/**
 * @file charger.c
 * @brief Charger control module for managing GPIO-based battery charging status and control.
 *
 * This module provides an interface for managing a battery charger using GPIO pins defined
 * in the devicetree. It supports:
 * - Initialization of charger-related GPIOs
 * - Reading charging status 
 * - Reading power input presence
 * - Enabling and disabling charging via enable pin
 * 
 * @author Eugene K.
 * @copyright (C) 2025 Alnicko Lab OU. All rights reserved.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>

#include "charger.h"

LOG_MODULE_REGISTER(charger, CONFIG_RPR_MODULE_CHARGER_LOG_LEVEL);

#define CHGEN_ENABLE_VALUE  1
#define CHGEN_DISABLE_VALUE 0

#define CHARGER_PIN_ACOK  DT_ALIAS(acok)
#define CHARGER_PIN_CHGOK DT_ALIAS(chgok)
#define CHARGER_PIN_CHGEN DT_ALIAS(chgen)
#define POWER_INPUT_PIN   DT_ALIAS(inputp)

#define ACOK_GPIO_SPEC GPIO_DT_SPEC_GET(CHARGER_PIN_ACOK, gpios)

#define CHGOK_GPIO_SPEC GPIO_DT_SPEC_GET(CHARGER_PIN_CHGOK, gpios)

#define CHGEN_GPIO_SPEC GPIO_DT_SPEC_GET(CHARGER_PIN_CHGEN, gpios)

#define INPUT_POWER_GPIO_SPEC GPIO_DT_SPEC_GET(POWER_INPUT_PIN, gpios)

#define DEBOUNCE_DELAY_MS 500

struct charger_config {
    struct gpio_dt_spec             acok_gpio;
    struct gpio_dt_spec             chgok_gpio;
    struct gpio_dt_spec             chgen_gpio;
    struct gpio_dt_spec             input_power_gpio;
    struct k_work_delayable         debounce_work;
    struct gpio_callback            gpio_cb;
    bool                            is_device_ready;
    bool                            charger_state;
    input_power_detected_callback_t input_power_callback;
};

static struct charger_config charger_config = {
    .acok_gpio            = ACOK_GPIO_SPEC,
    .chgok_gpio           = CHGOK_GPIO_SPEC,
    .chgen_gpio           = CHGEN_GPIO_SPEC,
    .input_power_gpio     = INPUT_POWER_GPIO_SPEC,
    .is_device_ready      = false,
    .charger_state        = false,
    .input_power_callback = NULL,

};

/**
  * @brief Debounce handler for input power detected GPIO.
  */
static void debounce_handler(struct k_work *work)
{
    LOG_DBG("Power pin detected");

    if (charger_config.input_power_callback) {
        charger_config.input_power_callback();
    }
}

/**
  * @brief GPIO interrupt handler for input power detected.
  */
static void input_power_detected_handler(const struct device  *port,
                                         struct gpio_callback *cb,
                                         uint32_t              pins)
{
    k_work_reschedule(&charger_config.debounce_work, K_MSEC(DEBOUNCE_DELAY_MS));
}

/**
  * @brief Register a user callback for the input power detected.
  *
  * @param cb Callback function
  */
void input_power_register_callback(input_power_detected_callback_t cb)
{
    charger_config.input_power_callback = cb;
}

/**
 * @brief Initialize charger-related GPIOs (ACOK, CHGOK, CHGEN, input power).
 *
 * Configures input and output pins based on devicetree definitions.
 * Must be called before using any charger functions.
 *
 * @return 0 on success, negative error code otherwise.
 */
static int charger_init(void)
{

    if (!device_is_ready(charger_config.acok_gpio.port) ||
        !device_is_ready(charger_config.chgok_gpio.port) ||
        !device_is_ready(charger_config.chgen_gpio.port) ||
        !device_is_ready(charger_config.input_power_gpio.port)) {
        LOG_ERR("Charger pins not ready");
        return -ENODEV;
    }

    int ret = 0;
    ret |= gpio_pin_configure_dt(&charger_config.acok_gpio, GPIO_INPUT);
    ret |= gpio_pin_configure_dt(&charger_config.chgok_gpio, GPIO_INPUT);
    ret |= gpio_pin_configure_dt(&charger_config.input_power_gpio, GPIO_INPUT);

    ret |= gpio_pin_configure_dt(&charger_config.chgen_gpio, GPIO_OUTPUT_HIGH);

    ret |= gpio_pin_interrupt_configure_dt(&charger_config.input_power_gpio,
                                           GPIO_INT_EDGE_BOTH);

    gpio_init_callback(&charger_config.gpio_cb,
                       input_power_detected_handler,
                       BIT(charger_config.input_power_gpio.pin));

    ret |= gpio_add_callback(charger_config.input_power_gpio.port,
                             &charger_config.gpio_cb);
    k_work_init_delayable(&charger_config.debounce_work, debounce_handler);

    if (ret) {
        LOG_ERR("Failed to configure charger pins (err %d)", ret);
    } else {
        LOG_DBG("Charger pins initialized");
        charger_config.is_device_ready = true;
    }

    return ret;
}

/**
 * @brief Get charger status based on CHGOK pin.
 *
 * @return CHARGER_STATUS_CHARGING if charging is active,
 *         CHARGER_STATUS_DONE_OR_FAULT otherwise.
 */
charger_status_t charger_get_status(void)
{
    if (!charger_config.is_device_ready) {
        LOG_ERR("Charger GPIOs not ready");
        return CHARGER_STATUS_FAILED;
    }
    int val = gpio_pin_get_dt(&charger_config.chgok_gpio);
    if (val < 0) {
        LOG_ERR("Error reading CHGOK pin (err %d)", val);
        return CHARGER_STATUS_DONE_OR_FAULT;
    }
    return (val == 0) ? CHARGER_STATUS_DONE_OR_FAULT : CHARGER_STATUS_CHARGING;
}

/**
 * @brief Get input power status based on ACOK pin.
 *
 * @return INPUT_POWER_VALID if input power is present,
 *         INPUT_POWER_NOT_VALID otherwise.
 */
input_power_status_t input_power_get_status(void)
{
    if (!charger_config.is_device_ready) {
        LOG_ERR("Charger GPIOs not ready");
        return INPUT_POWER_FAILED;
    }
    int val = gpio_pin_get_dt(&charger_config.acok_gpio);
    if (val < 0) {
        LOG_ERR("Error reading ACOK pin (err %d)", val);
        return INPUT_POWER_NOT_VALID;
    }
    return (val == 0) ? INPUT_POWER_NOT_VALID : INPUT_POWER_VALID;
}

/**
 * @brief Enable the charger by setting CHGEN pin.
 *
 * @return 0 on success, negative error code otherwise.
 */
int charger_enable(void)
{
    if (!charger_config.is_device_ready) {
        LOG_ERR("Charger GPIOs not ready");
        return -ENODEV;
    }
    int ret = gpio_pin_set_dt(&charger_config.chgen_gpio, CHGEN_ENABLE_VALUE);
    if (ret == 0) {
        LOG_DBG("Charger ENABLED");
        charger_config.charger_state = true;
    } else {
        LOG_ERR("Failed to enable charger (err %d)", ret);
    }
    return ret;
}

/**
 * @brief Disable the charger by clearing CHGEN pin.
 *
 * @return 0 on success, negative error code otherwise.
 */
int charger_disable(void)
{
    if (!charger_config.is_device_ready) {
        LOG_ERR("Charger GPIOs not ready");
        return -ENODEV;
    }
    int ret = gpio_pin_set_dt(&charger_config.chgen_gpio, CHGEN_DISABLE_VALUE);
    if (ret == 0) {
        LOG_DBG("Charger DISABLED");
        charger_config.charger_state = false;
    } else {
        LOG_ERR("Failed to disable charger (err %d)", ret);
    }
    return ret;
}

/**
 * @brief Get the current charger state.
 *
 * @return true if charger was enabled, false otherwise.
 */
bool charger_get_state(void)
{
    return charger_config.charger_state;
}

/**
 * @brief Check if external input power is currently detected via input power GPIO.
 *
 * @return INPUT_POWER_DETECTED if power is present,
 *         INPUT_POWER_NOT_DETECTED if not present,
 *         INPUT_POWER_FAILED on GPIO read error.
 */
input_power_status_t is_input_power_detected(void)
{
    if (!charger_config.is_device_ready) {
        LOG_ERR("Charger GPIOs not ready");
        return INPUT_POWER_FAILED;
    }
    int val = gpio_pin_get_dt(&charger_config.input_power_gpio);
    if (val < 0) {
        LOG_ERR("Error reading input power detect pin (err %d)", val);
        return INPUT_POWER_NOT_DETECTED;
    }
    return (val == 0) ? INPUT_POWER_NOT_DETECTED : INPUT_POWER_DETECTED;
}

SYS_INIT(charger_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
