/*
 * Copyright (c) 2025 RapidReach
 * 
 * Shell MQTT connection monitor implementation
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "shell_mqtt_monitor.h"

LOG_MODULE_REGISTER(shell_mqtt_monitor, LOG_LEVEL_INF);

static atomic_t shell_mqtt_connected = ATOMIC_INIT(0);

bool is_shell_mqtt_connected(void)
{
    return atomic_get(&shell_mqtt_connected) != 0;
}

void mark_shell_mqtt_connected(void)
{
    atomic_set(&shell_mqtt_connected, 1);
    LOG_INF("Shell MQTT marked as connected");
}

void reset_shell_mqtt_status(void)
{
    atomic_set(&shell_mqtt_connected, 0);
    LOG_INF("Shell MQTT status reset");
}
