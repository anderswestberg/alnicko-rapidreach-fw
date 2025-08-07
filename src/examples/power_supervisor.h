/**
 * @file power_supervisor.h
 * @brief Demonstration of system power supervision and shutdown logic.
 *
 * This example showcases how to implement basic system health monitoring using
 * available firmware modules: battery level measurement, input power detection,
 * charger management, watchdog feeding, and safe power-off via button.
 * 
 * @author Eugene K.
 * @copyright (C) 2025 Alnicko Lab OU. All rights reserved.
 */

#ifndef _SUPERVISOR_H_
#define _SUPERVISOR_H_

/**
 * @brief Callback type used to determine if the watchdog should be fed.
 *
 * The callback should return true for feeding the watchdog,
 * or false if ping is failed.
 */
typedef bool (*supervisor_ping_callback_t)(void);

/**
 * @brief Callback type used to execute custom logic before power-off.
 *
 * This callback will be called before the system powers off, allowing
 * the application to perform cleanup or shutdown tasks.
 */
typedef void (*supervisor_poweroff_callback_t)(void);

/**
 * @brief Registers a callback to be used for watchdog ping.
 *
 * Stores the provided callback function in the supervisor context.
 *
 * @param cb Callback function to be registered.
 */
void supervisor_ping_register_callback(supervisor_ping_callback_t cb);

/**
 * @brief Registers a callback to be called before system power-off.
 *
 * Stores the provided callback function to be executed before initiating shutdown.
 *
 * @param cb Callback function to be registered.
 */
void supervisor_poweroff_register_callback(supervisor_poweroff_callback_t cb);

/**
 * @brief Request system power-off via supervisor.
 *
 * This function signals the supervisor to initiate the power-off sequence
 * by releasing the poweroff semaphore. 
 */
void request_poweroff(void);

#endif // _SUPERVISOR_H_
