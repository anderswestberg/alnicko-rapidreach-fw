/**
 * @file sntp_sync.h
 * @brief SNTP (Simple Network Time Protocol) synchronization module
 *
 * This module provides automatic time synchronization using SNTP.
 * It synchronizes with NTP servers and updates the RTC.
 *
 * @author RapidReach Team
 * @copyright (C) 2025 Alnicko Lab OU. All rights reserved.
 */

#ifndef __SNTP_SYNC_H__
#define __SNTP_SYNC_H__

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Initialize the SNTP sync module
 *
 * This function initializes the SNTP client and prepares it for time synchronization.
 * It should be called after network connectivity is established.
 *
 * @return 0 on success, negative error code on failure
 */
int sntp_sync_init(void);

/**
 * @brief Perform a one-time SNTP synchronization
 *
 * Query an NTP server and update the system time and RTC if successful.
 *
 * @param timeout_ms Timeout in milliseconds for the SNTP query (default: 5000)
 * @return 0 on success, negative error code on failure
 */
int sntp_sync_now(uint32_t timeout_ms);

/**
 * @brief Enable or disable automatic periodic SNTP synchronization
 *
 * When enabled, the module will automatically sync time at regular intervals.
 *
 * @param enable true to enable periodic sync, false to disable
 * @param interval_seconds Interval between syncs in seconds (default: 3600 = 1 hour)
 * @return 0 on success, negative error code on failure
 */
int sntp_sync_set_periodic(bool enable, uint32_t interval_seconds);

/**
 * @brief Get the last SNTP sync status
 *
 * @param last_sync_time_out Pointer to store the last successful sync time (Unix timestamp)
 * @return 0 if last sync was successful, negative error code if failed or never synced
 */
int sntp_sync_get_status(int64_t *last_sync_time_out);

/**
 * @brief Get the time difference detected during last sync (in seconds)
 *
 * Positive value means device was ahead, negative means device was behind.
 *
 * @return Time difference in seconds, or 0 if never synced
 */
int32_t sntp_sync_get_drift(void);

#endif // __SNTP_SYNC_H__

