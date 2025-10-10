/*
 * Copyright (c) 2025 RapidReach
 * 
 * Shell MQTT connection monitor
 */

#ifndef SHELL_MQTT_MONITOR_H_
#define SHELL_MQTT_MONITOR_H_

#include <stdbool.h>

/**
 * @brief Check if shell MQTT is connected
 * 
 * This function monitors the console output for the shell MQTT
 * connection message "MQTT client connected!" which indicates
 * the shell backend has successfully connected.
 * 
 * @return true if shell MQTT is connected, false otherwise
 */
bool is_shell_mqtt_connected(void);

/**
 * @brief Mark shell MQTT as connected
 * 
 * This should be called when we detect the shell MQTT connection
 * message in the logs.
 */
void mark_shell_mqtt_connected(void);

/**
 * @brief Reset shell MQTT connection status
 * 
 * Call this on network disconnect to reset the status.
 */
void reset_shell_mqtt_status(void);

#endif /* SHELL_MQTT_MONITOR_H_ */
