/**
 * @file led_control.c
 * @brief LED control module for multiple GPIO LEDs.
 *
 * This module provides an interface to control up to 4 LEDs using
 * the Zephyr GPIO and workqueue APIs. Each LED can be turned on,
 * turned off, or set to blink with configurable on/off durations.
 *
 * The module is initialized automatically at application startup.
 * 
 * @author Eugene K.
 * @copyright (C) 2025 Alnicko Lab OU. All rights reserved.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <zephyr/logging/log.h>

#include "led_control.h"

#define LED_ON_VALUE  1
#define LED_OFF_VALUE 0

LOG_MODULE_REGISTER(led, CONFIG_RPR_MODULE_LED_LOG_LEVEL);

#define LED1_NODE DT_ALIAS(led0)
#define LED2_NODE DT_ALIAS(led1)
#define LED3_NODE DT_ALIAS(led2)
#define LED4_NODE DT_ALIAS(led3)

#define NUM_LEDS 4

struct leds_config {
    struct gpio_dt_spec     led_specs[NUM_LEDS];
    bool                    is_device_ready;
    struct k_work_delayable blink_works[NUM_LEDS];
    bool                    led_states[NUM_LEDS];
    uint32_t                blink_on_ms[NUM_LEDS];
    uint32_t                blink_off_ms[NUM_LEDS];
};

static struct leds_config leds_config = {
    .led_specs = {
        GPIO_DT_SPEC_GET(LED1_NODE, gpios),
        GPIO_DT_SPEC_GET(LED2_NODE, gpios),
        GPIO_DT_SPEC_GET(LED3_NODE, gpios),
        GPIO_DT_SPEC_GET(LED4_NODE, gpios),
    },
    .is_device_ready = false,
};

/**
 * @brief Internal handler for LED blinking.
 *
 * @param work Pointer to the delayed work structure.
 */
static void blink_handler(struct k_work *work)
{
    int                      index = -1;
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);

    for (int i = 0; i < NUM_LEDS; i++) {
        if (dwork == &leds_config.blink_works[i]) {
            index = i;
            break;
        }
    }

    if (index == -1) {
        LOG_ERR("Unknown work item");
        return;
    }

    if (!leds_config.is_device_ready) {
        return;
    }

    leds_config.led_states[index] = !leds_config.led_states[index];
    if (gpio_pin_toggle_dt(&leds_config.led_specs[index])) {
        return;
    }

    k_work_schedule(&leds_config.blink_works[index],
                    K_MSEC(leds_config.led_states[index] ?
                                   leds_config.blink_on_ms[index] :
                                   leds_config.blink_off_ms[index]));
}

/**
 * @brief Turn LED on
 *
 * @param index Index of LED (0 to NUM_LEDS-1)
 * @return LED_OK or LED_INVALID_INDEX
 */
led_status_t led_on(int index)
{
    if (!leds_config.is_device_ready) {
        LOG_ERR("Failed to configure LEDs GPIOs");
        return LED_FAILED;
    }
    if (index < 0 || index >= NUM_LEDS) {
        return LED_INVALID_INDEX;
    }
    k_work_cancel_delayable(&leds_config.blink_works[index]);
    gpio_pin_set_dt(&leds_config.led_specs[index], LED_ON_VALUE);
    return LED_OK;
}

/**
 * @brief Turn LED off
 *
 * @param index Index of LED (0 to NUM_LEDS-1)
 * @return LED_OK or LED_INVALID_INDEX
 */
led_status_t led_off(int index)
{
    if (!leds_config.is_device_ready) {
        LOG_ERR("Failed to configure LEDs GPIOs");
        return LED_FAILED;
    }
    if (index < 0 || index >= NUM_LEDS) {
        return LED_INVALID_INDEX;
    }
    k_work_cancel_delayable(&leds_config.blink_works[index]);
    gpio_pin_set_dt(&leds_config.led_specs[index], LED_OFF_VALUE);
    return LED_OK;
}

/**
 * @brief Start blinking LED with specified timing
 *
 * @param index Index of LED (0 to NUM_LEDS-1)
 * @param on_ms ON duration in milliseconds
 * @param off_ms OFF duration in milliseconds
 * @return LED_OK or LED_INVALID_INDEX
 */
led_status_t led_blink(int index, uint32_t on_ms, uint32_t off_ms)
{
    if (!leds_config.is_device_ready) {
        LOG_ERR("Failed to configure LEDs GPIOs");
        return LED_FAILED;
    }
    if (index < 0 || index >= NUM_LEDS) {
        return LED_INVALID_INDEX;
    }
    leds_config.blink_on_ms[index]  = (on_ms == 0) ? 1 : on_ms;
    leds_config.blink_off_ms[index] = (off_ms == 0) ? 1 : off_ms;
    k_work_schedule(&leds_config.blink_works[index], K_NO_WAIT);
    return LED_OK;
}

/**
 * @brief Initialize the LED module.
 *
 * Configures all LED GPIOs and initializes the delayed work
 * structures used for blinking. 
 *
 * @return 0 on success, negative error code on failure.
 */
static int led_module_init(void)
{
    int ret = 0;
    for (int i = 0; i < NUM_LEDS; i++) {
        if (!device_is_ready(leds_config.led_specs[i].port)) {
            LOG_ERR("LED %d not ready", i);
            return -ENODEV;
        }
        ret |= gpio_pin_configure_dt(&leds_config.led_specs[i],
                                     GPIO_OUTPUT_HIGH);
        k_work_init_delayable(&leds_config.blink_works[i], blink_handler);
    }

    if (ret != 0) {
        LOG_ERR("Failed to configure LEDs GPIOs");
        return ret;
    } else {
        LOG_DBG("LEDs GPIOs initialized");
        leds_config.is_device_ready = true;
    }

    for (int i = 0; i < NUM_LEDS; i++) {
        ret = led_off(i);
        if (ret != 0) {
            LOG_ERR("Failure to turn off the LED #%d", i);
            leds_config.is_device_ready = false;
        }
    }
    return ret;
}

SYS_INIT(led_module_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
