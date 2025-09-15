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
#include "../dev_info/dev_info.h"
#ifdef CONFIG_RPR_MODULE_DEVICE_REGISTRY
#include "../device_registry/device_registry.h"
#endif

#ifdef CONFIG_RPR_MODULE_INIT_SM
#include "../init_state_machine/init_state_machine.h"
#endif

#ifdef CONFIG_RPR_MODULE_FILE_MANAGER
#include "../file_manager/file_manager.h"
#include <zephyr/fs/fs.h>
#endif

LOG_MODULE_REGISTER(mqtt_module, CONFIG_RPR_MODULE_MQTT_LOG_LEVEL);

/* MQTT client instance */
static struct mqtt_client client;
static struct sockaddr_storage broker;
static uint8_t rx_buffer[4096];   /* 4KB for receiving - audio will be chunked */
static uint8_t tx_buffer[1024];   /* 1KB for sending (heartbeats, etc) */
static char client_id_buffer[32];  /* Buffer for "rr-speaker-XXXXX" format */

/* Heartbeat task control */
static struct k_work_delayable heartbeat_work;
static bool heartbeat_enabled = false;
static bool mqtt_connected = false;
static bool mqtt_initialized = false;

/* MQTT maintenance thread */
static struct k_thread mqtt_thread;
static k_tid_t mqtt_thread_id;
static K_THREAD_STACK_DEFINE(mqtt_thread_stack, 8192);
static bool mqtt_thread_running = false;

/* Auto-reconnection state */
static bool auto_reconnect_enabled = true;
static int64_t last_reconnect_attempt = 0;
static int reconnect_interval_ms = 5000; /* Start with 5 seconds */
static int reconnect_attempts = 0;
#define MIN_RECONNECT_INTERVAL_MS 5000    /* 5 seconds minimum */
#define MAX_RECONNECT_INTERVAL_MS 300000  /* 5 minutes maximum */
#define RECONNECT_BACKOFF_MULTIPLIER 2    /* Double the interval each failure */

/* Event handler callback */
static mqtt_event_handler_t event_handler = NULL;

/* Subscription management */
#define MAX_SUBSCRIPTIONS 8
struct mqtt_subscription {
    char topic[128];
    mqtt_message_handler_t handler;
    uint8_t qos;
    bool active;
};
static struct mqtt_subscription subscriptions[MAX_SUBSCRIPTIONS];
static struct k_mutex subscriptions_mutex;

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
            
            /* Call the event handler if registered */
            if (event_handler) {
                event_handler(MQTT_EVENT_CONNECT_FAILED);
            }
        } else {
            LOG_INF("MQTT client connected!");
            mqtt_connected = true;
            /* Reset reconnection backoff on successful connection */
            
#ifdef CONFIG_RPR_MODULE_INIT_SM
            /* Notify state machine of successful connection */
            init_state_machine_send_event(EVENT_MQTT_CONNECTED);
#endif
            reconnect_interval_ms = MIN_RECONNECT_INTERVAL_MS;
            reconnect_attempts = 0;
            
            /* Call the event handler if registered */
            if (event_handler) {
                event_handler(MQTT_EVENT_CONNECTED);
            }
        }
        break;

    case MQTT_EVT_DISCONNECT:
        LOG_WRN("MQTT disconnected: %d", evt->result);
        mqtt_connected = false;
        /* Note: Automatic reconnection could be implemented here if needed */
        
#ifdef CONFIG_RPR_MODULE_INIT_SM
        /* Notify state machine of disconnection */
        init_state_machine_send_event(EVENT_MQTT_DISCONNECTED);
