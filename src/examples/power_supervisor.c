/**
 * @file power_supervisor.c
 * @brief Demonstration of system power supervision and shutdown logic.
 *
 * This example showcases how to implement basic system health monitoring using
 * available firmware modules: battery level measurement, input power detection,
 * charger management, watchdog feeding, and safe power-off via button.
 * 
 * @author Eugene K.
 * @copyright (C) 2025 Alnicko Lab OU. All rights reserved.
 */

#include <zephyr/kernel.h>
#include <stdlib.h>
#include <zephyr/sys/reboot.h>

#include "battery.h"
#include "charger.h"
#include "watchdog.h"
#include "led_control.h"
#include "poweroff.h"
#include "power_supervisor.h"

#ifdef CONFIG_RPR_MODULE_SYSTEM_WATCHDOG
#include "system_watchdog.h"
#endif

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(supervisor, CONFIG_EXAMPLES_POWER_SUPERVISOR_LOG_LEVEL);

#define POWER_LED         0
#define LED_BLINK_PROCESS 100

#define SUPERVISOR_THREAD_PRIORITY   -15
#define SUPERVISOR_THREAD_STACK_SIZE 4096

#define BATTERY_LOW_THRESHOLD \
    CONFIG_EXAMPLES_POWER_SUPERVISOR_BATTERY_LOW_THRESHOLD
#define BATTERY_HIGH_THRESHOLD \
    CONFIG_EXAMPLES_POWER_SUPERVISOR_BATTERY_HIGH_THRESHOLD
#define CHECK_POWER_INTERVAL_MS \
    CONFIG_EXAMPLES_POWER_SUPERVISOR_CHECK_INTERVAL_MS

#define SUPERVISOR_DEINIT_DELAY_MS    1000
#define SUPERVISOR_POWERDOWN_DELAY_MS 500

#define SUPERVISOR_INTERVAL_MS 500

struct power_context {
    struct k_sem                   poweroff_sem;
    supervisor_ping_callback_t     ping_callback;
    supervisor_poweroff_callback_t poweroff_callback;
    bool                           is_watchdog_enable;
};

static struct power_context pwr_ctx = {
    .ping_callback      = NULL,
    .poweroff_callback  = NULL,
    .is_watchdog_enable = false,
};

/**
 * @brief Supervisor thread for system power and health monitoring.
 *
 * Periodically:
 *  - Feeding the watchdog via `supervisor_feed_watchdog`
 *  - Monitoring power and battery status via `supervisor_power_monitor`
 *  - Checking for a power-off request via semaphore and initiating shutdown
 *
 * The thread runs continuously and ensures the system remains healthy.
 * If critical conditions are detected (e.g., low battery, failed ping),
 * it calls `supervisor_poweroff()` to shutdown or reset the system.
 */
static void supervisor_thread_func(void);

K_THREAD_DEFINE(supervisor_thread,
                SUPERVISOR_THREAD_STACK_SIZE,
                supervisor_thread_func,
                NULL,
                NULL,
                NULL,
                SUPERVISOR_THREAD_PRIORITY,
                0,
                0);

/**
 * @brief Handles the power-off button press event.
 *
 * Disables the power button interrupt and signals the supervisor
 * to proceed with the power-off procedure via semaphore.
 */
static void power_off_pressed(void)
{
    poweroff_irq_disable();
    LOG_INF("Pressed power off button");

    k_sem_give(&pwr_ctx.poweroff_sem);
}

/**
 * @brief Request system power-off via supervisor.
 *
 * This function signals the supervisor to initiate the power-off sequence
 * by releasing the poweroff semaphore. 
 */
void request_poweroff(void)
{
    LOG_INF("Power-off requested");

    k_sem_give(&pwr_ctx.poweroff_sem);
}

/**
 * @brief Registers a callback to be used for watchdog ping.
 *
 * Stores the provided callback function in the supervisor context.
 *
 * @param cb Callback function to be registered.
 */
void supervisor_ping_register_callback(supervisor_ping_callback_t cb)
{
    pwr_ctx.ping_callback = cb;
}

/**
 * @brief Registers a callback to be called before system power-off.
 *
 * Stores the provided callback function to be executed before initiating shutdown.
 *
 * @param cb Callback function to be registered.
 */
void supervisor_poweroff_register_callback(supervisor_poweroff_callback_t cb)
{
    pwr_ctx.poweroff_callback = cb;
}

/**
 * @brief Input power detection interrupt callback.
 *
 * This function is called from an interrupt when external power is connected or disconnected.
 * It checks the input power status and battery level, and enables or disables the charger accordingly.
 * If the battery voltage cannot be read, a shutdown is initiated.
 */
