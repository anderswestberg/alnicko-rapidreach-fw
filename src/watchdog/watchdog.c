/**
 * @file watchdog.c
 * 
 * @brief External Watchdog Control API
 *
 * This header provides functions to control an external watchdog circuit
 * via GPIO pins (enable, disable, feed).
 *
 * The module uses `wden` to enable/disable the watchdog and `wdi` to feed it
 * by toggling the pin state.
 * 
 * @author Eugene K.
 * @copyright (C) 2025 Alnicko Lab OU. All rights reserved.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>

#include "watchdog.h"

LOG_MODULE_REGISTER(watchdog, CONFIG_RPR_MODULE_WATCHDOG_LOG_LEVEL);

#define WATCHDOG_ENABLE_VALUE  1
#define WATCHDOG_DISABLE_VALUE 0

#define WDEN_GPIO_NODE DT_ALIAS(wden)
#define WDI_GPIO_NODE  DT_ALIAS(wdi)

#define WDEN_GPIO_SPEC GPIO_DT_SPEC_GET(WDEN_GPIO_NODE, gpios)
#define WDI_GPIO_SPEC  GPIO_DT_SPEC_GET(WDI_GPIO_NODE, gpios)

struct watchdog_config {
    struct gpio_dt_spec wden_gpio;
    struct gpio_dt_spec wdi_gpio;
    bool                is_device_ready;
};

static struct watchdog_config watchdog_cfg = {
    .wden_gpio       = WDEN_GPIO_SPEC,
    .wdi_gpio        = WDI_GPIO_SPEC,
    .is_device_ready = false,
};

/**
 * @brief Enable the watchdog via WDEN pin.
 *
 * @return 0 on success, negative error code otherwise.
 */
int watchdog_enable(void)
{
    if (!watchdog_cfg.is_device_ready) {
        LOG_ERR("Watchdog GPIOs not ready");
        return -ENODEV;
    }

#ifndef CONFIG_RPR_DISABLE_WATCHDOG_WHILE_DEBUG
    int ret = gpio_pin_set_dt(&watchdog_cfg.wden_gpio, WATCHDOG_ENABLE_VALUE);
    if (ret == 0) {
        LOG_DBG("Watchdog ENABLED");
    } else {
        LOG_ERR("Failed to enable watchdog (err %d)", ret);
    }
    return ret;
#else
    LOG_WRN("Disable watchdog reset!");
    LOG_WRN("Use RPR_DISABLE_WATCHDOG_WHILE_DEBUG to turn off debug mode");
    return 0;
#endif
}

/**
 * @brief Disable the watchdog via WDEN pin.
 *
 * @return 0 on success, negative error code otherwise.
 */
int watchdog_disable(void)
{
    if (!watchdog_cfg.is_device_ready) {
        LOG_ERR("Watchdog GPIOs not ready");
        return -ENODEV;
    }
    int ret = gpio_pin_set_dt(&watchdog_cfg.wden_gpio, WATCHDOG_DISABLE_VALUE);
    if (ret == 0) {
        LOG_DBG("Watchdog DISABLED");
    } else {
        LOG_ERR("Failed to disable watchdog (err %d)", ret);
    }
    return ret;
}

/**
 * @brief Feed the watchdog by toggling the WDI pin.
 *
 * @return 0 on success, negative error code otherwise.
 */
int watchdog_feed(void)
{
    if (!watchdog_cfg.is_device_ready) {
        LOG_ERR("Watchdog GPIOs not ready");
        return -ENODEV;
    }
    int ret = gpio_pin_toggle_dt(&watchdog_cfg.wdi_gpio);
    if (ret == 0) {
        LOG_DBG("Watchdog has been fed");
    } else {
        LOG_ERR("Failed to feed watchdog (err %d)", ret);
    }
    return ret;
}

/**
 * @brief Initialize watchdog-related GPIOs (WDI, WDEN, CHGEN).
 *
 * Configures output pins based on devicetree definitions.
 * Must be called before using any watchdog functions.
 *
 * @return 0 on success, negative error code otherwise.
 */
static int watchdog_init(void)
{
    if (!device_is_ready(watchdog_cfg.wden_gpio.port) ||
        !device_is_ready(watchdog_cfg.wdi_gpio.port)) {
        LOG_ERR("Watchdog GPIOs not ready");
        return -ENODEV;
    }

    int ret = 0;
    ret |= gpio_pin_configure_dt(&watchdog_cfg.wden_gpio, GPIO_OUTPUT_HIGH);
    ret |= gpio_pin_configure_dt(&watchdog_cfg.wdi_gpio, GPIO_OUTPUT_LOW);

    if (ret != 0) {
        LOG_ERR("Failed to configure watchdog GPIOs");
    } else {
        LOG_DBG("Watchdog GPIOs initialized");
        watchdog_cfg.is_device_ready = true;
    }
    return ret;
}

SYS_INIT(watchdog_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
