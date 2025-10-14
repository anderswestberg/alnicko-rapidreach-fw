/**
 * @file sntp_sync_shell.c
 * @brief Shell commands for SNTP time synchronization
 *
 * @copyright (C) 2025 Alnicko Lab OU. All rights reserved.
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/logging/log.h>
#include <time.h>

#include "sntp_sync.h"
#include "../rtc/rtc.h"

LOG_MODULE_REGISTER(sntp_shell, CONFIG_RPR_MODULE_SNTP_SYNC_LOG_LEVEL);

/**
 * @brief Command: sntp sync
 * Perform immediate SNTP synchronization
 */
static int cmd_sntp_sync(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t timeout_ms = 5000;
	int ret;
	
	if (argc > 1) {
		timeout_ms = strtoul(argv[1], NULL, 10);
		if (timeout_ms < 1000) {
			shell_error(sh, "Timeout must be at least 1000 ms");
			return -EINVAL;
		}
	}
	
	shell_print(sh, "Synchronizing time with NTP server (timeout: %u ms)...", timeout_ms);
	
	ret = sntp_sync_now(timeout_ms);
	if (ret < 0) {
		shell_error(sh, "SNTP sync failed: %d", ret);
		return ret;
	}
	
	shell_print(sh, "Time synchronized successfully");
	
	/* Show drift if available */
	int32_t drift = sntp_sync_get_drift();
	if (drift != 0) {
		shell_print(sh, "Clock adjusted by %d seconds (was %s)", 
			abs(drift), drift > 0 ? "ahead" : "behind");
	}
	
	return 0;
}

/**
 * @brief Command: sntp status
 * Show SNTP synchronization status
 */
static int cmd_sntp_status(const struct shell *sh, size_t argc, char **argv)
{
	int64_t last_sync_time = 0;
	int ret;
	
	ret = sntp_sync_get_status(&last_sync_time);
	
	if (ret < 0 && ret != -ENODATA) {
		shell_error(sh, "Failed to get status: %d", ret);
		return ret;
	}
	
	if (last_sync_time == 0) {
		shell_print(sh, "SNTP Status: Never synchronized");
	} else {
		struct tm *tm_time;
		time_t sync_time = (time_t)last_sync_time;
		
		tm_time = gmtime(&sync_time);
		if (tm_time) {
			shell_print(sh, "Last successful sync: %04d-%02d-%02d %02d:%02d:%02d UTC",
				tm_time->tm_year + 1900, tm_time->tm_mon + 1, tm_time->tm_mday,
				tm_time->tm_hour, tm_time->tm_min, tm_time->tm_sec);
		}
		
		int32_t drift = sntp_sync_get_drift();
		if (drift != 0) {
			shell_print(sh, "Last clock adjustment: %d seconds (was %s)",
				abs(drift), drift > 0 ? "ahead" : "behind");
		}
	}
	
	/* Show current system time */
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	struct tm *now = gmtime(&ts.tv_sec);
	if (now) {
		shell_print(sh, "Current system time: %04d-%02d-%02d %02d:%02d:%02d UTC",
			now->tm_year + 1900, now->tm_mon + 1, now->tm_mday,
			now->tm_hour, now->tm_min, now->tm_sec);
	}
	
	/* Show RTC time */
	struct rtc_time rtc_data;
	ret = get_date_time(&rtc_data);
	if (ret == 0) {
		shell_print(sh, "RTC time:            %04d-%02d-%02d %02d:%02d:%02d",
			rtc_data.tm_year, rtc_data.tm_mon, rtc_data.tm_mday,
			rtc_data.tm_hour, rtc_data.tm_min, rtc_data.tm_sec);
	}
	
	return 0;
}

/**
 * @brief Command: sntp periodic <enable|disable> [interval]
 * Enable or disable periodic SNTP synchronization
 */
static int cmd_sntp_periodic(const struct shell *sh, size_t argc, char **argv)
{
	bool enable;
	uint32_t interval = 3600;
	int ret;
	
	if (argc < 2) {
		shell_error(sh, "Usage: sntp periodic <enable|disable> [interval_seconds]");
		return -EINVAL;
	}
	
	if (strcmp(argv[1], "enable") == 0) {
		enable = true;
		
		if (argc > 2) {
			interval = strtoul(argv[2], NULL, 10);
			if (interval < 60) {
				shell_error(sh, "Interval must be at least 60 seconds");
				return -EINVAL;
			}
		}
		
		ret = sntp_sync_set_periodic(true, interval);
		if (ret < 0) {
			shell_error(sh, "Failed to enable periodic sync: %d", ret);
			return ret;
		}
		
		shell_print(sh, "Periodic SNTP sync enabled (interval: %u seconds)", interval);
	} else if (strcmp(argv[1], "disable") == 0) {
		ret = sntp_sync_set_periodic(false, 0);
		if (ret < 0) {
			shell_error(sh, "Failed to disable periodic sync: %d", ret);
			return ret;
		}
		
		shell_print(sh, "Periodic SNTP sync disabled");
	} else {
		shell_error(sh, "Invalid argument. Use 'enable' or 'disable'");
		return -EINVAL;
	}
	
	return 0;
}

/* Define shell commands */
SHELL_STATIC_SUBCMD_SET_CREATE(sntp_cmds,
	SHELL_CMD(sync, NULL, "Synchronize time now [timeout_ms]", cmd_sntp_sync),
	SHELL_CMD(status, NULL, "Show SNTP sync status", cmd_sntp_status),
	SHELL_CMD(periodic, NULL, "Enable/disable periodic sync [interval_sec]", cmd_sntp_periodic),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(sntp, &sntp_cmds, "SNTP time synchronization commands", NULL);

