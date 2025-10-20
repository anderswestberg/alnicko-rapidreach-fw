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

static size_t mqtt_log_buf_pos = 0;

/* Log output processing function */
static int mqtt_log_output(uint8_t *data, size_t length, void *ctx)
{
    ARG_UNUSED(ctx);
    
    /* Capture formatted log output into buffer */
    size_t remaining = MQTT_LOG_BACKEND_BUF_SIZE - mqtt_log_buf_pos - 1;
    if (remaining > 0 && length > 0) {
        size_t copy_len = (length > remaining) ? remaining : length;
        memcpy(&mqtt_log_buf[mqtt_log_buf_pos], data, copy_len);
        mqtt_log_buf_pos += copy_len;
        mqtt_log_buf[mqtt_log_buf_pos] = '\0';
    }
    return (int)length;
}

LOG_OUTPUT_DEFINE(mqtt_log_output_instance, mqtt_log_output, mqtt_log_buf, MQTT_LOG_BACKEND_BUF_SIZE);

static void mqtt_log_backend_process(const struct log_backend *const backend,
                                    union log_msg_generic *msg)
{
    const char *level_str = "info";
    uint32_t flags = 0;  /* We'll format just the message content */
    
    /* Reset buffer position */
    mqtt_log_buf_pos = 0;
    mqtt_log_buf[0] = '\0';
    
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
    
    /* Format the log message using the log output API */
    log_output_msg_process(&mqtt_log_output_instance, &msg->log, flags);
    
    /* Get timestamp - use uptime for now, RTC conversion happens at publish time */
    uint64_t timestamp_ms = k_uptime_get();
    
    /* Send to MQTT log client if we have a message */
    if (mqtt_log_buf_pos > 0) {
        /* Remove trailing newline if present */
        if (mqtt_log_buf[mqtt_log_buf_pos-1] == '\n') {
            mqtt_log_buf[mqtt_log_buf_pos-1] = '\0';
            mqtt_log_buf_pos--;
        }
        
        mqtt_log_client_put(level_str, (char *)mqtt_log_buf, timestamp_ms);
    }
}

static void mqtt_log_backend_panic(const struct log_backend *const backend)
{
    /* Try to flush any pending logs */
    mqtt_log_client_flush();
}

static void mqtt_log_backend_init(const struct log_backend *const backend)
{
    /* Use printk since logging might not be ready yet */
    printk("MQTT log backend initializing...\n");
    
    /* Initialize the MQTT log client */
    mqtt_log_client_init();
}

static void mqtt_log_backend_notify(const struct log_backend *const backend,
                                   enum log_backend_evt event, union log_backend_evt_arg *arg)
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
