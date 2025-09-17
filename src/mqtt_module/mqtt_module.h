/**
 * @file mqtt_module.h
 * @brief MQTT module with heartbeat functionality for RapidReach firmware
 */

#ifndef MQTT_MODULE_H
#define MQTT_MODULE_H
#ifdef __cplusplus
extern "C" {
#endif

/* Expose latest temp file for handlers that need it */
extern char g_mqtt_last_temp_file[];

#ifdef __cplusplus
}
#endif

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
 * @return int 0 on success, negative error code otherwise
 */
int mqtt_init(void);

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

/**
 * @brief Enable automatic reconnection when connection is lost
 * 
 * @return mqtt_status_t MQTT_SUCCESS on success, error code otherwise
 */
mqtt_status_t mqtt_enable_auto_reconnect(void);

/**
 * @brief Disable automatic reconnection
 * 
 * @return mqtt_status_t MQTT_SUCCESS on success, error code otherwise
 */
mqtt_status_t mqtt_disable_auto_reconnect(void);

/**
 * @brief Check if auto-reconnection is enabled
 * 
 * @return true if auto-reconnection is enabled, false otherwise
 */
bool mqtt_is_auto_reconnect_enabled(void);

/**
 * @brief MQTT message handler callback type
 * 
 * @param topic The topic the message was received on
 * @param payload The message payload
 * @param payload_len Length of the payload
 */
typedef void (*mqtt_message_handler_t)(const char *topic, const uint8_t *payload, size_t payload_len);

/**
 * @brief Subscribe to an MQTT topic
 * 
 * @param topic The topic to subscribe to
 * @param qos Quality of Service level (0, 1, or 2)
 * @param handler Callback function to handle received messages
 * @return mqtt_status_t MQTT_SUCCESS on success, error code otherwise
 */
mqtt_status_t mqtt_module_subscribe(const char *topic, uint8_t qos, mqtt_message_handler_t handler);

/**
 * @brief Unsubscribe from an MQTT topic
 * 
 * @param topic The topic to unsubscribe from
 * @return mqtt_status_t MQTT_SUCCESS on success, error code otherwise
 */
mqtt_status_t mqtt_module_unsubscribe(const char *topic);

/**
 * @brief MQTT connection event types
 */
typedef enum {
    MQTT_EVENT_CONNECTED,      /**< Connected to broker */
    MQTT_EVENT_DISCONNECTED,   /**< Disconnected from broker */
    MQTT_EVENT_CONNECT_FAILED  /**< Connection attempt failed */
} mqtt_event_type_t;

/**
 * @brief MQTT event handler callback type
 * 
 * @param event_type Type of event that occurred
 */
typedef void (*mqtt_event_handler_t)(mqtt_event_type_t event_type);

/**
 * @brief Set the MQTT event handler callback
 * 
 * @param handler The event handler callback function (NULL to disable)
 */
void mqtt_set_event_handler(mqtt_event_handler_t handler);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_MODULE_H */
