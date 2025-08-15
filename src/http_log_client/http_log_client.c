/**
 * @file http_log_client.c
 * @brief HTTP Log Client implementation
 */

#include "http_log_client.h"
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/logging/log.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <errno.h>

LOG_MODULE_REGISTER(http_log_client, CONFIG_HTTP_LOG_CLIENT_LOG_LEVEL);

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

/* Internal state */
static struct {
    struct http_log_config config;
    struct log_entry *buffer;
    uint16_t head;
    uint16_t tail;
    uint16_t count;
    bool initialized;
    bool enabled;
    struct k_mutex mutex;
    struct k_work_delayable flush_work;
    
    /* File system overflow support */
    bool fs_overflow_enabled;
    uint32_t fs_log_count;
    
    /* Statistics */
    uint32_t logs_sent;
    uint32_t logs_dropped;
    uint32_t send_failures;
    
    /* Backoff state */
    uint32_t backoff_ms;
    uint32_t consecutive_failures;
} log_client;

/* Backoff configuration */
#define INITIAL_BACKOFF_MS      1000   /* 1 second */
#define MAX_BACKOFF_MS          60000  /* 60 seconds */
#define BACKOFF_MULTIPLIER      2

/* HTTP client configuration */
#define HTTP_TIMEOUT_MS         5000
#define HTTP_RECV_BUF_SIZE      512

/* File system overflow configuration */
#define FS_LOG_FILE_PATH        "/lfs/http_logs.bin"
#define FS_LOG_MAGIC            0x4C4F4753  /* "LOGS" */
#define FS_LOG_VERSION          1
#define FS_MAX_LOGS             1000        /* Maximum logs to store on filesystem */

/* File system log header */
struct fs_log_header {
    uint32_t magic;
    uint32_t version;
    uint32_t total_logs;
    uint32_t write_index;
    uint32_t read_index;
};

/* Forward declarations */
static void flush_work_handler(struct k_work *work);
static int send_log_batch(struct log_entry *logs, uint16_t count);
static void update_backoff(bool success);
static int parse_url(const char *url, char *host, size_t host_len, uint16_t *port);
static int fs_log_init(void);
static int fs_log_write(const struct log_entry *entry);
static int fs_log_read(struct log_entry *entry);
static int fs_log_remove(uint32_t count);

http_log_status_t http_log_init(const struct http_log_config *config)
{
    LOG_INF("HTTP log client init called");
    
    if (!config || !config->server_url || !config->device_id) {
        LOG_ERR("Invalid config: config=%p, url=%p, device_id=%p", 
                config, config ? config->server_url : NULL, 
                config ? config->device_id : NULL);
        return HTTP_LOG_ERR_INVALID_PARAM;
    }
    
    if (log_client.initialized) {
        LOG_WRN("HTTP log client already initialized");
        return HTTP_LOG_SUCCESS;
    }
    
    /* Copy configuration */
    log_client.config = *config;
    
    /* Set defaults if not provided */
    if (log_client.config.batch_size == 0) {
        log_client.config.batch_size = 10;  /* Reduced from 50 to fit in smaller buffers */
    }
    if (log_client.config.flush_interval_ms == 0) {
        log_client.config.flush_interval_ms = 5000;
    }
    if (log_client.config.buffer_size == 0) {
        log_client.config.buffer_size = CONFIG_HTTP_LOG_CLIENT_DEFAULT_BUFFER_SIZE;
    }
    
    /* Allocate buffer */
    log_client.buffer = k_malloc(sizeof(struct log_entry) * log_client.config.buffer_size);
    if (!log_client.buffer) {
        LOG_ERR("Failed to allocate log buffer");
        return HTTP_LOG_ERR_INVALID_PARAM;
    }
    
    /* Initialize state */
    log_client.head = 0;
    log_client.tail = 0;
    log_client.count = 0;
    log_client.enabled = true;
    log_client.logs_sent = 0;
    log_client.logs_dropped = 0;
    log_client.send_failures = 0;
    log_client.backoff_ms = INITIAL_BACKOFF_MS;
    log_client.consecutive_failures = 0;
    
    k_mutex_init(&log_client.mutex);
    k_work_init_delayable(&log_client.flush_work, flush_work_handler);
    
    /* Schedule first flush */
    k_work_schedule(&log_client.flush_work, K_MSEC(log_client.config.flush_interval_ms));
    
    log_client.initialized = true;
    
    /* Try to initialize filesystem overflow support */
    if (fs_log_init() == 0) {
        log_client.fs_overflow_enabled = true;
        LOG_INF("Filesystem overflow support enabled");
    } else {
        log_client.fs_overflow_enabled = false;
        LOG_WRN("Filesystem overflow support disabled");
    }
    
    LOG_INF("HTTP log client initialized (buffer: %d, batch: %d, interval: %dms)",
            log_client.config.buffer_size, log_client.config.batch_size,
            log_client.config.flush_interval_ms);
    
    return HTTP_LOG_SUCCESS;
}

