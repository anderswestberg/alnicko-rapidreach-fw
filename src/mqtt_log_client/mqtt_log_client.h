#pragma once

#include <zephyr/kernel.h>

int mqtt_log_client_init(void);
int mqtt_log_client_put(const char *level, const char *message, uint64_t timestamp_ms);
int mqtt_log_client_flush(void);

/* Get status for debugging */
void mqtt_log_client_get_status(bool *initialized, bool *fs_overflow_enabled, 
                                 size_t *buffer_count, size_t *buffer_capacity,
                                 size_t *fs_log_count);




