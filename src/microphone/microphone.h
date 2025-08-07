/**
 * @file microphone.h
 * @brief Simple module for detecting sound using GPIO microphone input.
 *
 * This module reads the state of a digital GPIO pin connected to a microphone
 * and determines whether sound is currently detected.
 *
 * @author Eugene K.
 * @copyright (C) 2025 Alnicko Lab OU. All rights reserved.
 */

#ifndef MICROPHONE_H
#define MICROPHONE_H

#include <stdbool.h>

/**
  * @brief Check if sound is detected by the microphone.
  *
  * @retval true  if sound is detected (pin is HIGH)
  * @retval false if no sound is detected (pin is LOW)
  */
bool microphone_is_sound_detected(void);

#endif /* MICROPHONE_H */
