/**
 * @file sntp_sync.c
 * @brief SNTP (Simple Network Time Protocol) synchronization implementation
 *
 * This module provides automatic time synchronization using SNTP.
 * It synchronizes with NTP servers and updates the RTC.
 *
 * @author RapidReach Team
 * @copyright (C) 2025 Alnicko Lab OU. All rights reserved.
 */

#include <zephyr/kernel.h>
#include <zephyr/net/sntp.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/logging/log.h>
#include <time.h>
#include <errno.h>

#include "sntp_sync.h"
#include "../rtc/rtc.h"

LOG_MODULE_REGISTER(sntp_sync, CONFIG_RPR_MODULE_SNTP_SYNC_LOG_LEVEL);

/* Default NTP servers (pool.ntp.org provides load-balanced servers) */
#define NTP_SERVER_PRIMARY   "pool.ntp.org"
#define NTP_SERVER_SECONDARY "time.google.com"
#define NTP_PORT             123

/* Default sync parameters */
#define DEFAULT_SYNC_TIMEOUT_MS    5000
#define DEFAULT_SYNC_INTERVAL_SEC  3600  /* 1 hour */

struct sntp_sync_state {
	bool initialized;
	bool periodic_enabled;
	uint32_t sync_interval_sec;
	int64_t last_sync_time;
	int32_t last_drift_sec;
	int last_sync_result;
	struct k_work_delayable periodic_work;
};

static struct sntp_sync_state state = {
	.initialized = false,
	.periodic_enabled = false,
	.sync_interval_sec = DEFAULT_SYNC_INTERVAL_SEC,
	.last_sync_time = 0,
	.last_drift_sec = 0,
	.last_sync_result = -ENODATA,
};

/**
 * @brief Update the RTC with the current system time
 */
static int update_rtc_from_system_time(void)
{
	struct timespec ts;
	struct rtc_time rtc_data;
	struct tm *tm_time;
	
	/* Get current system time */
	if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
		LOG_ERR("Failed to get system time");
		return -errno;
	}
	
	/* Convert to tm structure */
	tm_time = gmtime(&ts.tv_sec);
	if (!tm_time) {
		LOG_ERR("Failed to convert time");
		return -EINVAL;
	}
	
	/* Prepare RTC time (already in tm format) */
	rtc_data.tm_year = tm_time->tm_year + 1900;  /* tm_year is years since 1900 */
	rtc_data.tm_mon = tm_time->tm_mon + 1;       /* tm_mon is 0-11, RTC expects 1-12 */
	rtc_data.tm_mday = tm_time->tm_mday;
	rtc_data.tm_hour = tm_time->tm_hour;
	rtc_data.tm_min = tm_time->tm_min;
	rtc_data.tm_sec = tm_time->tm_sec;
	rtc_data.tm_wday = tm_time->tm_wday;         /* Day of week (0-6, Sunday = 0) */
	rtc_data.tm_yday = tm_time->tm_yday;         /* Day of year (0-365) */
	rtc_data.tm_isdst = tm_time->tm_isdst;       /* DST flag */
	rtc_data.tm_nsec = 0;                        /* Nanoseconds */
	
	LOG_DBG("Updating RTC with: %04d-%02d-%02d %02d:%02d:%02d wday=%d",
		rtc_data.tm_year, rtc_data.tm_mon, rtc_data.tm_mday,
		rtc_data.tm_hour, rtc_data.tm_min, rtc_data.tm_sec,
		rtc_data.tm_wday);
	
	/* Update RTC */
	int ret = set_date_time(&rtc_data);
	if (ret < 0) {
		LOG_ERR("Failed to update RTC: %d", ret);
		return ret;
	}
	
	LOG_INF("RTC updated: %04d-%02d-%02d %02d:%02d:%02d UTC",
		rtc_data.tm_year, rtc_data.tm_mon, rtc_data.tm_mday,
		rtc_data.tm_hour, rtc_data.tm_min, rtc_data.tm_sec);
	
	return 0;
}

/**
 * @brief Perform SNTP synchronization with a specific server
 */
