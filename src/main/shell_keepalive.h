/*
 * Copyright (c) 2024 RapidReach
 * SPDX-License-Identifier: Apache-2.0
 * 
 * MQTT Shell Backend Keepalive
 */

#ifndef __SHELL_KEEPALIVE_H__
#define __SHELL_KEEPALIVE_H__

/**
 * @brief Start the MQTT shell keepalive
 * 
 * Sends periodic messages to prevent the MQTT shell backend from
 * timing out after 90 seconds of inactivity.
 */
void mqtt_shell_keepalive_start(void);

/**
 * @brief Stop the MQTT shell keepalive
 */
void mqtt_shell_keepalive_stop(void);

#endif /* __SHELL_KEEPALIVE_H__ */
