/**
 * @file microphone.c
 * @brief Simple module for detecting sound using GPIO microphone input.
 *
 * This module reads the state of a digital GPIO pin connected to a microphone
 * and determines whether sound is currently detected.
 *
 * @author Eugene K.
 * @copyright (C) 2025 Alnicko Lab OU. All rights reserved.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/init.h>

LOG_MODULE_REGISTER(microphone, CONFIG_RPR_MODULE_MICRO_LOG_LEVEL);

#define MICROPHONE_GPIO_NODE DT_ALIAS(microphone)
#define MICROPHONE_GPIO_SPEC GPIO_DT_SPEC_GET(MICROPHONE_GPIO_NODE, gpios)

struct microphone_config {
    struct gpio_dt_spec gpio;
    bool                is_device_ready;
};

static struct microphone_config mic_cfg = {
    .gpio            = MICROPHONE_GPIO_SPEC,
    .is_device_ready = false,

};

/**
  * @brief Initialize the microphone GPIO.
  *
  * @return 0 on success, negative error code otherwise.
  */
static int microphone_init(void)
{

    if (!device_is_ready(mic_cfg.gpio.port)) {
        LOG_ERR("Microphone GPIO not ready");
        return -ENODEV;
    }

    int ret = gpio_pin_configure_dt(&mic_cfg.gpio, GPIO_INPUT);
    if (ret != 0) {
        LOG_ERR("Failed to configure microphone GPIO");
    } else {
        LOG_DBG("Microphone GPIO initialized");
        mic_cfg.is_device_ready = true;
    }

    return ret;
}

SYS_INIT(microphone_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

/**
  * @brief Check if sound is detected by the microphone.
  *
  * @retval true  if sound is detected (pin is HIGH)
  * @retval false if no sound is detected (pin is LOW)
  */
bool microphone_is_sound_detected(void)
{
    if (!mic_cfg.is_device_ready) {
        LOG_ERR("Microphone GPIO not ready");
        return -ENODEV;
    }
    int val = gpio_pin_get_dt(&mic_cfg.gpio);
    if (val < 0) {
        LOG_ERR("Failed to read microphone GPIO (err %d)", val);
        return false;
    }

    return (val == 1);
}