http_log_status_t http_log_add(uint8_t level, const char *module, 
                               const char *message, ...)
{
    if (!log_client.initialized) {
        return HTTP_LOG_ERR_NOT_INITIALIZED;
    }
    
    if (!log_client.enabled) {
        return HTTP_LOG_SUCCESS; /* Silently ignore when disabled */
    }
    
    struct log_entry entry;
    va_list args;
    
    /* Prepare log entry */
    entry.timestamp = k_uptime_get();
    entry.level = level;
    strncpy(entry.module, module ? module : "unknown", sizeof(entry.module) - 1);
    entry.module[sizeof(entry.module) - 1] = '\0';
    
    /* Format message */
    va_start(args, message);
    vsnprintf(entry.message, sizeof(entry.message), message, args);
    va_end(args);
    entry.message[sizeof(entry.message) - 1] = '\0';
    
    /* Add to buffer */
    k_mutex_lock(&log_client.mutex, K_FOREVER);
    
    LOG_DBG("Adding log: [%s] %s", module, entry.message);
    
    if (log_client.count >= log_client.config.buffer_size) {
        /* Buffer full - try filesystem overflow */
        if (log_client.fs_overflow_enabled && fs_log_write(&entry) == 0) {
            log_client.fs_log_count++;
            LOG_DBG("Log overflowed to filesystem (fs logs: %d)", log_client.fs_log_count);
        } else {
            /* Drop oldest entry if filesystem is not available or full */
            log_client.tail = (log_client.tail + 1) % log_client.config.buffer_size;
            log_client.logs_dropped++;
            
            /* Log buffer overflow locally (not to HTTP to avoid recursion) */
            LOG_WRN("Log buffer overflow - dropping oldest entry (total dropped: %d)",
                    log_client.logs_dropped);
            
            /* Add new entry */
            log_client.buffer[log_client.head] = entry;
            log_client.head = (log_client.head + 1) % log_client.config.buffer_size;
        }
    } else {
        /* Add new entry to RAM buffer */
        log_client.buffer[log_client.head] = entry;
        log_client.head = (log_client.head + 1) % log_client.config.buffer_size;
        log_client.count++;
    }
    
    /* Check if we should flush immediately */
    if (log_client.count >= log_client.config.batch_size) {
        k_work_reschedule(&log_client.flush_work, K_NO_WAIT);
    }
    
    k_mutex_unlock(&log_client.mutex);
    
    return HTTP_LOG_SUCCESS;
}

