/**
 * @file log_backend_mqtt.c
 * @brief Zephyr log backend for MQTT logging
 * 
 * This backend captures all Zephyr logs and forwards them to the MQTT log client
 */

#include <zephyr/logging/log_backend.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/logging/log_output.h>
#include <zephyr/logging/log_msg.h>
#include <zephyr/kernel.h>
#include <string.h>
#include "mqtt_log_client.h"

#ifdef CONFIG_LOG_BACKEND_MQTT

#define MQTT_LOG_BACKEND_BUF_SIZE 256

static uint8_t mqtt_log_buf[MQTT_LOG_BACKEND_BUF_SIZE];
static uint32_t log_format_current = CONFIG_LOG_BACKEND_MQTT_OUTPUT_DEFAULT;

static void mqtt_log_backend_process(const struct log_backend *const backend,
                                    union log_msg_generic *msg)
{
    char str[MQTT_LOG_BACKEND_BUF_SIZE];
    const char *level_str = "info";
    size_t len;
    uint32_t flags = 0;
    
    /* Get the log level from the message header */
    uint8_t level = log_msg_get_level(&msg->log);
    
    switch (level) {
    case LOG_LEVEL_ERR:
        level_str = "error";
        break;
    case LOG_LEVEL_WRN:
        level_str = "warn";
        break;
    case LOG_LEVEL_INF:
        level_str = "info";
        break;
    case LOG_LEVEL_DBG:
        level_str = "debug";
        break;
    }
    
    /* Extract the log message - simplified approach */
    /* For now, just get the raw string data */
    const char *msg_str = log_msg_get_package(&msg->log, &len);
    if (msg_str && len > 0) {
        /* Copy to our buffer */
        size_t copy_len = len < sizeof(str) - 1 ? len : sizeof(str) - 1;
        memcpy(str, msg_str, copy_len);
        str[copy_len] = '\0';
        
        /* Get timestamp */
        uint64_t timestamp_ms = k_uptime_get();
        
        /* Send to MQTT log client */
        mqtt_log_client_put(level_str, str, timestamp_ms);
    }
}

static void mqtt_log_backend_panic(const struct log_backend *const backend)
{
    /* Try to flush any pending logs */
    mqtt_log_client_flush();
}

static void mqtt_log_backend_init(const struct log_backend *const backend)
{
    /* Initialize the MQTT log client */
    mqtt_log_client_init();
}

static void mqtt_log_backend_notify(const struct log_backend *const backend,
                                   enum log_backend_evt event, void *arg)
{
    switch (event) {
    case LOG_BACKEND_EVT_PROCESS_THREAD_DONE:
        /* Periodic flush opportunity */
        mqtt_log_client_flush();
        break;
    default:
        break;
    }
}

static const struct log_backend_api mqtt_log_backend_api = {
    .process = mqtt_log_backend_process,
    .panic = mqtt_log_backend_panic,
    .init = mqtt_log_backend_init,
    .notify = mqtt_log_backend_notify,
};

LOG_BACKEND_DEFINE(mqtt_log_backend, mqtt_log_backend_api, true);

#endif /* CONFIG_LOG_BACKEND_MQTT */
