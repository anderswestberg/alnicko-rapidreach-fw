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

/* Forward declarations */
static void mqtt_evt_handler(struct mqtt_client *const client,
                             const struct mqtt_evt *evt);
static void heartbeat_work_handler(struct k_work *work);
static int prepare_mqtt_client(void);

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
        LOG_INF("MQTT client disconnected: %d", evt->result);
        mqtt_connected = false;
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

mqtt_status_t mqtt_init(void)
{
    int ret;

    if (mqtt_initialized) {
        LOG_WRN("MQTT module already initialized");
        return MQTT_SUCCESS;
    }

    LOG_INF("Initializing MQTT module...");

    /* Prepare MQTT client */
    ret = prepare_mqtt_client();
    if (ret != 0) {
        LOG_ERR("Failed to prepare MQTT client: %d", ret);
        return MQTT_ERR_NOT_INITIALIZED;
    }

    /* Initialize heartbeat work */
    k_work_init_delayable(&heartbeat_work, heartbeat_work_handler);

    mqtt_initialized = true;
    LOG_INF("MQTT module initialized successfully");

    return MQTT_SUCCESS;
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

    LOG_INF("Connecting to MQTT broker %s:%d...", 
            CONFIG_RPR_MQTT_BROKER_HOST, CONFIG_RPR_MQTT_BROKER_PORT);

    ret = mqtt_connect(&client);
    if (ret != 0) {
        LOG_ERR("Failed to connect to MQTT broker: %d", ret);
        return MQTT_ERR_CONNECTION_FAILED;
    }

    /* Wait a bit for connection to establish */
    k_sleep(K_MSEC(100));

    return MQTT_SUCCESS;
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

    /* Stop heartbeat first */
    mqtt_stop_heartbeat();

    LOG_INF("Disconnecting from MQTT broker...");
    ret = mqtt_disconnect(&client);
    if (ret != 0) {
        LOG_ERR("Failed to disconnect from MQTT broker: %d", ret);
    }

    mqtt_connected = false;
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

/* Auto-initialize MQTT module during system startup */
#ifdef CONFIG_RPR_MODULE_MQTT
SYS_INIT(mqtt_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
#endif

#endif /* CONFIG_RPR_MODULE_MQTT */
