/**
 * @file mqtt_module.c
 * @brief MQTT module implementation with heartbeat functionality
 */

#include "mqtt_module.h"

#ifdef CONFIG_RPR_MODULE_MQTT

#include <zephyr/kernel.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/net/socket.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

LOG_MODULE_REGISTER(mqtt_module, CONFIG_RPR_MODULE_MQTT_LOG_LEVEL);

/* MQTT client instance */
static struct mqtt_client client;
static struct sockaddr_storage broker;
static uint8_t rx_buffer[128];
static uint8_t tx_buffer[128];

/* Heartbeat task control */
static struct k_work_delayable heartbeat_work;
static bool heartbeat_enabled = false;
static bool mqtt_connected = false;
static bool mqtt_initialized = false;

/* MQTT maintenance thread */
static struct k_thread mqtt_thread;
static k_tid_t mqtt_thread_id;
static K_THREAD_STACK_DEFINE(mqtt_thread_stack, 2048);
static bool mqtt_thread_running = false;

/* Auto-reconnection state */
static bool auto_reconnect_enabled = true;
static int64_t last_reconnect_attempt = 0;
static int reconnect_interval_ms = 10000; /* 10 seconds between reconnection attempts */

/* Forward declarations */
static void mqtt_evt_handler(struct mqtt_client *const client,
                             const struct mqtt_evt *evt);
static void heartbeat_work_handler(struct k_work *work);
static int prepare_mqtt_client(void);
static void mqtt_thread_func(void *arg1, void *arg2, void *arg3);
static int mqtt_internal_connect(void);

/**
 * @brief MQTT event handler
 */
static void mqtt_evt_handler(struct mqtt_client *const client,
                             const struct mqtt_evt *evt)
{
    switch (evt->type) {
    case MQTT_EVT_CONNACK:
        if (evt->result != 0) {
            LOG_ERR("MQTT connect failed: %d", evt->result);
            mqtt_connected = false;
        } else {
            LOG_INF("MQTT client connected!");
            mqtt_connected = true;
        }
        break;

    case MQTT_EVT_DISCONNECT:
        LOG_WRN("MQTT disconnected: %d", evt->result);
        mqtt_connected = false;
        /* Note: Automatic reconnection could be implemented here if needed */
        break;

    case MQTT_EVT_PUBACK:
        if (evt->result != 0) {
            LOG_ERR("MQTT PUBACK error: %d", evt->result);
        } else {
            LOG_DBG("PUBACK packet id: %u", evt->param.puback.message_id);
        }
        break;

    case MQTT_EVT_PUBREC:
        if (evt->result != 0) {
            LOG_ERR("MQTT PUBREC error: %d", evt->result);
        } else {
            LOG_DBG("PUBREC packet id: %u", evt->param.pubrec.message_id);
        }
        break;

    case MQTT_EVT_PUBREL:
        if (evt->result != 0) {
            LOG_ERR("MQTT PUBREL error: %d", evt->result);
        } else {
            LOG_DBG("PUBREL packet id: %u", evt->param.pubrel.message_id);
        }
        break;

    case MQTT_EVT_PUBCOMP:
        if (evt->result != 0) {
            LOG_ERR("MQTT PUBCOMP error: %d", evt->result);
        } else {
            LOG_DBG("PUBCOMP packet id: %u", evt->param.pubcomp.message_id);
        }
        break;

    default:
        LOG_DBG("Unhandled MQTT event: %d", evt->type);
        break;
    }
}

/**
 * @brief Internal MQTT connection function (without timeout/waiting)
 */
static int mqtt_internal_connect(void)
{
    int ret;

    if (mqtt_connected) {
        return 0; /* Already connected */
    }

    LOG_INF("Attempting MQTT connection to %s:%d...", 
            CONFIG_RPR_MQTT_BROKER_HOST, CONFIG_RPR_MQTT_BROKER_PORT);

    ret = mqtt_connect(&client);
    if (ret != 0) {
        LOG_ERR("MQTT connect call failed: %d", ret);
        return ret;
    }

    return 0; /* Connection initiated, wait for CONNACK in event handler */
}

/**
 * @brief MQTT maintenance thread - continuously processes packets
 */
