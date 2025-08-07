/**
 * @file system_watchdog.c
 * @brief Internal IWDG watchdog handler for STM32-based systems.
 *
 * This module provides initialization and feeding logic for the STM32 Independent Watchdog Timer (IWDG).
 * It was introduced to solve a rare but critical issue encountered after moving the DFU slot
 * to external flash memory due to internal flash limitations. In some cases, MCUBoot fails to boot the main
 * application image and hangs the device during startup.
 *
 * Although the system includes an external watchdog, its short timeout (1.6 seconds) is not suitable for
 * long-running operations such as firmware updates. The IWDG timer, however, can be
 * activated earlier during the bootloader phase and maintained throughout the firmware update and runtime.
 *
 * This internal watchdog acts as a second-level safety mechanism. It is enabled in MCUBoot, where it is fed
 * during firmware updates, and continues to operate in the main firmware via `system_watchdog_feed()`.
 *
 * This module works in parallel with the external watchdog and provides additional protection against
 * rare startup hangs.
 *
 * @author Eugene K.
 * @copyright (C) 2025 Alnicko Lab OU. All rights reserved.
 */

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/logging/log.h>

#include "system_watchdog.h"

LOG_MODULE_REGISTER(sys_watchdog, CONFIG_RPR_MODULE_SYSTEM_WATCHDOG_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(st_stm32_watchdog)
#define WDT_OPT        WDT_OPT_PAUSE_HALTED_BY_DBG
#define WDT_MAX_WINDOW CONFIG_RPR_MODULE_SYSTEM_WATCHDOG_TIMEOUT_MS
#define WDT_MIN_WINDOW 0U
#else
#error Support only independent STM32 watchdog
#endif

struct sys_watchdog_config {
    int                        wdt_channel_id;
    const struct device *const wdt;
    bool                       is_device_ready;
};

static struct sys_watchdog_config sys_watchdog_cfg = {
    .wdt_channel_id  = 0,
    .wdt             = DEVICE_DT_GET(DT_ALIAS(watchdog0)),
    .is_device_ready = false,
};

/**
 * @brief Feed the internal STM32 independent watchdog (IWDG).
 *
 * This function resets the watchdog timer to prevent system reset.
 * Must be called periodically from the application to keep the system alive.
 *
 * @return 0 on success, negative error code on failure.
 */
int system_watchdog_feed(void)
{
    if (!sys_watchdog_cfg.is_device_ready) {
        LOG_ERR("System Watchdog not ready");
        return -ENODEV;
    }

    int ret = wdt_feed(sys_watchdog_cfg.wdt, sys_watchdog_cfg.wdt_channel_id);

    if (ret < 0) {
        LOG_ERR("Failed to feed system watchdog (err %d)", ret);
    }
    return ret;
}

/**
 * @brief Initialize the STM32 internal independent watchdog (IWDG).
 *
 * Sets up and starts the IWDG with timeout configured via Kconfig.
 * This function is called automatically during system initialization.
 *
 * @return 0 on success, negative error code on failure.
 */
static int system_watchdog_init(void)
{
    if (!device_is_ready(sys_watchdog_cfg.wdt)) {
        LOG_ERR("%s: device not ready", sys_watchdog_cfg.wdt->name);
        return -ENODEV;
    }

    sys_watchdog_cfg.is_device_ready = true;

    struct wdt_timeout_cfg wdt_config = {
        /* Reset SoC when watchdog timer expires. */
        .flags = WDT_FLAG_RESET_SOC,

        /* Expire watchdog after max window */
        .window.min = WDT_MIN_WINDOW,
        .window.max = WDT_MAX_WINDOW,

        /* IWDG driver for STM32 doesn't support callback */
        .callback = NULL,
    };

    sys_watchdog_cfg.wdt_channel_id =
            wdt_install_timeout(sys_watchdog_cfg.wdt, &wdt_config);

    if (sys_watchdog_cfg.wdt_channel_id < 0) {
        LOG_ERR("Watchdog install error");
        return sys_watchdog_cfg.wdt_channel_id;
    }

    int ret = wdt_setup(sys_watchdog_cfg.wdt, WDT_OPT);
    if (ret < 0) {
        LOG_ERR("Watchdog setup error");
        return ret;
    }

    LOG_INF("System watchdog initialized successfully");
    return 0;
}

SYS_INIT(system_watchdog_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
