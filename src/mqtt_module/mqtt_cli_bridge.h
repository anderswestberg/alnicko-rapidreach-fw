/**
 * @file mqtt_cli_bridge.h
 * @brief MQTT CLI Bridge - Remote command execution via MQTT
 * 
 * @author RapidReach Development Team
 * @copyright (C) 2025 RapidReach. All rights reserved.
 */

#ifndef MQTT_CLI_BRIDGE_H
#define MQTT_CLI_BRIDGE_H

#include <stdbool.h>
#include <zephyr/shell/shell.h>

/**
 * @brief Initialize MQTT CLI bridge
 * 
 * @param dev_id Device ID to use in topic names
 * @return 0 on success, negative error code on failure
 */
int mqtt_cli_bridge_init(const char *dev_id);

/**
 * @brief Enable/disable MQTT CLI bridge
 * 
 * @param enable true to enable, false to disable
 * @return 0 on success, negative error code on failure
 */
int mqtt_cli_bridge_enable(bool enable);

/**
 * @brief Get MQTT CLI bridge status
 * 
 * @return true if enabled, false if disabled
 */
bool mqtt_cli_bridge_is_enabled(void);

/**
 * @brief Get command topic for subscription
 * 
 * @return Command topic string
 */
const char *mqtt_cli_get_command_topic(void);

/**
 * @brief Process incoming MQTT command
 * 
 * This function should be called from mqtt_module.c when a message
 * is received on the command topic.
 * 
 * @param topic The topic the message was received on
 * @param payload The message payload
 * @param len Length of the payload
 */
void mqtt_cli_process_command(const char *topic, const uint8_t *payload, size_t len);

/**
 * @brief Shell command handler for MQTT CLI bridge
 * 
 * @param sh Shell instance
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, negative error code on failure
 */
int cmd_mqtt_cli_bridge(const struct shell *sh, size_t argc, char **argv);

#endif /* MQTT_CLI_BRIDGE_H */