static void mqtt_thread_func(void *arg1, void *arg2, void *arg3)
{
    int ret;
    int64_t current_time;
    
    LOG_INF("MQTT maintenance thread started");
    
    while (mqtt_thread_running) {
        current_time = k_uptime_get();
        
        if (mqtt_connected) {
            /* Process incoming MQTT packets */
            ret = mqtt_input(&client);
            if (ret < 0) {
                if (ret == -ENOTCONN || ret == -ECONNRESET || ret == -EPIPE) {
                    LOG_ERR("MQTT connection lost: %d", ret);
                    mqtt_connected = false;
                    last_reconnect_attempt = current_time;
                } else if (ret == -EBUSY) {
                    LOG_DBG("MQTT socket busy (disconnected): %d", ret);
                    mqtt_connected = false;
                    last_reconnect_attempt = current_time;
                } else if (ret != -EAGAIN && ret != -EWOULDBLOCK) {
                    LOG_DBG("MQTT input error (non-fatal): %d", ret);
                    /* Don't immediately disconnect on other errors */
                }
            }
            
            /* Keep connection alive only if still connected */
            if (mqtt_connected) {
                ret = mqtt_live(&client);
                if (ret < 0 && ret != -EAGAIN) {
                    if (ret == -ENOTCONN || ret == -ECONNRESET || ret == -EPIPE || ret == -EBUSY) {
                        LOG_ERR("MQTT connection lost during live: %d", ret);
                        mqtt_connected = false;
                        last_reconnect_attempt = current_time;
                    } else {
                        LOG_DBG("MQTT live error (non-fatal): %d", ret);
                    }
                }
            }
        } else if (auto_reconnect_enabled) {
            /* Attempt auto-reconnection if enough time has passed */
            if (current_time - last_reconnect_attempt >= reconnect_interval_ms) {
                LOG_INF("Auto-reconnecting to MQTT broker...");
                ret = mqtt_internal_connect();
                if (ret == 0) {
                    /* Wait a bit for the connection to establish */
                    k_sleep(K_MSEC(100));
                    /* Process any incoming CONNACK */
                    mqtt_input(&client);
                }
                last_reconnect_attempt = current_time;
            }
        }
        
        /* Sleep for a short time to avoid busy waiting */
        k_sleep(K_MSEC(100));
    }
    
    LOG_INF("MQTT maintenance thread stopped");
}

/**
 * @brief Heartbeat work handler - publishes periodic alive messages
 */
static void heartbeat_work_handler(struct k_work *work)
{
    static uint32_t sequence_number = 0;
    char payload[64];
    int ret;

    if (!mqtt_connected) {
        LOG_WRN("MQTT not connected, skipping heartbeat");
        goto reschedule;
    }

    /* Create heartbeat payload with timestamp and sequence */
    ret = snprintf(payload, sizeof(payload), 
                   "{\"alive\":true,\"seq\":%u,\"uptime\":%llu}",
                   sequence_number++, k_uptime_get());
    
    if (ret < 0 || ret >= sizeof(payload)) {
        LOG_ERR("Failed to create heartbeat payload");
        goto reschedule;
    }

    /* Publish heartbeat message */
    ret = mqtt_send_heartbeat();
    if (ret != MQTT_SUCCESS) {
        LOG_WRN("Failed to send heartbeat: %d", ret);
    } else {
        LOG_DBG("Heartbeat sent: %s", payload);
    }

reschedule:
    if (heartbeat_enabled) {
        k_work_schedule(&heartbeat_work, 
                       K_SECONDS(CONFIG_RPR_MQTT_HEARTBEAT_INTERVAL_SEC));
    }
}

/**
 * @brief Prepare MQTT client configuration
 */
