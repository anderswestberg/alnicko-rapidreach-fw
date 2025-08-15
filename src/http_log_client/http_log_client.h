/**
 * @file http_log_client.h
 * @brief HTTP Log Client for sending device logs to centralized log server
 * 
 * This module provides buffered, batched log transmission with automatic
 * retry and backoff strategies. Logs are queued locally and sent in batches
 * to reduce network overhead.
 * 
 * @author AI Assistant
 * @copyright (C) 2025 RapidReach. All rights reserved.
 */

#ifndef HTTP_LOG_CLIENT_H
#define HTTP_LOG_CLIENT_H

#include <stdint.h>
#include <stdbool.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

/**
 * @brief Log entry structure
 */
struct log_entry {
    int64_t timestamp;      /**< Unix timestamp in milliseconds */
    uint8_t level;          /**< Log level (LOG_LEVEL_ERR, LOG_LEVEL_WRN, etc.) */
    char module[32];        /**< Module name */
    char message[256];      /**< Log message */
};

/**
 * @brief HTTP log client configuration
 */
struct http_log_config {
    const char *server_url;     /**< Log server URL (e.g., "http://192.168.2.62:3000") */
    const char *device_id;      /**< Device identifier */
    uint16_t batch_size;        /**< Maximum logs per batch (default: 50) */
    uint32_t flush_interval_ms; /**< Auto-flush interval in ms (default: 5000) */
    uint16_t buffer_size;       /**< Maximum logs to buffer (default: 500) */
    bool enable_compression;    /**< Enable gzip compression (default: false) */
};

/**
 * @brief HTTP log client status
 */
typedef enum {
    HTTP_LOG_SUCCESS = 0,
    HTTP_LOG_ERR_NOT_INITIALIZED = -1,
    HTTP_LOG_ERR_BUFFER_FULL = -2,
    HTTP_LOG_ERR_NETWORK = -3,
    HTTP_LOG_ERR_SERVER = -4,
    HTTP_LOG_ERR_INVALID_PARAM = -5,
} http_log_status_t;

/**
 * @brief Initialize the HTTP log client
 * 
 * @param config Configuration parameters
 * @return HTTP_LOG_SUCCESS on success, error code otherwise
 */
http_log_status_t http_log_init(const struct http_log_config *config);

/**
 * @brief Add a log entry to the buffer
 * 
 * This function is non-blocking and adds the log to an internal buffer.
 * If the buffer is full, the oldest log entry will be dropped and a
 * warning will be logged locally.
 * 
 * @param level Log level
 * @param module Module name
 * @param message Log message (printf format)
 * @param ... Variable arguments for message formatting
 * @return HTTP_LOG_SUCCESS on success, error code otherwise
 */
http_log_status_t http_log_add(uint8_t level, const char *module, 
                               const char *message, ...);

/**
 * @brief Force flush all buffered logs
 * 
 * This function will attempt to send all buffered logs immediately.
 * It's blocking and should be used sparingly (e.g., before shutdown).
 * 
 * @return Number of logs successfully sent, negative on error
 */
int http_log_flush(void);

/**
 * @brief Enable or disable HTTP log client
 * 
 * When disabled, logs are not buffered or sent. Useful for testing
 * or when network is known to be unavailable.
 * 
 * @param enable true to enable, false to disable
 */
void http_log_enable(bool enable);

/**
 * @brief Get current buffer usage
 * 
 * @return Number of logs currently buffered
 */
uint16_t http_log_get_buffer_count(void);

/**
 * @brief Get statistics about log transmission
 * 
 * @param sent Number of logs successfully sent (output)
 * @param dropped Number of logs dropped due to buffer overflow (output)
 * @param failed Number of failed transmission attempts (output)
 */
void http_log_get_stats(uint32_t *sent, uint32_t *dropped, uint32_t *failed);

/**
 * @brief Shutdown the HTTP log client
 * 
 * This will attempt to flush remaining logs before shutting down.
 */
void http_log_shutdown(void);

/* Convenience macros for logging with automatic module name */
#ifdef LOG_MODULE_NAME
#define HTTP_LOG_MODULE LOG_MODULE_NAME
#else
#define HTTP_LOG_MODULE "app"
#endif

#define HTTP_LOG_ERR(fmt, ...) \
    http_log_add(LOG_LEVEL_ERR, HTTP_LOG_MODULE, fmt, ##__VA_ARGS__)

#define HTTP_LOG_WRN(fmt, ...) \
    http_log_add(LOG_LEVEL_WRN, HTTP_LOG_MODULE, fmt, ##__VA_ARGS__)

#define HTTP_LOG_INF(fmt, ...) \
    http_log_add(LOG_LEVEL_INF, HTTP_LOG_MODULE, fmt, ##__VA_ARGS__)

#define HTTP_LOG_DBG(fmt, ...) \
    http_log_add(LOG_LEVEL_DBG, HTTP_LOG_MODULE, fmt, ##__VA_ARGS__)

#endif /* HTTP_LOG_CLIENT_H */