#endif
        
        /* Call the event handler if registered */
        if (event_handler) {
            event_handler(MQTT_EVENT_DISCONNECTED);
        }
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

    case MQTT_EVT_PUBLISH: {
        const struct mqtt_publish_param *pub = &evt->param.publish;
        size_t total_len = pub->message.payload.len;
        
        /* Extract topic */
        char topic_buf[128];
        size_t topic_len = pub->message.topic.topic.size;
        if (topic_len > sizeof(topic_buf) - 1) {
            topic_len = sizeof(topic_buf) - 1;
        }
        memcpy(topic_buf, pub->message.topic.topic.utf8, topic_len);
        topic_buf[topic_len] = '\0';
        
        LOG_INF("Received PUBLISH on topic '%s' [%zu bytes]", topic_buf, total_len);
        
        /* Check if this is a potentially large audio message */
        if (strstr(topic_buf, "rapidreach/audio/") != NULL && total_len > 256) {
            /* Handle large audio message with chunked reading */
            LOG_INF("Processing large audio message (%zu bytes)", total_len);
            
            /* First, read up to 512 bytes to get the JSON header */
            uint8_t json_buf[512];
            size_t json_read = (total_len < sizeof(json_buf)) ? total_len : sizeof(json_buf);
            int ret = mqtt_read_publish_payload_blocking(client, json_buf, json_read);
            if (ret < 0) {
                LOG_ERR("Failed to read JSON header: %d", ret);
                break;
            }
            
            /* Find the end of JSON (look for the last '}') */
            int json_end = -1;
            for (int i = json_read - 1; i >= 0; i--) {
                if (json_buf[i] == '}') {
                    json_end = i + 1;
                    /* Make sure this is the end of the JSON by checking for audio data */
                    if (json_end < json_read && json_buf[json_end] != '{') {
                        break;
                    }
                }
            }
            
            if (json_end < 0) {
                LOG_ERR("Could not find JSON boundary in first %zu bytes", json_read);
                /* Consume the rest of the message */
                uint8_t discard[256];
                size_t remaining = total_len - json_read;
                while (remaining > 0) {
                    size_t chunk = (remaining < sizeof(discard)) ? remaining : sizeof(discard);
                    mqtt_read_publish_payload_blocking(client, discard, chunk);
                    remaining -= chunk;
                }
                break;
            }
            
            LOG_INF("JSON header is %d bytes, audio starts at byte %d", json_end, json_end);
            
#ifdef CONFIG_RPR_MODULE_FILE_MANAGER
            /* Save audio data to file */
            const char *audio_file = "/lfs/mqtt_audio_temp.opus";
            struct fs_file_t file;
            fs_file_t_init(&file);
            
            /* Ensure file system is mounted */
            ret = file_manager_init();
            if (ret < 0 && ret != -EALREADY) {
                LOG_ERR("Failed to initialize file manager: %d", ret);
                /* Consume the rest of the message */
                uint8_t discard[256];
                size_t remaining = total_len - json_read;
                while (remaining > 0) {
                    size_t chunk = (remaining < sizeof(discard)) ? remaining : sizeof(discard);
                    mqtt_read_publish_payload_blocking(client, discard, chunk);
                    remaining -= chunk;
                }
                break;
            }
            
            /* Calculate how much audio data we already have in json_buf */
            size_t audio_in_buf = json_read - json_end;
            size_t audio_remaining = total_len - json_end;
            
            /* Open file for writing */
            ret = fs_open(&file, audio_file, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
            if (ret < 0) {
                LOG_ERR("Failed to open audio file: %d", ret);
                /* Consume the rest */
                uint8_t discard[256];
                while (audio_remaining > 0) {
                    size_t chunk = (audio_remaining < sizeof(discard)) ? audio_remaining : sizeof(discard);
                    mqtt_read_publish_payload_blocking(client, discard, chunk);
                    audio_remaining -= chunk;
                }
                break;
            }
            
            /* Write initial audio data from json_buf if any */
            size_t written = 0;
            if (audio_in_buf > 0) {
                ret = fs_write(&file, json_buf + json_end, audio_in_buf);
                if (ret < 0) {
                    LOG_ERR("Failed to write initial audio chunk: %d", ret);
                    fs_close(&file);
                    /* Consume the rest */
                    uint8_t discard[256];
                    audio_remaining -= audio_in_buf;
                    while (audio_remaining > 0) {
                        size_t chunk = (audio_remaining < sizeof(discard)) ? audio_remaining : sizeof(discard);
                        mqtt_read_publish_payload_blocking(client, discard, chunk);
                        audio_remaining -= chunk;
                    }
                    break;
                }
                written = ret;
                audio_remaining -= audio_in_buf;
            }
            
            /* Read and write the rest of the audio data in chunks */
            uint8_t chunk_buf[512];
            
            while (audio_remaining > 0) {
                size_t chunk = (audio_remaining < sizeof(chunk_buf)) ? audio_remaining : sizeof(chunk_buf);
                ret = mqtt_read_publish_payload_blocking(client, chunk_buf, chunk);
                if (ret < 0) {
                    LOG_ERR("Failed to read audio chunk: %d", ret);
                    fs_close(&file);
                    break;
                }
                
                /* Write to file */
                ret = fs_write(&file, chunk_buf, chunk);
                if (ret < 0) {
                    LOG_ERR("Failed to write audio chunk: %d", ret);
                    fs_close(&file);
                    break;
                }
                
                written += ret;
                audio_remaining -= chunk;
                LOG_DBG("Wrote %d bytes, %zu remaining", ret, audio_remaining);
            }
            
            fs_close(&file);
            LOG_INF("Audio saved to %s (%zu bytes)", audio_file, written);
            
            /* Call handler with JSON metadata only */
            k_mutex_lock(&subscriptions_mutex, K_FOREVER);
            for (int i = 0; i < MAX_SUBSCRIPTIONS; i++) {
                if (subscriptions[i].active && 
                    strcmp(subscriptions[i].topic, topic_buf) == 0) {
                    if (subscriptions[i].handler) {
                        /* Pass only the JSON part, null-terminated */
                        json_buf[json_end] = '\0';
                        subscriptions[i].handler(topic_buf, json_buf, json_end);
                    }
                    break;
                }
            }
            k_mutex_unlock(&subscriptions_mutex);
#else
            LOG_ERR("File manager not available for audio storage");
            /* Consume the rest of the message */
            uint8_t discard[256];
            size_t remaining = total_len - json_read;
            while (remaining > 0) {
                size_t chunk = (remaining < sizeof(discard)) ? remaining : sizeof(discard);
                mqtt_read_publish_payload_blocking(client, discard, chunk);
                remaining -= chunk;
            }
#endif
        } else {
            /* Small message - read it all at once */
            uint8_t payload_buf[256];
            size_t payload_len = total_len;
            
            /* Ensure payload fits in buffer */
            if (payload_len > sizeof(payload_buf) - 1) {
                payload_len = sizeof(payload_buf) - 1;
            }
            
            /* Read the payload */
            int ret = mqtt_read_publish_payload_blocking(client, payload_buf, payload_len);
            if (ret < 0) {
                LOG_ERR("Failed to read MQTT payload: %d", ret);
                break;
            }
            payload_buf[payload_len] = '\0';
            
            LOG_DBG("Received message: %.*s", (int)payload_len, payload_buf);
            
            /* Find matching subscription and call handler */
            k_mutex_lock(&subscriptions_mutex, K_FOREVER);
            for (int i = 0; i < MAX_SUBSCRIPTIONS; i++) {
                if (subscriptions[i].active && 
                    strcmp(subscriptions[i].topic, topic_buf) == 0) {
                    if (subscriptions[i].handler) {
                        subscriptions[i].handler(topic_buf, payload_buf, payload_len);
                    }
                    break;
                }
            }
            k_mutex_unlock(&subscriptions_mutex);
        }
        
        /* Send PUBACK for QoS 1 */
        if (pub->message.topic.qos == MQTT_QOS_1_AT_LEAST_ONCE) {
            struct mqtt_puback_param puback = {
                .message_id = pub->message_id
            };
            mqtt_publish_qos1_ack(client, &puback);
        }
        break;
    }

    case MQTT_EVT_SUBACK:
        if (evt->result != 0) {
            LOG_ERR("MQTT SUBACK error: %d", evt->result);
        } else {
            LOG_INF("MQTT subscription acknowledged, id: %d", evt->param.suback.message_id);
        }
        break;

    case MQTT_EVT_UNSUBACK:
        if (evt->result != 0) {
            LOG_ERR("MQTT UNSUBACK error: %d", evt->result);
        } else {
            LOG_INF("MQTT unsubscribe acknowledged, id: %d", evt->param.unsuback.message_id);
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
                LOG_INF("Auto-reconnecting to MQTT broker (attempt %d, wait %d ms)...", 
                        reconnect_attempts + 1, reconnect_interval_ms);
                ret = mqtt_internal_connect();
                if (ret == 0) {
                    /* Wait a bit for the connection to establish */
                    k_sleep(K_MSEC(100));
                    /* Process any incoming CONNACK */
                    mqtt_input(&client);
                    /* Reset backoff on successful connection attempt */
                    reconnect_interval_ms = MIN_RECONNECT_INTERVAL_MS;
                    reconnect_attempts = 0;
                } else {
                    /* Connection failed, apply exponential backoff */
                    reconnect_attempts++;
                    reconnect_interval_ms = reconnect_interval_ms * RECONNECT_BACKOFF_MULTIPLIER;
                    if (reconnect_interval_ms > MAX_RECONNECT_INTERVAL_MS) {
                        reconnect_interval_ms = MAX_RECONNECT_INTERVAL_MS;
                    }
                    LOG_WRN("Reconnect failed, next attempt in %d seconds", 
                            reconnect_interval_ms / 1000);
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

    /* Generate client ID */
#ifdef CONFIG_RPR_MODULE_INIT_SM
    /* Get device ID from state machine */
    const char *device_id = init_state_machine_get_device_id();
    if (device_id && strlen(device_id) > 0) {
        snprintf(client_id_buffer, sizeof(client_id_buffer), "%s-speaker", device_id);
    } else {
        LOG_ERR("No device ID available from state machine");
        return -ENODEV;
    }
#else
    /* Legacy device ID generation */
    size_t device_id_len;
    const char *full_device_id = dev_info_get_device_id_str(&device_id_len);
    
#ifdef CONFIG_RPR_MODULE_DEVICE_REGISTRY
    /* Try to register with GitHub registry and get a unique short ID */
    device_registry_result_t reg_result;
    int reg_ret = device_registry_register(
        full_device_id,
        CONFIG_RPR_DEVICE_REGISTRY_PREFERRED_ID_LENGTH,
        CONFIG_RPR_DEVICE_REGISTRY_GITHUB_TOKEN,
        CONFIG_RPR_DEVICE_REGISTRY_REPO_OWNER,
        CONFIG_RPR_DEVICE_REGISTRY_REPO_NAME,
        &reg_result
    );
    
    if (reg_ret == 0) {
        LOG_INF("Device registered with GitHub, using ID: %s (length: %d)", 
                reg_result.assigned_id, reg_result.id_length);
        snprintf(client_id_buffer, sizeof(client_id_buffer), "%s-speaker", reg_result.assigned_id);
    } else if (reg_ret == -ENOTCONN) {
        LOG_WRN("No network available, registration deferred, using temporary ID");
        snprintf(client_id_buffer, sizeof(client_id_buffer), "%s-speaker", reg_result.assigned_id);
    } else {
        LOG_WRN("Failed to register with GitHub (%d), using default ID", reg_ret);
        snprintf(client_id_buffer, sizeof(client_id_buffer), "%.6s-speaker", full_device_id);
    }
#else
    /* Use default format "XXXXXX-speaker" (6 digits only) */
    snprintf(client_id_buffer, sizeof(client_id_buffer), "%.6s-speaker", full_device_id);
#endif
#endif /* CONFIG_RPR_MODULE_INIT_SM */
    
    LOG_INF("MQTT client ID: %s", client_id_buffer);
    
    /* MQTT client configuration */
    client.broker = &broker;
    client.evt_cb = mqtt_evt_handler;
    client.client_id.utf8 = (uint8_t *)client_id_buffer;
    client.client_id.size = strlen(client_id_buffer);
    /* Credentials (optional) */
#ifdef CONFIG_RPR_MQTT_USERNAME
    static struct mqtt_utf8 username = {
        .utf8 = (uint8_t *)CONFIG_RPR_MQTT_USERNAME,
        .size = sizeof(CONFIG_RPR_MQTT_USERNAME) - 1,
    };
    client.user_name = &username;
#else
    client.user_name = NULL;
#endif

#ifdef CONFIG_RPR_MQTT_PASSWORD
    static struct mqtt_utf8 password = {
        .utf8 = (uint8_t *)CONFIG_RPR_MQTT_PASSWORD,
        .size = sizeof(CONFIG_RPR_MQTT_PASSWORD) - 1,
    };
    client.password = &password;
#else
    client.password = NULL;
#endif
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
    
    /* Initialize subscriptions */
    k_mutex_init(&subscriptions_mutex);
    memset(subscriptions, 0, sizeof(subscriptions));

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

    /* Don't enable auto-reconnection here - let user enable it after successful connection */
    
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
    char payload[128];
    char topic[64];
    int ret;

    if (!mqtt_connected) {
        return MQTT_ERR_CONNECTION_FAILED;
    }

    /* Create heartbeat topic with client ID */
    ret = snprintf(topic, sizeof(topic), "%s/%s", 
                   CONFIG_RPR_MQTT_HEARTBEAT_TOPIC, client_id_buffer);
    if (ret < 0 || ret >= sizeof(topic)) {
        LOG_ERR("Failed to create heartbeat topic");
        return MQTT_ERR_PUBLISH_FAILED;
    }

    /* Create heartbeat payload with device info */
    size_t hw_id_len;
    const char *hw_device_id = dev_info_get_device_id_str(&hw_id_len);
    ret = snprintf(payload, sizeof(payload), 
                   "{\"alive\":true,\"seq\":%u,\"uptime\":%llu,\"deviceId\":\"%s\",\"hwId\":\"%s\",\"version\":\"%s\"}",
                   sequence_number++, k_uptime_get() / 1000, /* Convert to seconds */
                   client_id_buffer, hw_device_id ? hw_device_id : "", "1.0.0");
    
    if (ret < 0 || ret >= sizeof(payload)) {
        LOG_ERR("Failed to create heartbeat payload");
        return MQTT_ERR_PUBLISH_FAILED;
    }

    LOG_DBG("Publishing heartbeat to %s: %s", topic, payload);
    return mqtt_module_publish(topic, payload, strlen(payload));
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

mqtt_status_t mqtt_module_subscribe(const char *topic, uint8_t qos, mqtt_message_handler_t handler)
{
    if (!mqtt_initialized) {
        return MQTT_ERR_NOT_INITIALIZED;
    }
    
    if (!mqtt_connected) {
        return MQTT_ERR_CONNECTION_FAILED;
    }
    
    if (!topic || !handler) {
        return MQTT_ERR_INVALID_PARAM;
    }
    
    /* Find an available subscription slot */
    k_mutex_lock(&subscriptions_mutex, K_FOREVER);
    int slot = -1;
    for (int i = 0; i < MAX_SUBSCRIPTIONS; i++) {
        if (!subscriptions[i].active) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        k_mutex_unlock(&subscriptions_mutex);
        LOG_ERR("No available subscription slots");
        return MQTT_ERR_SUBSCRIBE_FAILED;
    }
    
    /* Store subscription info */
    strncpy(subscriptions[slot].topic, topic, sizeof(subscriptions[slot].topic) - 1);
    subscriptions[slot].topic[sizeof(subscriptions[slot].topic) - 1] = '\0';
    subscriptions[slot].handler = handler;
    subscriptions[slot].qos = qos;
    subscriptions[slot].active = true;
    k_mutex_unlock(&subscriptions_mutex);
    
    /* Create subscription list */
    struct mqtt_topic subscribe_topic = {
        .topic = {
            .utf8 = (uint8_t *)topic,
            .size = strlen(topic)
        },
        .qos = qos
    };
    
    struct mqtt_subscription_list sub_list = {
        .list = &subscribe_topic,
        .list_count = 1,
        .message_id = 1000 + slot  /* Use slot as part of message ID */
    };
    
    int ret = mqtt_subscribe(&client, &sub_list);
    if (ret != 0) {
        /* Clean up on failure */
        k_mutex_lock(&subscriptions_mutex, K_FOREVER);
        subscriptions[slot].active = false;
        k_mutex_unlock(&subscriptions_mutex);
        
        LOG_ERR("Failed to subscribe to topic '%s': %d", topic, ret);
        return MQTT_ERR_SUBSCRIBE_FAILED;
    }
    
    LOG_INF("Subscribed to topic '%s' with QoS %d", topic, qos);
    return MQTT_SUCCESS;
}

mqtt_status_t mqtt_module_unsubscribe(const char *topic)
{
    if (!mqtt_initialized) {
        return MQTT_ERR_NOT_INITIALIZED;
    }
    
    if (!mqtt_connected) {
        return MQTT_ERR_CONNECTION_FAILED;
    }
    
    if (!topic) {
        return MQTT_ERR_INVALID_PARAM;
    }
    
    /* Find the subscription */
    k_mutex_lock(&subscriptions_mutex, K_FOREVER);
    int slot = -1;
    for (int i = 0; i < MAX_SUBSCRIPTIONS; i++) {
        if (subscriptions[i].active && 
            strcmp(subscriptions[i].topic, topic) == 0) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        k_mutex_unlock(&subscriptions_mutex);
        LOG_WRN("Topic '%s' not found in subscriptions", topic);
        return MQTT_SUCCESS; /* Not an error, just not subscribed */
    }
    
    /* Mark as inactive */
    subscriptions[slot].active = false;
    k_mutex_unlock(&subscriptions_mutex);
    
    /* Create unsubscription list */
    struct mqtt_topic unsubscribe_topic = {
        .topic = {
            .utf8 = (uint8_t *)topic,
            .size = strlen(topic)
        }
    };
    
    struct mqtt_subscription_list unsub_list = {
        .list = &unsubscribe_topic,
        .list_count = 1,
        .message_id = 2000 + slot  /* Use slot as part of message ID */
    };
    
    int ret = mqtt_unsubscribe(&client, &unsub_list);
    if (ret != 0) {
        /* Restore subscription on failure */
        k_mutex_lock(&subscriptions_mutex, K_FOREVER);
        subscriptions[slot].active = true;
        k_mutex_unlock(&subscriptions_mutex);
        
        LOG_ERR("Failed to unsubscribe from topic '%s': %d", topic, ret);
        return MQTT_ERR_SUBSCRIBE_FAILED;
    }
    
    LOG_INF("Unsubscribed from topic '%s'", topic);
    return MQTT_SUCCESS;
}

/* Auto-initialize MQTT module during system startup */
#ifdef CONFIG_RPR_MODULE_MQTT
/* Module initialization is now done after network connectivity is established */
/* SYS_INIT(mqtt_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY); */
#endif

void mqtt_set_event_handler(mqtt_event_handler_t handler)
{
    event_handler = handler;
}

#endif /* CONFIG_RPR_MODULE_MQTT */
