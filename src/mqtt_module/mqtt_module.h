/**
 * @file mqtt_module.h
 * @brief MQTT module with heartbeat functionality for RapidReach firmware
 */

#ifndef MQTT_MODULE_H
#define MQTT_MODULE_H

#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief MQTT module status codes
 */
typedef enum {
    MQTT_SUCCESS = 0,
    MQTT_ERR_NOT_INITIALIZED,
    MQTT_ERR_CONNECTION_FAILED,
    MQTT_ERR_PUBLISH_FAILED,
    MQTT_ERR_SUBSCRIBE_FAILED,
    MQTT_ERR_INVALID_PARAM,
} mqtt_status_t;

/**
 * @brief Initialize the MQTT module
 * 
 * Sets up the MQTT client and starts the heartbeat task.
 * 
 * @return mqtt_status_t MQTT_SUCCESS on success, error code otherwise
 */
mqtt_status_t mqtt_init(void);

/**
 * @brief Connect to the MQTT broker
 * 
 * @return mqtt_status_t MQTT_SUCCESS on success, error code otherwise
 */
mqtt_status_t mqtt_module_connect(void);

/**
 * @brief Disconnect from the MQTT broker
 * 
 * @return mqtt_status_t MQTT_SUCCESS on success, error code otherwise
 */
mqtt_status_t mqtt_module_disconnect(void);

/**
 * @brief Publish a message to a topic
 * 
 * @param topic The topic to publish to
 * @param payload The message payload
 * @param payload_len Length of the payload
 * @return mqtt_status_t MQTT_SUCCESS on success, error code otherwise
 */
mqtt_status_t mqtt_module_publish(const char *topic, const char *payload, size_t payload_len);

/**
 * @brief Manually trigger a heartbeat message
 * 
 * @return mqtt_status_t MQTT_SUCCESS on success, error code otherwise
 */
mqtt_status_t mqtt_send_heartbeat(void);

/**
 * @brief Check if MQTT is connected
 * 
 * @return true if connected, false otherwise
 */
bool mqtt_is_connected(void);

/**
 * @brief Start the periodic heartbeat task
 * 
 * @return mqtt_status_t MQTT_SUCCESS on success, error code otherwise
 */
mqtt_status_t mqtt_start_heartbeat(void);

/**
 * @brief Stop the periodic heartbeat task
 * 
 * @return mqtt_status_t MQTT_SUCCESS on success, error code otherwise
 */
mqtt_status_t mqtt_stop_heartbeat(void);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_MODULE_H */