static void supervisor_input_pwr_detected(void)
{
    input_power_status_t detected_status = is_input_power_detected();

    if (detected_status == INPUT_POWER_FAILED) {
        LOG_ERR("Power detection failed (DETECT pin error)");
        return;
    }

    if (detected_status == INPUT_POWER_DETECTED) {

        input_power_status_t power_status = input_power_get_status();

        if (power_status == INPUT_POWER_VALID) {

            int voltage = battery_sample();
            if (voltage < 0) {
                LOG_ERR("Battery voltage read failed (err %d)", voltage);
                k_sem_give(&pwr_ctx.poweroff_sem);
                return;
            }

            unsigned int battery_level = battery_level_pptt(voltage) / 100;

            if (battery_level < BATTERY_HIGH_THRESHOLD) {
                LOG_INF("Charging started: battery not fully charged");
                LOG_INF("Voltage: %d mV, battery level: ~%u%%",
                        voltage,
                        battery_level);

                if (!charger_get_state()) {
                    if (charger_enable() != 0) {
                        LOG_ERR("Failed to enable charger");
                    }
                }
            } else {
                LOG_INF("Battery fully charged: no charging needed");
            }

            return;

        } else {
            LOG_WRN("Power present but invalid (ACOK pin low)");
            if (charger_get_state()) {
                if (charger_disable() != 0) {
                    LOG_ERR("Failed to disable charger");
                }
            }
        }

    } else if (detected_status == INPUT_POWER_NOT_DETECTED) {
        LOG_INF("External power disconnected");

        if (charger_get_state()) {
            if (charger_disable() != 0) {
                LOG_ERR("Failed to disable charger");
            }
        }
    }
}

/**
 * @brief Performs a controlled power-off or system reset.
 *
 * This function is called to safely shut down or reset the system. It disables the watchdog,
 * handles LED indication, invokes a registered shutdown callback (if available), and then either:
 *   - powers off the system if no external power is detected, or
 *   - resets the system if external power is still present.
 */
static void supervisor_poweroff(void)
{
    if (watchdog_disable() != 0) {
        LOG_WRN("Watchdog disable failed");
    }

    if (led_blink(POWER_LED, LED_BLINK_PROCESS, LED_BLINK_PROCESS) != LED_OK) {
        LOG_WRN("Failed to blink power LED");
    }

    if (pwr_ctx.poweroff_callback) {
        LOG_INF("Calling registered poweroff callback...");
        pwr_ctx.poweroff_callback();
    } else {
        LOG_WRN("No poweroff callback registered");
        k_msleep(SUPERVISOR_DEINIT_DELAY_MS);
    }

    if (led_off(POWER_LED) != LED_OK) {
        LOG_WRN("Failed to turn off power LED");
    }

    input_power_status_t detected_status = is_input_power_detected();

    if (detected_status == INPUT_POWER_FAILED) {
        LOG_WRN("Input power detection failed (detect pin error)");
    }

    if (detected_status != INPUT_POWER_DETECTED) {
        LOG_INF("No external power detected, initiating shutdown...");
        k_msleep(SUPERVISOR_POWERDOWN_DELAY_MS);
        while (true) {
            int ret = poweroff_activate();
            if (ret < 0) {
                LOG_WRN("Poweroff failed (err %d), rebooting instead", ret);
                k_msleep(SUPERVISOR_POWERDOWN_DELAY_MS);
                sys_reboot(SYS_REBOOT_COLD);
            }
            k_msleep(SUPERVISOR_POWERDOWN_DELAY_MS);
        }
    } else {
        LOG_INF("External power present, rebooting system...");
        k_msleep(SUPERVISOR_POWERDOWN_DELAY_MS);
        sys_reboot(SYS_REBOOT_COLD);
    }
}

/**
 * @brief Monitors power input and battery level, manages charger and initiates poweroff if needed.
 *
 * This function is intended to be called periodically to:
 * - Check input power and charger status.
 * - Measure battery voltage and calculate battery level.
 * - Start or stop charging based on thresholds.
 * - Trigger system shutdown if battery is critically low or detection fails.
 */
