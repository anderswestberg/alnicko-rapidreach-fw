/**
 * @file led_control.h
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

#ifndef LED_CONTROL_H
#define LED_CONTROL_H

#include <zephyr/kernel.h>
#include <stdint.h>

typedef enum {
    LED_OK            = 0,
    LED_INVALID_INDEX = -1,
    LED_FAILED        = -2
} led_status_t;

/**
 * @brief Turn LED on
 *
 * @param index Index of LED (0 to NUM_LEDS-1)
 * @return LED_OK or LED_INVALID_INDEX
 */
led_status_t led_on(int index);

/**
 * @brief Turn LED off
 *
 * @param index Index of LED (0 to NUM_LEDS-1)
 * @return LED_OK or LED_INVALID_INDEX
 */
led_status_t led_off(int index);

/**
 * @brief Start blinking LED with specified timing
 *
 * @param index Index of LED (0 to NUM_LEDS-1)
 * @param on_ms ON duration in milliseconds
 * @param off_ms OFF duration in milliseconds
 * @return LED_OK or LED_INVALID_INDEX
 */
led_status_t led_blink(int index, uint32_t on_ms, uint32_t off_ms);

#endif // LED_CONTROL_H