static void flush_work_handler(struct k_work *work)
{
    if (!log_client.enabled || (log_client.count == 0 && log_client.fs_log_count == 0)) {
        goto reschedule;
    }
    
    /* Extract batch from buffer - use static buffer to avoid stack overflow */
    static struct log_entry batch[CONFIG_HTTP_LOG_CLIENT_MAX_BATCH_SIZE];
    uint16_t batch_count = 0;
    
    k_mutex_lock(&log_client.mutex, K_FOREVER);
    
    /* First: Read older logs from filesystem (oldest first due to FIFO) */
    while (batch_count < log_client.config.batch_size && log_client.fs_log_count > 0) {
        if (fs_log_read(&batch[batch_count]) == 0) {
            batch_count++;
            log_client.fs_log_count--;
        } else {
            LOG_WRN("Failed to read log from filesystem");
            break;
        }
    }
    
    /* Then: Read newer logs from RAM buffer (also oldest first due to circular buffer) */
    while (batch_count < log_client.config.batch_size && log_client.count > 0) {
        batch[batch_count++] = log_client.buffer[log_client.tail];
        log_client.tail = (log_client.tail + 1) % log_client.config.buffer_size;
        log_client.count--;
    }
    
    k_mutex_unlock(&log_client.mutex);
    
    if (batch_count > 0) {
        /* Attempt to send batch */
        int ret = send_log_batch(batch, batch_count);
        
        if (ret < 0) {
            /* Failed - put logs back in buffer if there's room */
            k_mutex_lock(&log_client.mutex, K_FOREVER);
            
            for (int i = batch_count - 1; i >= 0 && log_client.count < log_client.config.buffer_size; i--) {
                /* Put back at tail (oldest position) */
                log_client.tail = (log_client.tail - 1 + log_client.config.buffer_size) % log_client.config.buffer_size;
                log_client.buffer[log_client.tail] = batch[i];
                log_client.count++;
            }
            
            k_mutex_unlock(&log_client.mutex);
            
            update_backoff(false);
        } else {
            update_backoff(true);
            log_client.logs_sent += batch_count;
        }
    }
    
reschedule:
    /* Schedule next flush with backoff if we've been failing */
    uint32_t next_flush = log_client.consecutive_failures > 0 ? 
                         log_client.backoff_ms : log_client.config.flush_interval_ms;
    k_work_schedule(&log_client.flush_work, K_MSEC(next_flush));
}

static int send_log_batch(struct log_entry *logs, uint16_t count)
{
    char json_buf[512];  /* Reduced from 2048 to prevent stack overflow */
    char host[64];       /* Reduced from 128 */
    uint16_t port;
    int sock = -1;
    int ret;
    
    /* Don't even try if we know the server is unreachable */
    if (log_client.consecutive_failures > 5) {
        LOG_DBG("Skipping send due to consecutive failures");
        return -1;
    }
    
    /* Build JSON payload */
    ret = snprintf(json_buf, sizeof(json_buf), 
                   "{\"source\":\"%s\",\"logs\":[", log_client.config.device_id);
    
    for (uint16_t i = 0; i < count; i++) {
        const char *level_str;
        switch (logs[i].level) {
            case LOG_LEVEL_ERR: level_str = "error"; break;
            case LOG_LEVEL_WRN: level_str = "warn"; break;
            case LOG_LEVEL_INF: level_str = "info"; break;
            case LOG_LEVEL_DBG: level_str = "debug"; break;
            default: level_str = "unknown"; break;
        }
        
        /* Escape quotes in message */
        char escaped_msg[256];
        const char *src = logs[i].message;
        char *dst = escaped_msg;
        size_t remaining = sizeof(escaped_msg) - 1;
        
        while (*src && remaining > 1) {
            if (*src == '"' || *src == '\\') {
                if (remaining > 2) {
                    *dst++ = '\\';
                    remaining--;
                }
            }
            *dst++ = *src++;
            remaining--;
        }
        *dst = '\0';
        
        ret += snprintf(json_buf + ret, sizeof(json_buf) - ret,
                       "%s{\"timestamp\":%lld,\"level\":\"%s\",\"module\":\"%s\",\"message\":\"%s\"}",
                       i > 0 ? "," : "",
                       logs[i].timestamp,
                       level_str,
                       logs[i].module,
                       escaped_msg);
        
        if (ret >= sizeof(json_buf) - 10) {
            LOG_WRN("JSON buffer too small, truncating batch");
            break;
        }
    }
    
    ret += snprintf(json_buf + ret, sizeof(json_buf) - ret, "]}");
    
    /* Parse URL */
    if (parse_url(log_client.config.server_url, host, sizeof(host), &port) < 0) {
        LOG_ERR("Invalid server URL: %s", log_client.config.server_url);
        return -1;
    }
    
    LOG_INF("Sending %d logs to %s:%d", count, host, port);
    
    /* Create socket and connect */
    struct sockaddr_in addr;
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        LOG_ERR("Failed to create socket: %d", errno);
        return -1;
    }
    
    /* Set socket timeout */
    struct timeval timeout = {
        .tv_sec = HTTP_TIMEOUT_MS / 1000,
        .tv_usec = (HTTP_TIMEOUT_MS % 1000) * 1000
    };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    
    /* Connect to server */
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    
    /* Try to resolve hostname - simplified version using static IP */
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        /* For now, fallback to hardcoded IP if not a valid IP string */
        inet_pton(AF_INET, "192.168.2.62", &addr.sin_addr);
    }
    
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_ERR("Failed to connect to log server %s:%d: %d", host, port, errno);
        close(sock);
        log_client.send_failures++;
        return -1;
    }
    
    /* Build and send HTTP request */
    char http_req[640];  /* Reduced from 2560 to prevent stack overflow */
    int req_len = snprintf(http_req, sizeof(http_req),
                          "POST /logs HTTP/1.1\r\n"
                          "Host: %s:%d\r\n"
                          "Content-Type: application/json\r\n"
                          "Content-Length: %d\r\n"
                          "Connection: close\r\n"
                          "\r\n"
                          "%s",
                          host, port, strlen(json_buf), json_buf);
    
    if (send(sock, http_req, req_len, 0) != req_len) {
        LOG_ERR("Failed to send HTTP request: %d", errno);
        close(sock);
        log_client.send_failures++;
        return -1;
    }
    
    /* Read response - just check status line */
    char response[256];
    int recv_len = recv(sock, response, sizeof(response) - 1, 0);
    if (recv_len > 0) {
        response[recv_len] = '\0';
        if (strstr(response, "200 OK") || strstr(response, "201 Created")) {
            LOG_DBG("Successfully sent %d logs", count);
            ret = 0;
        } else {
            LOG_WRN("Server returned error: %.50s", response);
            log_client.send_failures++;
            ret = -1;
        }
    } else {
        LOG_ERR("Failed to receive response: %d", errno);
        log_client.send_failures++;
        ret = -1;
    }
    
    close(sock);
    return ret;
}