static void supervisor_power_monitor(void)
{
    input_power_status_t detected_status = is_input_power_detected();
    charger_status_t     charger_status  = charger_get_status();

    // If external power is present and charging is ongoing — nothing to do
    if (detected_status == INPUT_POWER_DETECTED &&
        charger_status == CHARGER_STATUS_CHARGING) {
        LOG_DBG("Charging in progress...");
        return;
    }

    // Error states, power or charger check failed
    if (detected_status == INPUT_POWER_FAILED ||
        charger_status == CHARGER_STATUS_FAILED) {
        LOG_ERR("Power or charger detection failed (possible pin error)");
        supervisor_poweroff();
        return;
    }

    // Read battery voltage
    int voltage = battery_sample();
    if (voltage < 0) {
        LOG_ERR("Failed to read battery voltage (err %d)", voltage);
        supervisor_poweroff();
        return;
    }

    unsigned int battery_level = battery_level_pptt(voltage) / 100;

    // External power connected but charging is not active
    if (detected_status == INPUT_POWER_DETECTED &&
        charger_status == CHARGER_STATUS_DONE_OR_FAULT) {

        if (battery_level == 0) {
            LOG_WRN("Battery is missing! (0%% level)");

            if (charger_get_state()) {
                if (charger_disable() != 0) {
                    LOG_ERR("Failed to disable charger");
                }
            }

            return;

        } else if (battery_level > BATTERY_HIGH_THRESHOLD) {

            if (charger_get_state()) {

                LOG_INF("Battery is fully charged!");

                if (charger_disable() != 0) {
                    LOG_ERR("Failed to disable charger");
                }
            }
        } else {
            LOG_INF("Charging started: battery not fully charged");

            LOG_INF("Voltage is %d mV, battery level ~%u%%",
                    voltage,
                    battery_level);

            if (!charger_get_state()) {
                if (charger_enable() != 0) {
                    LOG_ERR("Failed to enable charger");
                }
            }
            return;
        }
    }

    // If battery level is critical - shutdown
    LOG_DBG("Voltage is %d mV, battery level ~%u%%", voltage, battery_level);
    if (battery_level <= BATTERY_LOW_THRESHOLD) {
        LOG_ERR("Battery critically low (%u%%). Powering off...",
                battery_level);
        supervisor_poweroff();
    }
}

/**
 * @brief Periodically feeds the watchdog timer if system is healthy.
 *
 * It enables the watchdog if it hasn't been enabled yet, and feeds it
 * only if the registered `ping_callback` returns true, indicating the system is responsive.
 *
 * If ping fails repeatedly, the system will be reset.
 */
static void supervisor_feed_watchdog(void)
{
    if (!pwr_ctx.is_watchdog_enable) {
        if (watchdog_enable() != 0) {
            LOG_ERR("Failed to enable watchdog");
            supervisor_poweroff();
        }
        pwr_ctx.is_watchdog_enable = true;
    }

    if (!pwr_ctx.ping_callback) {
        LOG_ERR("Ping callback is not set. Skipping watchdog feed.");
        return;
    }

    if (pwr_ctx.ping_callback()) {
        if (watchdog_feed() != 0) {
            LOG_ERR("Watchdog feed failed.");
        }

#ifdef CONFIG_RPR_MODULE_SYSTEM_WATCHDOG
        if (system_watchdog_feed() != 0) {
            LOG_ERR("System Watchdog feed failed.");
        }
#endif

    } else {
        LOG_WRN("Ping callback returned false. Watchdog was not fed.");
    }
}

/**
 * @brief Supervisor thread for system power and health monitoring.
 *
 * Periodically:
 *  - Feeding the watchdog via `supervisor_feed_watchdog`
 *  - Monitoring power and battery status via `supervisor_power_monitor`
 *  - Checking for a power-off request via semaphore and initiating shutdown
 *
 * The thread runs continuously and ensures the system remains healthy.
 * If critical conditions are detected (e.g., low battery, failed ping),
 * it calls `supervisor_poweroff()` to shutdown or reset the system.
 */
static void supervisor_thread_func(void)
{

    input_power_register_callback(supervisor_input_pwr_detected);

    k_sem_init(&pwr_ctx.poweroff_sem, 0, 1);
    poweroff_register_callback(power_off_pressed);

    uint32_t battery_interval = CHECK_POWER_INTERVAL_MS;

    if (led_on(POWER_LED) != LED_OK) {
        LOG_WRN("Failed to turn on power LED");
    }
    k_msleep(SUPERVISOR_INTERVAL_MS);

    while (true) {
        k_msleep(SUPERVISOR_INTERVAL_MS);

        supervisor_feed_watchdog();

        battery_interval += SUPERVISOR_INTERVAL_MS;

        if (battery_interval >= CHECK_POWER_INTERVAL_MS) {
            supervisor_power_monitor();
            battery_interval = 0;
        }

        if (k_sem_take(&pwr_ctx.poweroff_sem, K_NO_WAIT) == 0) {
            LOG_INF("System power off");
            supervisor_poweroff();
        }
    }
}