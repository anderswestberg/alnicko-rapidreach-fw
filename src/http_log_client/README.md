# HTTP Log Client Module

This module provides buffered, batched HTTP log transmission from the RapidReach device to a centralized log server.

## Features

- **Buffered Logging**: Logs are stored in a circular buffer to handle network outages
- **Batch Transmission**: Logs are sent in configurable batches to reduce network overhead
- **Automatic Retry**: Failed transmissions are retried with exponential backoff
- **Buffer Overflow Protection**: When buffer is full, oldest logs are dropped with a warning
- **Non-blocking Operation**: Log submission is asynchronous and won't block your application
- **Statistics**: Track sent, dropped, and failed log counts
- **Filesystem Overflow**: Optional filesystem storage for logs when RAM buffer is full
- **Chronological Ordering**: Logs are sent in correct time order, with filesystem logs (older) sent before RAM logs (newer)

## Configuration

Add to your `prj.conf`:

```conf
# Enable HTTP log client
CONFIG_HTTP_LOG_CLIENT=y
CONFIG_HTTP_LOG_CLIENT_LOG_LEVEL=3

# Configure buffer and batch sizes
CONFIG_HTTP_LOG_CLIENT_MAX_BATCH_SIZE=50
CONFIG_HTTP_LOG_CLIENT_DEFAULT_BUFFER_SIZE=500

# Default server (can be overridden at runtime)
CONFIG_HTTP_LOG_CLIENT_DEFAULT_SERVER_URL="http://192.168.2.62:3000"
```

## Usage Example

```c
#include "http_log_client.h"

/* Initialize the log client */
struct http_log_config config = {
    .server_url = "http://192.168.2.62:3000",
    .device_id = "speaker-001",
    .batch_size = 50,
    .flush_interval_ms = 5000,
    .buffer_size = 500,
    .enable_compression = false
};

int ret = http_log_init(&config);
if (ret != HTTP_LOG_SUCCESS) {
    LOG_ERR("Failed to initialize HTTP log client: %d", ret);
}

/* Use convenience macros (uses current LOG_MODULE_NAME) */
HTTP_LOG_INF("Device started successfully");
HTTP_LOG_ERR("Connection failed: %d", error_code);

/* Or use the full function */
http_log_add(LOG_LEVEL_WRN, "custom_module", "Low battery: %d%%", battery_level);

/* Check statistics */
uint32_t sent, dropped, failed;
http_log_get_stats(&sent, &dropped, &failed);
LOG_INF("Log stats - Sent: %d, Dropped: %d, Failed: %d", sent, dropped, failed);

/* Force flush before shutdown */
int flushed = http_log_flush();
LOG_INF("Flushed %d logs", flushed);

/* Shutdown cleanly */
http_log_shutdown();
```

## Integration with Existing Code

To integrate with existing modules, you can redirect their logs:

```c
/* In your module's log handler */
void my_module_log_handler(uint8_t level, const char *fmt, ...)
{
    char msg[256];
    va_list args;
    
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    
    /* Send to both local log and HTTP log */
    LOG_PRINTK("%s", msg);
    http_log_add(level, "my_module", "%s", msg);
}
```

## Backoff Strategy

The module implements exponential backoff for failed transmissions:

1. Initial retry after 1 second
2. Double the delay after each failure (2s, 4s, 8s...)
3. Maximum backoff of 60 seconds
4. Reset to initial delay after successful transmission
5. Skip sending attempts after 5 consecutive failures until next scheduled flush

## Chronological Ordering

The HTTP log client ensures logs are sent to the server in chronological order:

1. **Filesystem logs are sent first** - These are older logs that overflowed when the RAM buffer was full
2. **RAM buffer logs are sent second** - These are newer logs currently in memory
3. **Both use FIFO ordering** - Within each storage type, oldest logs are sent first

This design requires no sorting (saving memory on embedded devices) while guaranteeing that the log server receives entries in the correct time sequence. The implementation takes advantage of the fact that filesystem logs are always older than RAM logs by design.

## Buffer Management

- Circular buffer holds configured number of log entries
- When buffer fills, oldest entries are dropped
- A warning is logged locally (not sent to HTTP) on each drop
- Buffer count can be monitored with `http_log_get_buffer_count()`

## Network Considerations

- The module doesn't require constant connectivity
- Logs are buffered during network outages
- Automatic retry when network returns
- Configurable timeouts for HTTP requests
- Connection failures don't block the application

## Performance Tips

1. Adjust batch size based on your network latency
2. Increase buffer size if you expect extended offline periods
3. Use longer flush intervals for power-sensitive applications
4. Monitor dropped logs to detect buffer size issues
5. Disable logging temporarily with `http_log_enable(false)` during critical operations
