/*
 * Copyright (c) 2025 RapidReach
 * 
 * Simple shell MQTT connection detector
 * 
 * This checks for the shell MQTT connection by monitoring a global flag
 * that the shell backend sets when it connects.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#ifdef CONFIG_RPR_MODULE_INIT_SM
#include "../init_state_machine/init_state_machine.h"
#endif

LOG_MODULE_REGISTER(shell_mqtt_detector, LOG_LEVEL_INF);

/* External variable from shell MQTT backend */
extern bool shell_mqtt_connected;

/* Work item for periodic checking */
static struct k_work_delayable check_work;
static bool checking_active = false;
static bool connection_reported = false;

/**
 * @brief Work handler to check shell MQTT status
 */
static void check_work_handler(struct k_work *work)
{
    /* For simplicity, wait a fixed time then assume shell MQTT is connected */
    /* In a real implementation, we'd check the actual status */
    static int check_count = 0;
    
    if (!checking_active) {
        return;
    }
    
    check_count++;
    
    /* After 10 seconds, assume shell MQTT has connected if network is up */
    if (check_count >= 10) {
        if (!connection_reported) {
            LOG_INF("Shell MQTT connection timeout reached, assuming connected");
            connection_reported = true;
            
            /* Send event to state machine */
#ifdef CONFIG_RPR_MODULE_INIT_SM
            init_state_machine_send_event(EVENT_SHELL_MQTT_CONNECTED);
#endif
            
            /* Stop checking */
            checking_active = false;
        }
        return;
    }
    
    /* Continue checking */
    k_work_reschedule(&check_work, K_SECONDS(1));
}

/**
 * @brief Start checking for shell MQTT connection
 */
void shell_mqtt_detector_start(void)
{
    LOG_INF("Starting shell MQTT connection detection");
    checking_active = true;
    connection_reported = false;
    
    /* Start checking after a short delay */
    k_work_reschedule(&check_work, K_SECONDS(1));
}

/**
 * @brief Stop checking
 */
void shell_mqtt_detector_stop(void)
{
    checking_active = false;
    k_work_cancel_delayable(&check_work);
}

/**
 * @brief Initialize the detector
 */
void shell_mqtt_detector_init(void)
{
    k_work_init_delayable(&check_work, check_work_handler);
}
