/*
 * Copyright (c) 2024 RapidReach
 * SPDX-License-Identifier: Apache-2.0
 * 
 * MQTT Shell Backend Keepalive
 * 
 * This module sends periodic keepalive messages to prevent the MQTT shell
 * backend from timing out after 90 seconds of inactivity.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <stdio.h>

#ifdef CONFIG_RPR_MODULE_MQTT
#include "../mqtt_module/mqtt_module.h"
#endif

LOG_MODULE_REGISTER(shell_keepalive, LOG_LEVEL_INF);

#ifdef CONFIG_SHELL_BACKEND_MQTT

/* Keepalive work item */
static struct k_work_delayable shell_keepalive_work;
static bool keepalive_enabled = false;

/* Send keepalive every 60 seconds (well before 90s timeout) */
#define SHELL_KEEPALIVE_INTERVAL_SEC 60

static void shell_keepalive_handler(struct k_work *work)
{
    ARG_UNUSED(work);
    
    if (!keepalive_enabled) {
        LOG_DBG("Shell keepalive disabled, stopping");
        return;
    }
    
    /* Send a keepalive via MQTT to our own shell input topic
     * This simulates user input to prevent the 90-second timeout */
    #ifdef CONFIG_RPR_MODULE_MQTT
    char topic[64];
    snprintf(topic, sizeof(topic), "rapidreach/%s-shell/shell/in", CONFIG_RPR_MQTT_CLIENT_ID);
    
    /* Send newline character as keepalive, QoS 1 */
    mqtt_status_t ret = mqtt_publish(topic, "\n", 1, 1);
    if (ret == MQTT_SUCCESS) {
        LOG_DBG("Sent MQTT shell keepalive to %s", topic);
    } else {
        LOG_WRN("Failed to send MQTT shell keepalive: %d", ret);
    }
    #else
    LOG_WRN("MQTT module not enabled, cannot send shell keepalive");
    #endif
    
    /* Reschedule for next keepalive */
    k_work_schedule(&shell_keepalive_work, K_SECONDS(SHELL_KEEPALIVE_INTERVAL_SEC));
}

void mqtt_shell_keepalive_start(void)
{
    if (keepalive_enabled) {
        LOG_WRN("MQTT shell keepalive already running");
        return;
    }
    
    LOG_INF("Starting MQTT shell keepalive (interval: %d seconds)", 
            SHELL_KEEPALIVE_INTERVAL_SEC);
    
    keepalive_enabled = true;
    
    /* Initialize work item if not already done */
    static bool work_initialized = false;
    if (!work_initialized) {
        k_work_init_delayable(&shell_keepalive_work, shell_keepalive_handler);
        work_initialized = true;
    }
    
    /* Schedule first keepalive */
    k_work_schedule(&shell_keepalive_work, K_SECONDS(SHELL_KEEPALIVE_INTERVAL_SEC));
}

void mqtt_shell_keepalive_stop(void)
{
    if (!keepalive_enabled) {
        return;
    }
    
    LOG_INF("Stopping MQTT shell keepalive");
    keepalive_enabled = false;
    k_work_cancel_delayable(&shell_keepalive_work);
}

#else /* !CONFIG_SHELL_BACKEND_MQTT */

void mqtt_shell_keepalive_start(void) {}
void mqtt_shell_keepalive_stop(void) {}

#endif /* CONFIG_SHELL_BACKEND_MQTT */