static void update_backoff(bool success)
{
    if (success) {
        log_client.consecutive_failures = 0;
        log_client.backoff_ms = INITIAL_BACKOFF_MS;
    } else {
        log_client.consecutive_failures++;
        log_client.backoff_ms = MIN(log_client.backoff_ms * BACKOFF_MULTIPLIER, MAX_BACKOFF_MS);
        LOG_WRN("Log send failed (%d consecutive), backing off to %dms",
                log_client.consecutive_failures, log_client.backoff_ms);
    }
}

int http_log_flush(void)
{
    if (!log_client.initialized || !log_client.enabled) {
        return -1;
    }
    
    /* Cancel scheduled flush and do it now */
    k_work_cancel_delayable(&log_client.flush_work);
    
    int total_sent = 0;
    
    /* Flush all logs in batches - use static buffer to avoid stack overflow */
    static struct log_entry flush_batch[CONFIG_HTTP_LOG_CLIENT_MAX_BATCH_SIZE];
    
    /* First: Flush all filesystem logs (older logs) */
    while (log_client.fs_log_count > 0) {
        uint16_t batch_count = 0;
        
        k_mutex_lock(&log_client.mutex, K_FOREVER);
        
        /* Read filesystem logs into batch */
        while (batch_count < log_client.config.batch_size && log_client.fs_log_count > 0) {
            if (fs_log_read(&flush_batch[batch_count]) == 0) {
                batch_count++;
                log_client.fs_log_count--;
            } else {
                LOG_WRN("Failed to read log from filesystem during flush");
                break;
            }
        }
        
        k_mutex_unlock(&log_client.mutex);
        
        if (batch_count > 0) {
            if (send_log_batch(flush_batch, batch_count) == 0) {
                total_sent += batch_count;
                log_client.logs_sent += batch_count;
            } else {
                /* On failure, we can't put filesystem logs back easily, so just log and continue */
                LOG_ERR("Failed to send filesystem logs during flush");
                log_client.send_failures++;
                break;
            }
        }
    }
    
    /* Then: Flush all RAM buffer logs (newer logs) */
    while (log_client.count > 0) {
        uint16_t batch_count = 0;
        
        k_mutex_lock(&log_client.mutex, K_FOREVER);
        
        while (batch_count < log_client.config.batch_size && log_client.count > 0) {
            flush_batch[batch_count++] = log_client.buffer[log_client.tail];
            log_client.tail = (log_client.tail + 1) % log_client.config.buffer_size;
            log_client.count--;
        }
        
        k_mutex_unlock(&log_client.mutex);
        
        if (send_log_batch(flush_batch, batch_count) == 0) {
            total_sent += batch_count;
            log_client.logs_sent += batch_count;
        } else {
            /* Put back failed logs and stop trying */
            k_mutex_lock(&log_client.mutex, K_FOREVER);
            
            for (int i = batch_count - 1; i >= 0 && log_client.count < log_client.config.buffer_size; i--) {
                log_client.tail = (log_client.tail - 1 + log_client.config.buffer_size) % log_client.config.buffer_size;
                log_client.buffer[log_client.tail] = flush_batch[i];
                log_client.count++;
            }
            
            k_mutex_unlock(&log_client.mutex);
            break;
        }
    }
    
    /* Reschedule regular flush */
    k_work_schedule(&log_client.flush_work, K_MSEC(log_client.config.flush_interval_ms));
    
    return total_sent;
}