static int perform_sntp_query(const char *server, uint32_t timeout_ms)
{
	struct sntp_ctx ctx;
	struct sockaddr_in addr4;
	struct addrinfo hints, *result;
	struct sntp_time sntp_time;
	struct timespec ts_before, ts_after;
	int ret;
	
	LOG_INF("Resolving NTP server: %s", server);
	
	/* Resolve server hostname */
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	
	ret = getaddrinfo(server, "123", &hints, &result);
	if (ret != 0) {
		LOG_ERR("Failed to resolve %s: %d", server, ret);
		return -EHOSTUNREACH;
	}
	
	/* Copy resolved address */
	memcpy(&addr4, result->ai_addr, sizeof(addr4));
	freeaddrinfo(result);
	
	LOG_INF("Resolved to: %s:%d", 
		net_addr_ntop(AF_INET, &addr4.sin_addr, (char[INET_ADDRSTRLEN]){}, INET_ADDRSTRLEN),
		ntohs(addr4.sin_port));
	
	/* Get current time before sync */
	clock_gettime(CLOCK_REALTIME, &ts_before);
	
	/* Initialize SNTP context */
	ret = sntp_init(&ctx, (struct sockaddr *)&addr4, sizeof(addr4));
	if (ret < 0) {
		LOG_ERR("Failed to init SNTP: %d", ret);
		return ret;
	}
	
	/* Query NTP server */
	LOG_INF("Querying NTP server (timeout: %u ms)...", timeout_ms);
	ret = sntp_query(&ctx, timeout_ms, &sntp_time);
	
	sntp_close(&ctx);
	
	if (ret < 0) {
		LOG_ERR("SNTP query failed: %d", ret);
		return ret;
	}
	
	/* Get current time after sync */
	clock_gettime(CLOCK_REALTIME, &ts_after);
	
	/* Calculate time drift */
	int64_t time_before_ms = (int64_t)ts_before.tv_sec * 1000 + ts_before.tv_nsec / 1000000;
	int64_t time_after_ms = (int64_t)ts_after.tv_sec * 1000 + ts_after.tv_nsec / 1000000;
	int32_t drift_sec = (int32_t)((time_before_ms - time_after_ms) / 1000);
	
	state.last_drift_sec = drift_sec;
	
	if (drift_sec != 0) {
		LOG_WRN("Time adjusted by %d seconds (device was %s)",
			abs(drift_sec), drift_sec > 0 ? "ahead" : "behind");
	} else {
		LOG_INF("Time already synchronized (drift < 1 second)");
	}
	
	/* Update RTC with new system time */
	ret = update_rtc_from_system_time();
	if (ret < 0) {
		LOG_WRN("Failed to update RTC, but system time is synchronized");
		/* Don't fail the whole operation if RTC update fails */
	}
	
	/* Update state */
	state.last_sync_time = ts_after.tv_sec;
	state.last_sync_result = 0;
	
	LOG_INF("SNTP synchronization successful");
	return 0;
}

/**
 * @brief Periodic work handler for automatic time sync
 */
static void periodic_sync_handler(struct k_work *work)
{
	int ret;
	
	LOG_INF("Performing periodic SNTP sync");
	
	ret = sntp_sync_now(DEFAULT_SYNC_TIMEOUT_MS);
	if (ret < 0) {
		LOG_WRN("Periodic SNTP sync failed: %d", ret);
		state.last_sync_result = ret;
	}
	
	/* Schedule next sync if still enabled */
	if (state.periodic_enabled) {
		k_work_schedule(&state.periodic_work, 
			K_SECONDS(state.sync_interval_sec));
	}
}

int sntp_sync_init(void)
{
	if (state.initialized) {
		LOG_WRN("SNTP sync already initialized");
		return -EALREADY;
	}
	
	/* Initialize periodic work */
	k_work_init_delayable(&state.periodic_work, periodic_sync_handler);
	
	state.initialized = true;
	LOG_INF("SNTP sync module initialized");
	
	return 0;
}

int sntp_sync_now(uint32_t timeout_ms)
{
	int ret;
	
	if (!state.initialized) {
		LOG_ERR("SNTP sync not initialized");
		return -ENODEV;
	}
	
	if (timeout_ms == 0) {
		timeout_ms = DEFAULT_SYNC_TIMEOUT_MS;
	}
	
	/* Try primary server first */
	ret = perform_sntp_query(NTP_SERVER_PRIMARY, timeout_ms);
	if (ret == 0) {
		return 0;
	}
	
	LOG_WRN("Primary NTP server failed, trying secondary");
	
	/* Try secondary server */
	ret = perform_sntp_query(NTP_SERVER_SECONDARY, timeout_ms);
	if (ret < 0) {
		LOG_ERR("All NTP servers failed");
		state.last_sync_result = ret;
		return ret;
	}
	
	return 0;
}

int sntp_sync_set_periodic(bool enable, uint32_t interval_seconds)
{
	if (!state.initialized) {
		LOG_ERR("SNTP sync not initialized");
		return -ENODEV;
	}
	
	if (enable) {
		if (interval_seconds < 60) {
			LOG_WRN("Sync interval too short, using 60 seconds");
			interval_seconds = 60;
		}
		
		state.sync_interval_sec = interval_seconds;
		state.periodic_enabled = true;
		
		/* Schedule first sync - delay 60 seconds to ensure network and DNS are stable */
		k_work_schedule(&state.periodic_work, K_SECONDS(60));
		
		LOG_INF("Periodic SNTP sync enabled (interval: %u seconds, first sync in 60s)", 
			interval_seconds);
	} else {
		state.periodic_enabled = false;
		k_work_cancel_delayable(&state.periodic_work);
		
		LOG_INF("Periodic SNTP sync disabled");
	}
	
	return 0;
}

int sntp_sync_get_status(int64_t *last_sync_time_out)
{
	if (!state.initialized) {
		return -ENODEV;
	}
	
	if (last_sync_time_out) {
		*last_sync_time_out = state.last_sync_time;
	}
	
	return state.last_sync_result;
}

int32_t sntp_sync_get_drift(void)
{
	return state.last_drift_sec;
}