static int prepare_mqtt_client(void)
{
    struct sockaddr_in *broker4 = (struct sockaddr_in *)&broker;
    int ret;

    /* Configure broker address */
    broker4->sin_family = AF_INET;
    broker4->sin_port = htons(CONFIG_RPR_MQTT_BROKER_PORT);
    
    ret = zsock_inet_pton(AF_INET, CONFIG_RPR_MQTT_BROKER_HOST, 
                         &broker4->sin_addr);
    if (ret != 1) {
        LOG_ERR("Invalid broker IP address: %s", CONFIG_RPR_MQTT_BROKER_HOST);
        return -EINVAL;
    }

    /* Initialize MQTT client */
    mqtt_client_init(&client);

    /* MQTT client configuration */
    client.broker = &broker;
    client.evt_cb = mqtt_evt_handler;
    client.client_id.utf8 = (uint8_t *)CONFIG_RPR_MQTT_CLIENT_ID;
    client.client_id.size = strlen(CONFIG_RPR_MQTT_CLIENT_ID);
    client.password = NULL;
    client.user_name = NULL;
    client.protocol_version = MQTT_VERSION_3_1_1;

    /* MQTT buffers */
    client.rx_buf = rx_buffer;
    client.rx_buf_size = sizeof(rx_buffer);
    client.tx_buf = tx_buffer;
    client.tx_buf_size = sizeof(tx_buffer);

    /* MQTT transport */
    client.transport.type = MQTT_TRANSPORT_NON_SECURE;
    client.keepalive = CONFIG_RPR_MQTT_KEEPALIVE_SEC;

    return 0;
}

int mqtt_init(void)
{
    int ret;

    if (mqtt_initialized) {
        LOG_WRN("MQTT module already initialized");
        return 0;
    }

    LOG_INF("Initializing MQTT module...");

    /* Prepare MQTT client */
    ret = prepare_mqtt_client();
    if (ret != 0) {
        LOG_ERR("Failed to prepare MQTT client: %d", ret);
        return -1;
    }

    /* Initialize heartbeat work */
    k_work_init_delayable(&heartbeat_work, heartbeat_work_handler);

    /* Start MQTT maintenance thread */
    mqtt_thread_running = true;
    mqtt_thread_id = k_thread_create(&mqtt_thread, mqtt_thread_stack,
                                     K_THREAD_STACK_SIZEOF(mqtt_thread_stack),
                                     mqtt_thread_func, NULL, NULL, NULL,
                                     K_PRIO_COOP(7), 0, K_NO_WAIT);
    k_thread_name_set(mqtt_thread_id, "mqtt_maintenance");

    mqtt_initialized = true;
    LOG_INF("MQTT module initialized successfully");

    return 0;
}

mqtt_status_t mqtt_module_connect(void)
{
    int ret;

    if (!mqtt_initialized) {
        LOG_ERR("MQTT module not initialized");
        return MQTT_ERR_NOT_INITIALIZED;
    }

    if (mqtt_connected) {
        LOG_INF("MQTT already connected");
        return MQTT_SUCCESS;
    }

    /* Enable auto-reconnection */
    auto_reconnect_enabled = true;

    ret = mqtt_internal_connect();
    if (ret != 0) {
        LOG_ERR("Failed to initiate MQTT connection: %d", ret);
        return MQTT_ERR_CONNECTION_FAILED;
    }

    /* Wait for connection to establish - check for CONNACK */
    int timeout_ms = 5000; /* 5 second timeout */
    int check_interval_ms = 50;
    int elapsed_ms = 0;
    
    while (elapsed_ms < timeout_ms) {
        /* Process incoming MQTT packets */
        ret = mqtt_input(&client);
        if (ret < 0) {
            LOG_ERR("MQTT input error: %d", ret);
            break;
        }
        
        if (mqtt_connected) {
            LOG_INF("MQTT connection established successfully");
            return MQTT_SUCCESS;
        }
        
        k_sleep(K_MSEC(check_interval_ms));
        elapsed_ms += check_interval_ms;
    }
    
    LOG_ERR("MQTT connection timeout after %d ms", timeout_ms);
    return MQTT_ERR_CONNECTION_FAILED;
}

mqtt_status_t mqtt_module_disconnect(void)
{
    int ret;

    if (!mqtt_initialized) {
        return MQTT_ERR_NOT_INITIALIZED;
    }

    if (!mqtt_connected) {
        return MQTT_SUCCESS;
    }

    /* Disable auto-reconnection when explicitly disconnecting */
    auto_reconnect_enabled = false;

    /* Stop heartbeat first */
    mqtt_stop_heartbeat();

    LOG_INF("Disconnecting from MQTT broker...");
    ret = mqtt_disconnect(&client);
    if (ret != 0) {
        LOG_ERR("Failed to disconnect from MQTT broker: %d", ret);
    }

    mqtt_connected = false;
    
    /* Stop MQTT maintenance thread */
    if (mqtt_thread_running) {
        mqtt_thread_running = false;
        k_thread_join(mqtt_thread_id, K_FOREVER);
    }
    
    return MQTT_SUCCESS;
}

