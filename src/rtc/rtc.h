/**
 * @file rtc.h
 * @brief RTC interface for setting and retrieving date and time.
 *
 * This module provides an abstraction for working with the Real-Time Clock (RTC)
 * in a Zephyr-based system. It allows setting and getting the current date and time
 * using the `struct rtc_time` format, adjusting for standard `struct tm` conventions.
 *
 * Note: The year and month values are automatically offset according to `struct tm` conventions.
 *
 * @author Eugene K.
 * @copyright (C) 2025 Alnicko Lab OU. All rights reserved.
 */

#ifndef __RTC_MODULE_H__
#define __RTC_MODULE_H__
#include <zephyr/drivers/rtc.h>

/**
 * @brief Set the current RTC date and time.
 *
 * This function sets the date and time of the RTC device using the provided `rtc_time` structure.
 * It adjusts the year and month values according to the struct tm standard.
 *
 * @param rtc_data Pointer to a struct rtc_time containing the desired date and time.
 *
 * @return 0 on success, negative error code on failure.
 */
int set_date_time(struct rtc_time *rtc_data);

/**
 * @brief Get the current RTC date and time.
 *
 * This function reads the current date and time from the RTC device and fills
 * the provided `rtc_time` structure. It adjusts the year and 
 * month values according to the struct tm standard.
 *
 * @param rtc_data Pointer to a struct rtc_time where the current time will be stored.
 *
 * @return 0 on success, negative error code on failure.
 */
int get_date_time(struct rtc_time *rtc_data);

#endif // __RTC_MODULE_H__