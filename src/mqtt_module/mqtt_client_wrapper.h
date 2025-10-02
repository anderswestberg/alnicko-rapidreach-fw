/**
 * @file mqtt_client_wrapper.h
 * @brief Thread-safe MQTT client wrapper with improved reliability
 * 
 * This wrapper provides:
 * - Thread-safe access to MQTT client
 * - Separation of protocol handling from application logic
 * - Non-blocking message processing
 * - Automatic reconnection with backoff
 */

#ifndef MQTT_CLIENT_WRAPPER_H
#define MQTT_CLIENT_WRAPPER_H

#include <stdint.h>
#include <stdbool.h>
#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
struct mqtt_client;

/**
 * @brief MQTT message received callback
 * 
 * Called from a work queue, not from MQTT thread
 * Safe to perform blocking operations
 */
typedef void (*mqtt_msg_received_cb_t)(const char *topic, 
                                       const uint8_t *payload, 
                                       size_t payload_len,
                                       void *user_data);

/**
 * @brief MQTT connection state callback
 */
typedef void (*mqtt_conn_state_cb_t)(bool connected, void *user_data);

/**
 * @brief MQTT client configuration
 */
struct mqtt_client_config {
    const char *broker_hostname;
    uint16_t broker_port;
    const char *client_id;
    uint16_t keepalive_interval;
    bool clean_session;
    /* TLS config can be added here */
};

/**
 * @brief MQTT subscription configuration
 */
struct mqtt_subscription_config {
    const char *topic;
    uint8_t qos;
    mqtt_msg_received_cb_t callback;
    void *user_data;
};

/**
 * @brief Opaque MQTT client handle
 */
typedef struct mqtt_client_wrapper *mqtt_handle_t;

/**
 * @brief Create and initialize MQTT client wrapper
 * 
 * @param config Client configuration
 * @return Handle to MQTT client or NULL on error
 */
mqtt_handle_t mqtt_wrapper_create(const struct mqtt_client_config *config);

/**
 * @brief Connect to MQTT broker
 * 
 * Non-blocking - connection happens in background
 * 
 * @param handle MQTT client handle
 * @param conn_cb Connection state callback (optional)
 * @param user_data User data for callback
 * @return 0 on success, negative error code on failure
 */
int mqtt_client_connect(mqtt_handle_t handle, 
                       mqtt_conn_state_cb_t conn_cb,
                       void *user_data);

/**
 * @brief Disconnect from MQTT broker
 * 
 * @param handle MQTT client handle
 * @return 0 on success, negative error code on failure
 */
int mqtt_client_disconnect(mqtt_handle_t handle);

/**
 * @brief Subscribe to MQTT topic
 * 
 * @param handle MQTT client handle
 * @param sub Subscription configuration
 * @return 0 on success, negative error code on failure
 */
int mqtt_client_subscribe(mqtt_handle_t handle,
                         const struct mqtt_subscription_config *sub);

/**
 * @brief Unsubscribe from MQTT topic
 * 
 * @param handle MQTT client handle
 * @param topic Topic to unsubscribe from
 * @return 0 on success, negative error code on failure
 */
int mqtt_client_unsubscribe(mqtt_handle_t handle, const char *topic);

/**
 * @brief Publish MQTT message
 * 
 * Non-blocking - message is queued for transmission
 * 
 * @param handle MQTT client handle
 * @param topic Topic to publish to
 * @param payload Message payload
 * @param payload_len Payload length
 * @param qos Quality of Service level
 * @param retain Retain flag
 * @return 0 on success, negative error code on failure
 */
int mqtt_client_publish(mqtt_handle_t handle,
                       const char *topic,
                       const uint8_t *payload,
                       size_t payload_len,
                       uint8_t qos,
                       bool retain);

/**
 * @brief Check if client is connected
 * 
 * @param handle MQTT client handle
 * @return true if connected, false otherwise
 */
bool mqtt_client_is_connected(mqtt_handle_t handle);

/**
 * @brief Enable/disable automatic reconnection
 * 
 * @param handle MQTT client handle
 * @param enable true to enable, false to disable
 */
void mqtt_client_set_auto_reconnect(mqtt_handle_t handle, bool enable);

/**
 * @brief Deinitialize MQTT client
 * 
 * @param handle MQTT client handle
 */
void mqtt_client_deinit(mqtt_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_CLIENT_WRAPPER_H */