void http_log_enable(bool enable)
{
    log_client.enabled = enable;
    LOG_INF("HTTP log client %s", enable ? "enabled" : "disabled");
}

uint16_t http_log_get_buffer_count(void)
{
    return log_client.count;
}

void http_log_get_stats(uint32_t *sent, uint32_t *dropped, uint32_t *failed)
{
    if (sent) *sent = log_client.logs_sent;
    if (dropped) *dropped = log_client.logs_dropped;
    if (failed) *failed = log_client.send_failures;
}

void http_log_shutdown(void)
{
    if (!log_client.initialized) {
        return;
    }
    
    LOG_INF("Shutting down HTTP log client...");
    
    /* Cancel work and flush remaining logs */
    k_work_cancel_delayable(&log_client.flush_work);
    http_log_flush();
    
    /* Free resources */
    k_free(log_client.buffer);
    
    /* Reset state */
    memset(&log_client, 0, sizeof(log_client));
}

/* Parse URL to extract host and port */
static int parse_url(const char *url, char *host, size_t host_len, uint16_t *port)
{
    const char *p;
    
    /* Skip protocol */
    p = strstr(url, "://");
    if (p) {
        p += 3;
    } else {
        p = url;
    }
    
    /* Extract host:port */
    const char *colon = strchr(p, ':');
    const char *slash = strchr(p, '/');
    
    if (colon && (!slash || colon < slash)) {
        /* Port specified */
        size_t len = colon - p;
        if (len >= host_len) return -1;
        memcpy(host, p, len);
        host[len] = '\0';
        *port = atoi(colon + 1);
    } else {
        /* No port, use default */
        size_t len = slash ? (slash - p) : strlen(p);
        if (len >= host_len) return -1;
        memcpy(host, p, len);
        host[len] = '\0';
        *port = 3000; /* Default port */
    }
    
    return 0;
}

/* Filesystem overflow functions */
#ifdef CONFIG_FILE_SYSTEM
#include <zephyr/fs/fs.h>

static int fs_log_init(void)
{
    struct fs_file_t file;
    struct fs_log_header header;
    int ret;
    
    fs_file_t_init(&file);
    
    /* Try to open existing file */
    ret = fs_open(&file, FS_LOG_FILE_PATH, FS_O_RDWR);
    if (ret < 0) {
        /* File doesn't exist, create it */
        ret = fs_open(&file, FS_LOG_FILE_PATH, FS_O_CREATE | FS_O_RDWR);
        if (ret < 0) {
            LOG_ERR("Failed to create log file: %d", ret);
            return ret;
        }
        
        /* Initialize header */
        header.magic = FS_LOG_MAGIC;
        header.version = FS_LOG_VERSION;
        header.total_logs = 0;
        header.write_index = 0;
        header.read_index = 0;
        
        ret = fs_write(&file, &header, sizeof(header));
        if (ret != sizeof(header)) {
            LOG_ERR("Failed to write log header: %d", ret);
            fs_close(&file);
            return -EIO;
        }
    } else {
        /* Read existing header */
        ret = fs_read(&file, &header, sizeof(header));
        if (ret != sizeof(header) || header.magic != FS_LOG_MAGIC || 
            header.version != FS_LOG_VERSION) {
            LOG_ERR("Invalid log file header");
            fs_close(&file);
            return -EINVAL;
        }
        
        /* Update fs_log_count based on existing logs */
        log_client.fs_log_count = header.total_logs;
        LOG_INF("Found %d existing logs in filesystem", log_client.fs_log_count);
    }
    
    fs_close(&file);
    return 0;
}

