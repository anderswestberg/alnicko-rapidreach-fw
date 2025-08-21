#pragma once

#include <zephyr/kernel.h>

int mqtt_log_client_init(void);
int mqtt_log_client_put(const char *level, const char *message, uint64_t timestamp_ms);
int mqtt_log_client_flush(void);




