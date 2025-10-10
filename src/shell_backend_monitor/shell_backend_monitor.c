/*
 * Copyright (c) 2025 RapidReach
 * 
 * Shell backend MQTT connection monitor
 * 
 * This module monitors the shell backend MQTT status and sends events
 * to the init state machine when the shell MQTT connects.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include "../init_state_machine/init_state_machine.h"

LOG_MODULE_REGISTER(shell_backend_monitor, LOG_LEVEL_INF);

/* Work item for periodic checking */
static struct k_work_delayable monitor_work;
static bool monitoring_active = false;
static bool shell_mqtt_connected = false;

/**
 * @brief Check if shell MQTT message indicates connection
 * 
 * We look for the "MQTT client connected!" message from shell backend
 */
void shell_backend_monitor_check_message(const char *msg)
{
    if (!msg || !monitoring_active) {
        return;
    }
    
    /* Check for shell MQTT connection message */
    if (strstr(msg, "MQTT client connected!") != NULL) {
        if (!shell_mqtt_connected) {
            shell_mqtt_connected = true;
            LOG_INF("Detected shell MQTT connection");
            
            /* Send event to state machine */
            init_sm_send_event(EVENT_SHELL_MQTT_CONNECTED);
            
            /* Stop monitoring once connected */
            monitoring_active = false;
            k_work_cancel_delayable(&monitor_work);
        }
    }
}

/**
 * @brief Periodic work to check shell backend status
 */
static void monitor_work_handler(struct k_work *work)
{
    /* In a real implementation, we might check shell backend status here */
    /* For now, we rely on log message detection */
    
    if (monitoring_active) {
        /* Continue monitoring */
        k_work_reschedule(&monitor_work, K_SECONDS(1));
    }
}

/**
 * @brief Start monitoring for shell MQTT connection
 */
void shell_backend_monitor_start(void)
{
    LOG_INF("Starting shell backend monitor");
    monitoring_active = true;
    shell_mqtt_connected = false;
    
    /* Start periodic check */
    k_work_reschedule(&monitor_work, K_SECONDS(1));
}

/**
 * @brief Stop monitoring
 */
void shell_backend_monitor_stop(void)
{
    LOG_INF("Stopping shell backend monitor");
    monitoring_active = false;
    k_work_cancel_delayable(&monitor_work);
}

/**
 * @brief Reset monitor state on network disconnect
 */
void shell_backend_monitor_reset(void)
{
    shell_mqtt_connected = false;
}

/**
 * @brief Initialize the shell backend monitor
 */
void shell_backend_monitor_init(void)
{
    k_work_init_delayable(&monitor_work, monitor_work_handler);
    LOG_INF("Shell backend monitor initialized");
}