static int fs_log_write(const struct log_entry *entry)
{
    struct fs_file_t file;
    struct fs_log_header header;
    int ret;
    off_t offset;
    
    fs_file_t_init(&file);
    
    ret = fs_open(&file, FS_LOG_FILE_PATH, FS_O_RDWR);
    if (ret < 0) {
        return ret;
    }
    
    /* Read header */
    ret = fs_read(&file, &header, sizeof(header));
    if (ret != sizeof(header)) {
        fs_close(&file);
        return -EIO;
    }
    
    /* Check if we've reached max logs */
    if (header.total_logs >= FS_MAX_LOGS) {
        fs_close(&file);
        return -ENOSPC;
    }
    
    /* Calculate write position */
    offset = sizeof(header) + (header.write_index * sizeof(struct log_entry));
    
    /* Seek to write position */
    ret = fs_seek(&file, offset, FS_SEEK_SET);
    if (ret < 0) {
        fs_close(&file);
        return ret;
    }
    
    /* Write log entry */
    ret = fs_write(&file, entry, sizeof(*entry));
    if (ret != sizeof(*entry)) {
        fs_close(&file);
        return -EIO;
    }
    
    /* Update header */
    header.write_index = (header.write_index + 1) % FS_MAX_LOGS;
    header.total_logs++;
    
    /* Write updated header */
    fs_seek(&file, 0, FS_SEEK_SET);
    ret = fs_write(&file, &header, sizeof(header));
    
    fs_close(&file);
    return (ret == sizeof(header)) ? 0 : -EIO;
}

static int fs_log_read(struct log_entry *entry)
{
    struct fs_file_t file;
    struct fs_log_header header;
    int ret;
    off_t offset;
    
    fs_file_t_init(&file);
    
    ret = fs_open(&file, FS_LOG_FILE_PATH, FS_O_RDWR);
    if (ret < 0) {
        return ret;
    }
    
    /* Read header */
    ret = fs_read(&file, &header, sizeof(header));
    if (ret != sizeof(header)) {
        fs_close(&file);
        return -EIO;
    }
    
    /* Check if there are logs to read */
    if (header.total_logs == 0 || header.read_index == header.write_index) {
        fs_close(&file);
        return -ENOENT;
    }
    
    /* Calculate read position */
    offset = sizeof(header) + (header.read_index * sizeof(struct log_entry));
    
    /* Seek to read position */
    ret = fs_seek(&file, offset, FS_SEEK_SET);
    if (ret < 0) {
        fs_close(&file);
        return ret;
    }
    
    /* Read log entry */
    ret = fs_read(&file, entry, sizeof(*entry));
    if (ret != sizeof(*entry)) {
        fs_close(&file);
        return -EIO;
    }
    
    /* Update header */
    header.read_index = (header.read_index + 1) % FS_MAX_LOGS;
    header.total_logs--;
    
    /* Write updated header */
    fs_seek(&file, 0, FS_SEEK_SET);
    ret = fs_write(&file, &header, sizeof(header));
    
    fs_close(&file);
    return (ret == sizeof(header)) ? 0 : -EIO;
}

static int fs_log_remove(uint32_t count)
{
    /* This function can be implemented if needed to remove multiple logs at once */
    return 0;
}

#else /* !CONFIG_FILE_SYSTEM */

/* Stub implementations when filesystem is not available */
static int fs_log_init(void) { return -ENOTSUP; }
static int fs_log_write(const struct log_entry *entry) { return -ENOTSUP; }
static int fs_log_read(struct log_entry *entry) { return -ENOTSUP; }
static int fs_log_remove(uint32_t count) { return -ENOTSUP; }

#endif /* CONFIG_FILE_SYSTEM */
