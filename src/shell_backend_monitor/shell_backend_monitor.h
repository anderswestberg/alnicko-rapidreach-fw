/*
 * Copyright (c) 2025 RapidReach
 * 
 * Shell backend MQTT connection monitor
 */

#ifndef SHELL_BACKEND_MONITOR_H_
#define SHELL_BACKEND_MONITOR_H_

/**
 * @brief Initialize the shell backend monitor
 */
void shell_backend_monitor_init(void);

/**
 * @brief Start monitoring for shell MQTT connection
 */
void shell_backend_monitor_start(void);

/**
 * @brief Stop monitoring
 */
void shell_backend_monitor_stop(void);

/**
 * @brief Reset monitor state on network disconnect
 */
void shell_backend_monitor_reset(void);

/**
 * @brief Check if shell MQTT message indicates connection
 * 
 * This should be called from the logging backend to check messages
 * 
 * @param msg Log message to check
 */
void shell_backend_monitor_check_message(const char *msg);

#endif /* SHELL_BACKEND_MONITOR_H_ */
