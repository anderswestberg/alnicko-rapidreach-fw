/**
 * @file poweroff.h
 * @brief Module for handling power button with debounce and triggering battery poweroff GPIO.
 *
 * Handles power button presses via GPIO input with debounce logic.
 * Provides ability to register a user-defined callback.
 * Includes control for a separate GPIO that disconnects battery power.
 *
 * @author Eugene K.
 * @copyright (C) 2025 Alnicko Lab OU. All rights reserved.
 */

#ifndef POWEROFF_H
#define POWEROFF_H

typedef void (*power_button_callback_t)(void);

/**
  * @brief Register a user callback for the power button long press.
  *
  * @param cb Callback function
  */
void poweroff_register_callback(power_button_callback_t cb);

/**
 * @brief Register a user callback for the power button short press.
 * 
 * @param cb Callback function
 */
void poweroff_register_short_push_callback(power_button_callback_t cb);

/**
  * @brief Activate the poweroff GPIO to cut off battery.
  *
  * @return 0 on success, negative error code otherwise.
  */
int poweroff_activate(void);

/**
 * @brief Enable power button GPIO interrupt.
 *
 * @return 0 on success, negative error code otherwise.
 */
int poweroff_irq_enable(void);

/**
 * @brief Disable power button GPIO interrupt.
 *
 * @return 0 on success, negative error code otherwise.
 */
int poweroff_irq_disable(void);

#endif // POWEROFF_H
