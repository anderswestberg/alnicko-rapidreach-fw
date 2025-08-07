/**
 * @file charger.h
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

#ifndef CHARGER_H
#define CHARGER_H

/**
 * @brief Callback type for notifying input power detection.
 */
typedef void (*input_power_detected_callback_t)(void);

/**
 * @enum charger_status_t
 * @brief Charger status read from CHGOK pin.
 *
 * - CHARGER_STATUS_CHARGING: charging is in progress (CHGOK = 0)
 * - CHARGER_STATUS_DONE_OR_FAULT: charging completed or fault (CHGOK = 1)
 * - CHARGER_STATUS_FAILED: failed to retrieve charger status (GPIO error).
 */
typedef enum {
    CHARGER_STATUS_CHARGING = 0,
    CHARGER_STATUS_DONE_OR_FAULT,
    CHARGER_STATUS_FAILED = -2
} charger_status_t;

/**
 * @enum input_power_status_t
 * @brief Input power presence status read from ACOK pin and input power detected pin.
 *
 * - INPUT_POWER_VALID: valid input power detected (ACOK = 0)
 * - INPUT_POWER_NOT_VALID: no or invalid input power (ACOK = 1)
 * - INPUT_POWER_DETECTED: Input power pin state is active (power detected GPIO)
 * - INPUT_POWER_NOT_DETECTED: Input power pin state is inactive (power detected GPIO)
 * - CHARGER_STATUS_FAILED: failed to read input power status (GPIO error).
 * 
 */
typedef enum {
    INPUT_POWER_NOT_VALID = 0,
    INPUT_POWER_VALID,
    INPUT_POWER_NOT_DETECTED = 0,
    INPUT_POWER_DETECTED,
    INPUT_POWER_FAILED = -2

} input_power_status_t;

/**
 * @brief Get charger status based on CHGOK pin.
 *
 * @return CHARGER_STATUS_CHARGING if charging is active,
 *         CHARGER_STATUS_DONE_OR_FAULT otherwise.
 */
charger_status_t charger_get_status(void);

/**
 * @brief Get input power status based on ACOK pin.
 *
 * @return INPUT_POWER_VALID if input power is present,
 *         INPUT_POWER_NOT_VALID otherwise.
 */
input_power_status_t input_power_get_status(void);

/**
 * @brief Enable the charger by setting CHGEN pin.
 *
 * @return 0 on success, negative error code otherwise.
 */
int charger_enable(void);

/**
 * @brief Disable the charger by clearing CHGEN pin.
 *
 * @return 0 on success, negative error code otherwise.
 */
int charger_disable(void);

/**
 * @brief Get the current charger state.
 *
 * @return true if charger was enabled, false otherwise.
 */
bool charger_get_state(void);

/**
 * @brief Check if external input power is currently detected via input power GPIO.
 *
 * @return INPUT_POWER_DETECTED if power is present,
 *         INPUT_POWER_NOT_DETECTED if not present,
 *         INPUT_POWER_FAILED on GPIO read error.
 */
input_power_status_t is_input_power_detected(void);

/**
  * @brief Register a user callback for the input power detected.
  *
  * @param cb Callback function
  */
void input_power_register_callback(input_power_detected_callback_t cb);

#endif /* CHARGER_H */
