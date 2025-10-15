/**
 * @file rtc.с
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

#include <zephyr/device.h>
#include <zephyr/sys/util.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <time.h>
#include "rtc.h"

LOG_MODULE_REGISTER(rtc, CONFIG_RPR_MODULE_RTC_LOG_LEVEL);

#define RTC_YEAR_OFFSET  1900
#define RTC_MONTH_OFFSET 1

#define RTC_DEVICE_NODE DT_ALIAS(rtc)
#define RTC_DEVICE_SPEC DEVICE_DT_GET(RTC_DEVICE_NODE)

struct rtc_config {
    const struct device *const rtc;
    bool                       is_ready;
};

static struct rtc_config rtc_cfg = {
    .rtc      = RTC_DEVICE_SPEC,
    .is_ready = false,
};

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
int set_date_time(struct rtc_time *rtc_data)
{
    if (!rtc_cfg.is_ready) {
        LOG_ERR("RTC device not ready");
        return -ENODEV;
    }

    if (!rtc_data) {
        LOG_ERR("Invalid rtc_data pointer");
        return -EINVAL;
    }

    rtc_data->tm_year -= RTC_YEAR_OFFSET;
    rtc_data->tm_mon -= RTC_MONTH_OFFSET;

    struct rtc_time rtctime = { 0 };
    struct tm      *tm_time = rtc_time_to_tm(&rtctime);

    tm_time->tm_year = rtc_data->tm_year;
    tm_time->tm_mon  = rtc_data->tm_mon;
    tm_time->tm_mday = rtc_data->tm_mday;
    tm_time->tm_hour = rtc_data->tm_hour;
    tm_time->tm_min  = rtc_data->tm_min;
    tm_time->tm_sec  = rtc_data->tm_sec;
    tm_time->tm_wday = -1;  /* Let RTC driver calculate */
    tm_time->tm_yday = -1;  /* Let RTC driver calculate */
    tm_time->tm_isdst = -1; /* Unknown DST status */
    
    LOG_DBG("RTC set attempt: year=%d mon=%d mday=%d %02d:%02d:%02d",
            tm_time->tm_year, tm_time->tm_mon, tm_time->tm_mday,
            tm_time->tm_hour, tm_time->tm_min, tm_time->tm_sec);

    int ret = rtc_set_time(rtc_cfg.rtc, &rtctime);
    if (ret < 0) {
        LOG_ERR("Cannot write date time: %d", ret);
        return ret;
    }

    LOG_DBG("RTC time successfully set");
    return ret;
}

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
int get_date_time(struct rtc_time *rtc_data)
{
    if (!rtc_cfg.is_ready) {
        LOG_ERR("RTC device not ready");
        return -ENODEV;
    }

    if (!rtc_data) {
        LOG_ERR("Invalid rtc_data pointer");
        return -EINVAL;
    }

    int ret = rtc_get_time(rtc_cfg.rtc, rtc_data);
    if (ret < 0) {
        LOG_ERR("Cannot read date time: %d", ret);
        return ret;
    }

    rtc_data->tm_year += RTC_YEAR_OFFSET;
    rtc_data->tm_mon += RTC_MONTH_OFFSET;

    LOG_DBG("RTC time successfully read: %04d-%02d-%02d %02d:%02d:%02d",
            rtc_data->tm_year,
            rtc_data->tm_mon,
            rtc_data->tm_mday,
            rtc_data->tm_hour,
            rtc_data->tm_min,
            rtc_data->tm_sec);

    return ret;
}

/**
 * @brief Initialize the RTC module.
 *
 * This function checks if the RTC device is ready and reads the current
 * date and time. 
 * 
 * @return 0 on success, or a negative error code on failure.
 */
static int rtc_init(void)
{
    if (!device_is_ready(rtc_cfg.rtc)) {
        LOG_ERR("RTC device not ready");
        return -ENODEV;
    }

    rtc_cfg.is_ready = true;

    struct rtc_time rtc_data;
    int             ret = rtc_get_time(rtc_cfg.rtc, &rtc_data);

    if (ret == -ENODATA) {
        LOG_WRN("RTC time not set or RTC backup battery missing");
    } else if (ret < 0) {
        LOG_ERR("Cannot read date time: %d", ret);
    } else {
        LOG_INF("RTC device is ready: %04d-%02d-%02d %02d:%02d:%02d",
                rtc_data.tm_year + RTC_YEAR_OFFSET,
                rtc_data.tm_mon + RTC_MONTH_OFFSET,
                rtc_data.tm_mday,
                rtc_data.tm_hour,
                rtc_data.tm_min,
                rtc_data.tm_sec);
    }

    return ret;
}

SYS_INIT(rtc_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
