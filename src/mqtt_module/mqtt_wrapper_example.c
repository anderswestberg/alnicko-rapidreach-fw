/**
 * @file mqtt_wrapper_example.c
 * @brief Example of how to use the new MQTT client wrapper
 * 
 * This example shows how to migrate from the old mqtt_module
 * to the new thread-safe wrapper.
 */

#include "mqtt_client_wrapper.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(mqtt_example, LOG_LEVEL_INF);

/* Handle for MQTT client */
static mqtt_handle_t mqtt_client;

/**
 * @brief Callback for connection state changes
 */
static void mqtt_connection_cb(bool connected, void *user_data)
{
    if (connected) {
        LOG_INF("MQTT connected - device is online");
        
        /* Example: Publish a status message */
        const char *status = "{\"status\":\"online\",\"version\":\"1.0.0\"}";
        mqtt_client_publish(mqtt_client, "device/status", 
                           (uint8_t *)status, strlen(status),
                           MQTT_QOS_1_AT_LEAST_ONCE, false);
    } else {
        LOG_WRN("MQTT disconnected - will auto-reconnect");
    }
}

/**
 * @brief Callback for received messages
 * 
 * This runs in a worker thread, so it's safe to do blocking operations
 */
static void mqtt_message_received(const char *topic,
                                 const uint8_t *payload,
                                 size_t payload_len,
                                 void *user_data)
{
    LOG_INF("Received message on topic: %s", topic);
    
    /* Safe to do file I/O, database operations, etc. here */
    /* The MQTT protocol thread is not blocked */
    
    /* Example: Process command */
    if (strcmp(topic, "device/command") == 0) {
        /* Parse and execute command */
        LOG_INF("Processing command: %.*s", (int)payload_len, payload);
        
        /* Can safely do time-consuming operations */
        k_sleep(K_SECONDS(1));
        
        /* Send response */
        const char *response = "{\"result\":\"success\"}";
        mqtt_client_publish(mqtt_client, "device/response",
                           (uint8_t *)response, strlen(response),
                           MQTT_QOS_1_AT_LEAST_ONCE, false);
    }
}

/**
 * @brief Initialize and connect MQTT
 */
int mqtt_example_init(void)
{
    /* Configure MQTT client */
    struct mqtt_client_config config = {
        .broker_hostname = "mqtt.example.com",
        .broker_port = 1883,
        .client_id = "device-001",
        .keepalive_interval = 60,
        .clean_session = true
    };
    
    /* Create MQTT client */
    mqtt_client = mqtt_wrapper_create(&config);
    if (!mqtt_client) {
        LOG_ERR("Failed to create MQTT client");
        return -ENOMEM;
    }
    
    /* Connect (non-blocking) */
    int ret = mqtt_client_connect(mqtt_client, mqtt_connection_cb, NULL);
    if (ret < 0) {
        LOG_ERR("Failed to initiate connection: %d", ret);
        return ret;
    }
    
    /* Subscribe to topics */
    struct mqtt_subscription_config sub = {
        .topic = "device/command",
        .qos = MQTT_QOS_1_AT_LEAST_ONCE,
        .callback = mqtt_message_received,
        .user_data = NULL
    };
    
    ret = mqtt_client_subscribe(mqtt_client, &sub);
    if (ret < 0) {
        LOG_ERR("Failed to subscribe: %d", ret);
        /* Note: Will auto-subscribe when connected */
    }
    
    return 0;
}

/**
 * @brief Example of publishing sensor data
 */
void mqtt_example_publish_sensor_data(float temperature, float humidity)
{
    char payload[128];
    
    /* Check if connected */
    if (!mqtt_client_is_connected(mqtt_client)) {
        LOG_WRN("Not connected, skipping publish");
        return;
    }
    
    /* Format JSON payload */
    snprintf(payload, sizeof(payload),
             "{\"temperature\":%.2f,\"humidity\":%.2f,\"timestamp\":%lld}",
             temperature, humidity, k_uptime_get());
    
    /* Publish (non-blocking) */
    int ret = mqtt_client_publish(mqtt_client,
                                 "device/sensors",
                                 (uint8_t *)payload, strlen(payload),
                                 MQTT_QOS_0_AT_MOST_ONCE,
                                 false);
    if (ret < 0) {
        LOG_ERR("Failed to publish: %d", ret);
    }
}

/**
 * @brief Migration guide from old mqtt_module
 * 
 * Old way:
 *   mqtt_module_init();
 *   mqtt_module_connect();
 *   mqtt_module_subscribe(topic, qos, handler);
 *   mqtt_module_publish(topic, data, len);
 * 
 * New way:
 *   mqtt_client = mqtt_wrapper_create(&config);
 *   mqtt_client_connect(mqtt_client, conn_cb, user_data);
 *   mqtt_client_subscribe(mqtt_client, &sub_config);
 *   mqtt_client_publish(mqtt_client, topic, data, len, qos, retain);
 * 
 * Key improvements:
 * - Non-blocking operations
 * - Callbacks run in worker thread (safe for blocking ops)
 * - Automatic reconnection with exponential backoff
 * - Thread-safe API
 * - No global state
 */