mqtt_status_t mqtt_module_publish(const char *topic, const char *payload, size_t payload_len)
{
    struct mqtt_publish_param param;
    int ret;

    if (!mqtt_initialized) {
        return MQTT_ERR_NOT_INITIALIZED;
    }

    if (!mqtt_connected) {
        LOG_ERR("MQTT not connected");
        return MQTT_ERR_CONNECTION_FAILED;
    }

    if (!topic || !payload) {
        return MQTT_ERR_INVALID_PARAM;
    }

    /* Configure publish parameters */
    param.message.topic.qos = MQTT_QOS_1_AT_LEAST_ONCE;
    param.message.topic.topic.utf8 = (uint8_t *)topic;
    param.message.topic.topic.size = strlen(topic);
    param.message.payload.data = (uint8_t *)payload;
    param.message.payload.len = payload_len;
    param.message_id = 1; /* Static for simplicity */
    param.dup_flag = 0;
    param.retain_flag = 0;

    ret = mqtt_publish(&client, &param);
    if (ret != 0) {
        LOG_ERR("Failed to publish to topic '%s': %d", topic, ret);
        return MQTT_ERR_PUBLISH_FAILED;
    }

    /* Give the maintenance thread a chance to process the publish response */
    k_sleep(K_MSEC(10));

    LOG_DBG("Published to topic '%s': %.*s", topic, (int)payload_len, payload);
    return MQTT_SUCCESS;
}

mqtt_status_t mqtt_send_heartbeat(void)
{
    static uint32_t sequence_number = 0;
    char payload[64];
    int ret;

    if (!mqtt_connected) {
        return MQTT_ERR_CONNECTION_FAILED;
    }

    /* Create heartbeat payload */
    ret = snprintf(payload, sizeof(payload), 
                   "{\"alive\":true,\"seq\":%u,\"uptime\":%llu}",
                   sequence_number++, k_uptime_get());
    
    if (ret < 0 || ret >= sizeof(payload)) {
        LOG_ERR("Failed to create heartbeat payload");
        return MQTT_ERR_PUBLISH_FAILED;
    }

    return mqtt_module_publish(CONFIG_RPR_MQTT_HEARTBEAT_TOPIC, payload, strlen(payload));
}

bool mqtt_is_connected(void)
{
    return mqtt_connected;
}

mqtt_status_t mqtt_start_heartbeat(void)
{
    if (!mqtt_initialized) {
        return MQTT_ERR_NOT_INITIALIZED;
    }

    if (heartbeat_enabled) {
        LOG_WRN("Heartbeat already enabled");
        return MQTT_SUCCESS;
    }

    LOG_INF("Starting MQTT heartbeat (interval: %d seconds)", 
            CONFIG_RPR_MQTT_HEARTBEAT_INTERVAL_SEC);

    heartbeat_enabled = true;
    k_work_schedule(&heartbeat_work, 
                   K_SECONDS(CONFIG_RPR_MQTT_HEARTBEAT_INTERVAL_SEC));

    return MQTT_SUCCESS;
}

mqtt_status_t mqtt_stop_heartbeat(void)
{
    if (!heartbeat_enabled) {
        return MQTT_SUCCESS;
    }

    LOG_INF("Stopping MQTT heartbeat");
    heartbeat_enabled = false;
    k_work_cancel_delayable(&heartbeat_work);

    return MQTT_SUCCESS;
}

mqtt_status_t mqtt_enable_auto_reconnect(void)
{
    auto_reconnect_enabled = true;
    LOG_INF("MQTT auto-reconnection enabled");
    return MQTT_SUCCESS;
}

mqtt_status_t mqtt_disable_auto_reconnect(void)
{
    auto_reconnect_enabled = false;
    LOG_INF("MQTT auto-reconnection disabled");
    return MQTT_SUCCESS;
}

bool mqtt_is_auto_reconnect_enabled(void)
{
    return auto_reconnect_enabled;
}

/* Auto-initialize MQTT module during system startup */
#ifdef CONFIG_RPR_MODULE_MQTT
SYS_INIT(mqtt_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
#endif

#endif /* CONFIG_RPR_MODULE_MQTT */
