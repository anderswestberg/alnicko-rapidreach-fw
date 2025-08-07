/**
 * @file switch_module.h
 * @brief Hardware switch input module with debounce and switch callbacks.
 *
 * This module initializes 4 GPIO switches, configures
 * edge-triggered interrupts for them, and applies software debounce
 * using delayed work. Each switch can register its own user-defined
 * callback function to handle input events.
 *
 * @author Eugene K.
 * @copyright (C) 2025 Alnicko Lab OU. All rights reserved.
 */

#ifndef SWITCH_MODULE_H
#define SWITCH_MODULE_H
#include <stdint.h>

typedef void (*switch_user_callback_t)(void);

/**
  * @brief Get the state of a specific switch.
  *
  * @param index Switch index [0..3]
  * @return 1 if HIGH, 0 if LOW, negative error code on failure.
  */
int switch_get_state(uint8_t index);

/**
 * @brief Register a callback for a specific switch.
 *
 * @param index Switch index [0..3]
 * @param cb    Callback function to call on switch event
 */
void switch_register_callback(uint8_t index, switch_user_callback_t cb);

#endif // SWITCH_MODULE_H