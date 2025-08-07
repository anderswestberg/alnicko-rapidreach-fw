/**
 * @file watchdog.h
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

#ifndef WATCHDOG_H
#define WATCHDOG_H

/**
 * @brief Enable the watchdog via WDEN pin.
 *
 * @return 0 on success, negative error code otherwise.
 */
int watchdog_enable(void);

/**
 * @brief Disable the watchdog via WDEN pin.
 *
 * @return 0 on success, negative error code otherwise.
 */
int watchdog_disable(void);

/**
 * @brief Feed the watchdog by toggling the WDI pin.
 *
 * @return 0 on success, negative error code otherwise.
 */
int watchdog_feed(void);

#endif /* WATCHDOG_H */
