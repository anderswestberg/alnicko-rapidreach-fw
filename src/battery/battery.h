/**
 * @file battery.h
 * @brief Battery voltage monitoring module using ADC and optional voltage divider.
 *
 * This module provides battery voltage measurement and estimation functionality for embedded systems
 * based on the Zephyr RTOS. It uses the Analog-to-Digital Converter (ADC) to sample battery voltage
 * directly or through a resistive divider, depending on devicetree configuration.
 *
 * Based on Zephyr’s "Battery Voltage Measurement" sample.
 *
 * @author Eugene K.
 * @copyright (C) 2025 Alnicko Lab OU. All rights reserved.
 */

#ifndef APPLICATION_BATTERY_H_
#define APPLICATION_BATTERY_H_

/** @brief Measure the battery voltage.
  *
  * @return the battery voltage in millivolts, or a negative error
  * code.
  */
int battery_sample(void);

/** @brief A point in a battery discharge curve sequence.
  *
  * A discharge curve is defined as a sequence of these points, where
  * the first point has #lvl_pptt set to 10000 and the last point has
  * #lvl_pptt set to zero.  Both #lvl_pptt and #lvl_mV should be
  * monotonic decreasing within the sequence.
  */
struct battery_level_point {
    /** Remaining life at #lvl_mV. */
    uint16_t lvl_pptt;

    /** Battery voltage at #lvl_pptt remaining life. */
    uint16_t lvl_mV;
};

/** @brief Calculate the estimated battery level based on a measured voltage.
  *
  * @param batt_mV a measured battery voltage level.
  *
  * @return the estimated remaining capacity in parts per ten
  * thousand.
  */
unsigned int battery_level_pptt(unsigned int batt_mV);

#endif /* APPLICATION_BATTERY_H_ */