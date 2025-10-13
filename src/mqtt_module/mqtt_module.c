/**
 * @file mqtt_module.c
 * @brief MQTT module implementation with heartbeat functionality
 */

#include "mqtt_module.h"

#ifdef CONFIG_RPR_MODULE_MQTT

#include <zephyr/kernel.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
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

#ifdef CONFIG_RPR_MODEM
#include "../modem/modem_module.h"
#endif

LOG_MODULE_REGISTER(mqtt_module, CONFIG_RPR_MODULE_MQTT_LOG_LEVEL);

/* MQTT client instance */
static struct mqtt_client client;
static struct sockaddr_storage broker;
static uint8_t rx_buffer[8192];   /* 8KB for receiving - supports larger messages */
static uint8_t tx_buffer[2048];   /* 2KB for sending (heartbeats, logs) */
static char client_id_buffer[32];  /* Buffer for "rr-speaker-XXXXX" format */

/* Heartbeat task control */
static struct k_work_delayable heartbeat_work;
static bool heartbeat_enabled = false;
static bool mqtt_connected = false;
static bool mqtt_initialized = false;

/* MQTT maintenance thread */
static struct k_thread mqtt_thread;
static k_tid_t mqtt_thread_id;
static K_THREAD_STACK_DEFINE(mqtt_thread_stack, 12288);  /* Increased from 8192 for file I/O in PUBLISH handler */
static bool mqtt_thread_running = false;

/* Audio message processing work queue */
#define AUDIO_WORK_QUEUE_STACK_SIZE 12288  /* Increased from 8192 to handle file operations */
static K_THREAD_STACK_DEFINE(audio_work_queue_stack, AUDIO_WORK_QUEUE_STACK_SIZE);
static struct k_work_q audio_work_queue;
static bool audio_work_queue_initialized = false;

/* Share latest temp audio file path with handler */
char g_mqtt_last_temp_file[32] = {0};

/* Work item for processing audio messages */
struct audio_msg_work {
    struct k_work work;
    char topic[128];
    bool in_use;
    uint8_t json_header[512];  /* Small buffer just for JSON header */
    size_t json_len;
    char temp_file[32];  /* Store filename instead of pointer */
};

#define AUDIO_WORK_ITEMS 4  /* Increased from 2 to reduce queue exhaustion */

static struct audio_msg_work audio_work_items[AUDIO_WORK_ITEMS];

/* Forward declaration */
static void process_audio_message_work(struct k_work *work);

/* Auto-reconnection state */
static bool auto_reconnect_enabled = true;
static int64_t last_reconnect_attempt = 0;
static int reconnect_interval_ms = 5000; /* Start with 5 seconds */
static int reconnect_attempts = 0;
#define MIN_RECONNECT_INTERVAL_MS 5000    /* 5 seconds minimum */
#define MAX_RECONNECT_INTERVAL_MS 60000   /* 60 seconds maximum (standalone device needs conservative retry) */
#define RECONNECT_BACKOFF_MULTIPLIER 2    /* Double the interval each failure */

/* Broker fallback state */
#ifdef CONFIG_RPR_MQTT_FALLBACK_BROKER_ENABLED
static bool using_fallback_broker = false;
static int current_broker_attempts = 0;
static int total_reconnect_cycles = 0;   /* Track total cycles for logging */
#endif

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
            
            /* Re-establish all subscriptions after reconnect */
            LOG_INF("Re-establishing subscriptions after reconnect");
            k_mutex_lock(&subscriptions_mutex, K_FOREVER);
            int active_count = 0;
            for (int i = 0; i < MAX_SUBSCRIPTIONS; i++) {
                if (subscriptions[i].active) {
                    active_count++;
                    LOG_INF("Re-subscribing to stored topic[%d]: '%s'", i, subscriptions[i].topic);
                    struct mqtt_topic subscribe_topic = {
                        .topic = {
                            .utf8 = (uint8_t *)subscriptions[i].topic,
                            .size = strlen(subscriptions[i].topic)
                        },
                        .qos = subscriptions[i].qos
                    };
                    
                    struct mqtt_subscription_list sub_list = {
                        .list = &subscribe_topic,
                        .list_count = 1,
                        .message_id = 3000 + i  /* Use a unique message ID for re-subscription */
                    };
                    
                    int ret = mqtt_subscribe(client, &sub_list);
                    if (ret < 0) {
                        LOG_ERR("Failed to re-subscribe to '%s': %d", 
                                subscriptions[i].topic, ret);
                    } else {
                        LOG_INF("Re-subscribed to topic '%s' (msg_id: %d)", 
                                subscriptions[i].topic, sub_list.message_id);
                    }
                }
            }
            LOG_INF("Re-subscription complete: %d active subscriptions", active_count);
            k_mutex_unlock(&subscriptions_mutex);
            
            /* Send immediate heartbeat to show device online quickly */
            if (heartbeat_enabled) {
                LOG_INF("Sending immediate heartbeat after connection");
                k_work_schedule(&heartbeat_work, K_NO_WAIT);
            }
            
#ifdef CONFIG_RPR_MODULE_INIT_SM
            /* Notify state machine of successful connection */
            init_state_machine_send_event(EVENT_MQTT_CONNECTED);
#endif
            reconnect_interval_ms = MIN_RECONNECT_INTERVAL_MS;
            reconnect_attempts = 0;
#ifdef CONFIG_RPR_MQTT_FALLBACK_BROKER_ENABLED
            current_broker_attempts = 0;
            /* Log which broker we successfully connected to */
            if (using_fallback_broker) {
                LOG_INF("Successfully connected to fallback broker (cycle %d)", total_reconnect_cycles);
            } else {
                LOG_INF("Successfully connected to primary broker");
            }
#endif
            
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
        
#ifdef CONFIG_RPR_MQTT_FALLBACK_BROKER_ENABLED
        /* Reset to primary broker on disconnect so we always try local first */
        if (using_fallback_broker) {
            LOG_INF("Disconnected from fallback broker, will retry primary broker first");
            using_fallback_broker = false;
        }
        current_broker_attempts = 0;
        reconnect_interval_ms = MIN_RECONNECT_INTERVAL_MS;
#endif
        
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
        
        LOG_INF("Received PUBLISH on topic '%s' [%zu bytes] msg_id=%u", topic_buf, total_len, pub->message_id);
        
        /* Send PUBACK immediately for QoS 1 messages to avoid broker timeout */
        if (pub->message.topic.qos == MQTT_QOS_1_AT_LEAST_ONCE) {
            struct mqtt_puback_param puback = {
                .message_id = pub->message_id
            };
            int ack_ret = mqtt_publish_qos1_ack(client, &puback);
            if (ack_ret < 0) {
                LOG_ERR("Failed to send PUBACK: %d", ack_ret);
            } else {
                LOG_DBG("PUBACK sent for message ID %u", pub->message_id);
            }
        }
        
        /* Check if this is a potentially large audio message */
        if (strstr(topic_buf, "rapidreach/audio/") != NULL && total_len > 256) {
            /* Handle large audio message - read and save in MQTT thread with frequent yields */
            LOG_INF("Processing large audio message (%zu bytes)", total_len);
            LOG_INF("MQTT connected status before file write: %d", mqtt_connected);
            
            /* First, read up to 512 bytes to get the JSON header */
            uint8_t json_buf[512];
            size_t json_read = (total_len < sizeof(json_buf)) ? total_len : sizeof(json_buf);
            int ret = mqtt_read_publish_payload_blocking(client, json_buf, json_read);
            if (ret < 0) {
                LOG_ERR("Failed to read JSON header: %d", ret);
                break;
            }
            
            /* Find the end of JSON using proper brace counting */
            int json_end = -1;
            int brace_count = 0;
            bool in_string = false;
            bool escape_next = false;
            
            for (int i = 0; i < json_read; i++) {
                char c = json_buf[i];
                
                if (escape_next) {
                    escape_next = false;
                    continue;
                }
                
                if (c == '\\' && in_string) {
                    escape_next = true;
                    continue;
                }
                
                if (c == '"' && !escape_next) {
                    in_string = !in_string;
                    continue;
                }
                
                if (!in_string) {
                    if (c == '{') {
                        brace_count++;
                    } else if (c == '}') {
                        brace_count--;
                        if (brace_count == 0) {
                            json_end = i + 1;
                            break;
                        }
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
            
            /* Yield to let MQTT thread process any pending packets before file write */
            k_yield();
            k_sleep(K_MSEC(5));
            
#ifdef CONFIG_RPR_MODULE_FILE_MANAGER
            /* Save audio data to file with frequent yields */
            /* Use unique filename with timestamp and counter */
            static uint32_t file_counter = 0;
            char audio_file[32];
            uint32_t timestamp = k_uptime_get_32() & 0xFFFF; /* Use lower 16 bits of uptime */
            snprintf(audio_file, sizeof(audio_file), "/lfs/mqtt_audio_%04x_%03u.opus", 
                     timestamp, (file_counter++) % 1000);
            
            struct fs_file_t file;
            fs_file_t_init(&file);
            
            /* Ensure file system is mounted */
            ret = file_manager_init();
            if (ret < 0 && ret != -EALREADY) {
                LOG_ERR("Failed to initialize file manager: %d", ret);
                /* Consume the rest */
                uint8_t discard[256];
                size_t remaining = total_len - json_read;
                while (remaining > 0) {
                    size_t chunk = (remaining < sizeof(discard)) ? remaining : sizeof(discard);
                    mqtt_read_publish_payload_blocking(client, discard, chunk);
                    remaining -= chunk;
                }
                break;
            }
            
            /* Calculate audio data position and size */
            size_t audio_in_buf = 0;
            if (json_read > json_end) {
                audio_in_buf = json_read - json_end;
            }
            size_t audio_remaining = (total_len > json_end) ? (total_len - json_end) : 0;
            
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
                
                /* Note: Cannot call mqtt_input/mqtt_live while reading payload */
            }
            
            /* Read and write the rest in smaller chunks with more frequent yields */
            /* NOTE: We cannot call mqtt_input/mqtt_live while reading the payload
             * because the socket is busy with mqtt_read_publish_payload_blocking.
             * This means MQTT packets (including keepalives) cannot be processed
             * during the ~1.3 second file write operation. The 60-second keepalive
             * timeout should be sufficient to handle this delay. */
            uint8_t chunk_buf[1024];  /* Safe with increased network RX stack (4KB) */
            int64_t start_time = k_uptime_get();
            LOG_INF("Starting file write at time: %lld", start_time);
            
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
                
                /* Cannot call mqtt_input/mqtt_live while reading publish payload */
                /* The socket is busy with mqtt_read_publish_payload_blocking */
                
                /* Yield after every chunk (64 bytes) for maximum responsiveness */
                k_yield();
                
                /* Skip progress logging to reduce spam */
            }
            
            /* Skip explicit sync - fs_close should handle it */
            /* fs_sync(&file); */
            fs_close(&file);
            int64_t end_time = k_uptime_get();
            LOG_INF("Audio saved to %s (%zu bytes) at time: %lld (duration: %lld ms)", 
                    audio_file, written, end_time, (end_time - start_time));
            
            /* Give the MQTT thread time to process after the long blocking operation */
            k_yield();
            k_sleep(K_MSEC(10));  /* Brief delay to let MQTT thread catch up */
            
            /* Queue work item for handler call */
            struct audio_msg_work *work_item = NULL;
            for (int i = 0; i < AUDIO_WORK_ITEMS; i++) {
                if (!audio_work_items[i].in_use) {
                    work_item = &audio_work_items[i];
                    work_item->in_use = true;
                    break;
                }
            }
            
            if (work_item) {
                /* Copy JSON header */
                memcpy(work_item->json_header, json_buf, json_end);
                work_item->json_len = json_end;
                    strncpy(work_item->topic, topic_buf, sizeof(work_item->topic) - 1);
                    work_item->topic[sizeof(work_item->topic) - 1] = '\0';
                    strncpy(work_item->temp_file, audio_file, sizeof(work_item->temp_file) - 1);
                    work_item->temp_file[sizeof(work_item->temp_file) - 1] = '\0';
                
                /* Update global latest temp file for handler */
                strncpy(g_mqtt_last_temp_file, audio_file, sizeof(g_mqtt_last_temp_file) - 1);
                g_mqtt_last_temp_file[sizeof(g_mqtt_last_temp_file) - 1] = '\0';

                /* Submit to work queue */
                ret = k_work_submit_to_queue(&audio_work_queue, &work_item->work);
                if (ret < 0) {
                    LOG_ERR("Failed to submit audio work to queue: %d - message dropped", ret);
                    work_item->in_use = false;
                    /* Don't call handler directly - that defeats the work queue purpose
                     * and can cause deadlocks. Just drop the message. */
                }
            } else {
                LOG_ERR("No available work items - all %d items in use, dropping audio message", 
                        AUDIO_WORK_ITEMS);
                LOG_WRN("Consider increasing AUDIO_WORK_ITEMS if this happens frequently");
                /* Don't call handler directly - that can cause deadlocks in MQTT thread */
                /* The audio file has been saved, but we can't process it without available work items */
            }
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
        
        /* PUBACK already sent at the beginning of message processing */
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
    
    /* Clean up any previous connection state */
    if (client.transport.tcp.sock >= 0) {
        LOG_INF("Cleaning up previous socket (fd=%d) before reconnect", client.transport.tcp.sock);
        mqtt_disconnect(&client);
        k_sleep(K_MSEC(100)); /* Increased delay for cleanup */
        
        /* Force socket closed if still open */
        if (client.transport.tcp.sock >= 0) {
            zsock_close(client.transport.tcp.sock);
            client.transport.tcp.sock = -1;
        }
    }
    
    /* Always re-initialize the client structure for clean state */
    ret = prepare_mqtt_client();
    if (ret != 0) {
        LOG_ERR("Failed to re-prepare MQTT client: %d", ret);
        return ret;
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
            if (ret < 0 && ret != -EAGAIN) {
                if (ret == -ENOTCONN || ret == -ECONNRESET || ret == -EPIPE) {
                    LOG_ERR("MQTT connection lost in mqtt_input: %d (time: %lld)", ret, current_time);
                    LOG_ERR("Connection lost reason: ENOTCONN=%d, ECONNRESET=%d, EPIPE=%d", 
                            (ret == -ENOTCONN), (ret == -ECONNRESET), (ret == -EPIPE));
                    mqtt_connected = false;
                    last_reconnect_attempt = current_time;
                    
                    /* Mark socket as invalid to force cleanup on reconnect */
                    client.transport.tcp.sock = -1;
                    
                    /* Notify about disconnection */
                    if (event_handler) {
                        event_handler(MQTT_EVENT_DISCONNECTED);
                    }
                } else if (ret == -EBUSY) {
                    /* Busy means a publish payload read is in progress; not a disconnect. */
                    LOG_DBG("MQTT busy (payload read in progress), will retry later");
                } else if (ret != -EAGAIN && ret != -EWOULDBLOCK) {
                    LOG_DBG("MQTT input error (non-fatal): %d", ret);
                    /* Don't immediately disconnect on other errors */
                }
            }
            
            /* Keep connection alive only if still connected */
            if (mqtt_connected) {
                static int64_t last_live_log = 0;
                int64_t now = k_uptime_get();
                if (now - last_live_log > 5000) {  /* Log every 5 seconds */
                    LOG_DBG("Calling mqtt_live()");
                    last_live_log = now;
                }
                
                ret = mqtt_live(&client);
                if (ret < 0 && ret != -EAGAIN) {
                    if (ret == -ENOTCONN || ret == -ECONNRESET || ret == -EPIPE) {
                        LOG_ERR("MQTT connection lost during live: %d (time: %lld)", ret, current_time);
                        LOG_ERR("Live error: ENOTCONN=%d, ECONNRESET=%d, EPIPE=%d",
                                (ret == -ENOTCONN), (ret == -ECONNRESET), (ret == -EPIPE));
                        mqtt_connected = false;
                        last_reconnect_attempt = current_time;
                        
                        /* Mark socket as invalid to force cleanup on reconnect */
                        client.transport.tcp.sock = -1;
                        
                        /* Notify about disconnection */
                        if (event_handler) {
                            event_handler(MQTT_EVENT_DISCONNECTED);
                        }
                    } else if (ret == -EBUSY) {
                        /* Busy while keepalive typically means payload read in progress. Not fatal. */
                        LOG_DBG("mqtt_live busy (payload read in progress), will retry later");
                    } else {
                        LOG_DBG("MQTT live error (non-fatal): %d", ret);
                    }
                }
            }
        } else if (auto_reconnect_enabled) {
            /* Attempt auto-reconnection if enough time has passed */
            if (current_time - last_reconnect_attempt >= reconnect_interval_ms) {
#ifdef CONFIG_RPR_MQTT_FALLBACK_BROKER_ENABLED
                /* Check if we should switch brokers after failed attempts */
                if (current_broker_attempts >= CONFIG_RPR_MQTT_PRIMARY_RETRIES) {
                    /* Time to switch brokers */
                    if (!using_fallback_broker) {
                        /* Switch to fallback broker */
#ifdef CONFIG_RPR_MODEM
                        if (is_modem_connected()) {
                            LOG_WRN("Primary broker failed %d times, switching to fallback broker (cycle %d)",
                                    current_broker_attempts, total_reconnect_cycles);
                            using_fallback_broker = true;
                            current_broker_attempts = 0;
                            reconnect_interval_ms = MIN_RECONNECT_INTERVAL_MS;
                            
                            /* Set modem interface as default for public connectivity */
                            modem_set_iface_default();
                        } else {
                            LOG_WRN("Cannot switch to fallback broker: modem not connected");
                            LOG_INF("Attempting to initialize modem for fallback connection...");
                            c16qs_modem_status_t modem_status = modem_init_and_connect();
                            if (modem_status == MODEM_SUCCESS) {
                                LOG_INF("Modem initialized successfully, will retry fallback on next attempt");
                            } else {
                                LOG_ERR("Modem initialization failed: %d, will retry primary", modem_status);
                                /* Reset attempts to retry primary broker again */
                                current_broker_attempts = 0;
                                reconnect_interval_ms = MIN_RECONNECT_INTERVAL_MS;
                            }
                            last_reconnect_attempt = current_time;
                            continue;
                        }
#else
                        LOG_WRN("Fallback broker requires modem support (disabled), retrying primary");
                        current_broker_attempts = 0;
                        reconnect_interval_ms = MIN_RECONNECT_INTERVAL_MS;
#endif /* CONFIG_RPR_MODEM */
                    } else {
                        /* Switch back to primary broker */
                        LOG_WRN("Fallback broker failed %d times, switching back to primary broker (cycle %d)",
                                current_broker_attempts, total_reconnect_cycles);
                        using_fallback_broker = false;
                        current_broker_attempts = 0;
                        reconnect_interval_ms = MIN_RECONNECT_INTERVAL_MS;
                        total_reconnect_cycles++;
                    }
                }
#endif /* CONFIG_RPR_MQTT_FALLBACK_BROKER_ENABLED */

                const char *broker_type = "MQTT broker";
#ifdef CONFIG_RPR_MQTT_FALLBACK_BROKER_ENABLED
                broker_type = using_fallback_broker ? "fallback broker" : "primary broker";
#endif
                LOG_INF("Auto-reconnecting to %s (attempt %d/%d, interval %d s)...", 
                        broker_type, current_broker_attempts + 1, 
                        CONFIG_RPR_MQTT_PRIMARY_RETRIES, reconnect_interval_ms / 1000);
                
                ret = mqtt_internal_connect();
                if (ret == 0) {
                    /* Wait a bit for the connection to establish */
                    k_sleep(K_MSEC(100));
                    /* Process any incoming CONNACK */
                    mqtt_input(&client);
                    /* Reset backoff on successful connection attempt */
                    reconnect_interval_ms = MIN_RECONNECT_INTERVAL_MS;
                    reconnect_attempts = 0;
#ifdef CONFIG_RPR_MQTT_FALLBACK_BROKER_ENABLED
                    current_broker_attempts = 0;
                    /* Note: keep using current broker until disconnect, don't reset to primary */
#endif
                } else {
                    /* Connection failed, apply exponential backoff */
                    reconnect_attempts++;
#ifdef CONFIG_RPR_MQTT_FALLBACK_BROKER_ENABLED
                    current_broker_attempts++;
#endif
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
        
        /* Sleep for a very short time to avoid busy waiting and ensure we don't miss messages */
        k_sleep(K_MSEC(10));  /* Reduced to 10ms for better message reception */
    }
    
    LOG_INF("MQTT maintenance thread stopped");
}

/**
 * @brief Heartbeat work handler - publishes periodic alive messages
 */
static void heartbeat_work_handler(struct k_work *work)
{
    static uint32_t sequence_number = 0;
    char payload[256];
    int ret;
    char ip_addr[NET_IPV4_ADDR_LEN] = "0.0.0.0";

    if (!mqtt_connected) {
        LOG_WRN("MQTT not connected, skipping heartbeat");
        goto reschedule;
    }

    /* Get current IP address */
#ifdef CONFIG_RPR_ETHERNET
    struct net_if *iface = net_if_get_default();
    if (iface) {
        const struct net_if_config *cfg = net_if_get_config(iface);
        if (cfg && cfg->ip.ipv4) {
            net_addr_ntop(AF_INET, &cfg->ip.ipv4->unicast[0].ipv4.address.in_addr, 
                         ip_addr, sizeof(ip_addr));
        }
    }
#endif

    /* Get device info */
    size_t len = 0;
    const char *fw_ver = dev_info_get_fw_version_str(&len);
    const char *device_id_full = dev_info_get_device_id_str(&len);
    
    /* Extract numeric device ID (first 6 chars) for consistent identification */
    char device_id_numeric[7] = "000000";
    if (device_id_full && len >= 6) {
        strncpy(device_id_numeric, device_id_full, 6);
        device_id_numeric[6] = '\0';
    }
    
    uint32_t uptime_sec = k_uptime_get_32() / 1000;

    /* Create heartbeat payload with full device info */
    ret = snprintf(payload, sizeof(payload), 
                   "{\"alive\":true,\"deviceId\":\"%s\",\"seq\":%u,\"uptime\":%u,"
                   "\"version\":\"%s\",\"ip\":\"%s\",\"hwId\":\"%s\"}",
                   device_id_numeric,
                   sequence_number++, uptime_sec,
                   fw_ver ? fw_ver : "unknown",
                   ip_addr,
                   device_id_full ? device_id_full : "unknown");
    
    if (ret < 0 || ret >= sizeof(payload)) {
        LOG_ERR("Failed to create heartbeat payload");
        goto reschedule;
    }

    /* Create heartbeat topic with client ID */
    char topic[64];
    ret = snprintf(topic, sizeof(topic), "%s/%s", 
                   CONFIG_RPR_MQTT_HEARTBEAT_TOPIC, client_id_buffer);
    if (ret < 0 || ret >= sizeof(topic)) {
        LOG_ERR("Failed to create heartbeat topic");
        goto reschedule;
    }

    /* Publish heartbeat message */
    ret = mqtt_module_publish(topic, payload, strlen(payload));
    if (ret != MQTT_SUCCESS) {
        LOG_WRN("Failed to publish heartbeat: %d", ret);
    } else {
        LOG_INF("Published heartbeat to %s: %s", topic, payload);
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
    const char *broker_host;
    int broker_port;

#ifdef CONFIG_RPR_MQTT_FALLBACK_BROKER_ENABLED
    /* Select broker based on fallback state */
    if (using_fallback_broker) {
        broker_host = CONFIG_RPR_MQTT_FALLBACK_BROKER_HOST;
        broker_port = CONFIG_RPR_MQTT_FALLBACK_BROKER_PORT;
        LOG_INF("Using fallback MQTT broker: %s:%d", broker_host, broker_port);
    } else {
        broker_host = CONFIG_RPR_MQTT_BROKER_HOST;
        broker_port = CONFIG_RPR_MQTT_BROKER_PORT;
        LOG_INF("Using primary MQTT broker: %s:%d", broker_host, broker_port);
    }
#else
    broker_host = CONFIG_RPR_MQTT_BROKER_HOST;
    broker_port = CONFIG_RPR_MQTT_BROKER_PORT;
#endif

    /* Configure broker address */
    broker4->sin_family = AF_INET;
    broker4->sin_port = htons(broker_port);
    
    ret = zsock_inet_pton(AF_INET, broker_host, &broker4->sin_addr);
    if (ret != 1) {
        LOG_ERR("Invalid broker IP address: %s", broker_host);
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
    client.clean_session = 1; /* Use clean session to avoid stale state */

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

/**
 * @brief Process audio message in work queue (not MQTT thread)
 */
static void process_audio_message_work(struct k_work *work)
{
    struct audio_msg_work *audio_work = CONTAINER_OF(work, struct audio_msg_work, work);
    int json_end = -1;
    
    LOG_INF("Processing audio message in work queue: topic='%s', json_len=%zu", 
            audio_work->topic, audio_work->json_len);
    
    /* Parse the JSON header to find boundary */
    int brace_count = 0;
    bool in_string = false;
    bool escape_next = false;
    
    for (size_t i = 0; i < audio_work->json_len; i++) {
        char c = audio_work->json_header[i];
        
        if (escape_next) {
            escape_next = false;
            continue;
        }
        
        if (c == '\\' && in_string) {
            escape_next = true;
            continue;
        }
        
        if (c == '"' && !escape_next) {
            in_string = !in_string;
            continue;
        }
        
        if (!in_string) {
            if (c == '{') {
                brace_count++;
            } else if (c == '}') {
                brace_count--;
                if (brace_count == 0) {
                    json_end = i + 1;
                    break;
                }
            }
        }
    }
    
    if (json_end < 0) {
        LOG_ERR("Could not find JSON boundary in header");
        goto cleanup;
    }
    
    LOG_INF("JSON is %d bytes", json_end);
    
    /* Call handler with JSON metadata only */
    k_mutex_lock(&subscriptions_mutex, K_FOREVER);
    for (int i = 0; i < MAX_SUBSCRIPTIONS; i++) {
        if (subscriptions[i].active && 
            strcmp(subscriptions[i].topic, audio_work->topic) == 0) {
            if (subscriptions[i].handler) {
                /* Null-terminate JSON and call handler */
                audio_work->json_header[json_end] = '\0';
                subscriptions[i].handler(audio_work->topic, audio_work->json_header, json_end);
            }
            break;
        }
    }
    k_mutex_unlock(&subscriptions_mutex);
    
    LOG_INF("Audio message handler called, file: %s", audio_work->temp_file);

cleanup:
    /* Mark work item as available */
    audio_work->in_use = false;
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
    
    /* Initialize audio work queue */
    if (!audio_work_queue_initialized) {
        k_work_queue_init(&audio_work_queue);
        k_work_queue_start(&audio_work_queue, audio_work_queue_stack,
                          K_THREAD_STACK_SIZEOF(audio_work_queue_stack),
                          K_PRIO_PREEMPT(10), NULL);
        k_thread_name_set(&audio_work_queue.thread, "audio_work_queue");
        
        /* Initialize work items */
        for (int i = 0; i < AUDIO_WORK_ITEMS; i++) {
            k_work_init(&audio_work_items[i].work, process_audio_message_work);
            audio_work_items[i].in_use = false;
        }
        
        audio_work_queue_initialized = true;
        LOG_INF("Audio work queue initialized");
    }

    /* Don't start the maintenance thread yet - wait for explicit connect */
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

    /* Start MQTT maintenance thread if not already running */
    if (!mqtt_thread_running) {
        /* Set last reconnect attempt to current time to prevent immediate auto-reconnect */
        last_reconnect_attempt = k_uptime_get();
        
        mqtt_thread_running = true;
        mqtt_thread_id = k_thread_create(&mqtt_thread, mqtt_thread_stack,
                                         K_THREAD_STACK_SIZEOF(mqtt_thread_stack),
                                         mqtt_thread_func, NULL, NULL, NULL,
                                         K_PRIO_COOP(7), 0, K_NO_WAIT);
        k_thread_name_set(mqtt_thread_id, "mqtt_maintenance");
        LOG_INF("MQTT maintenance thread started");
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
    static uint16_t message_id_counter = 1;
    param.message.topic.qos = MQTT_QOS_1_AT_LEAST_ONCE;
    param.message.topic.topic.utf8 = (uint8_t *)topic;
    param.message.topic.topic.size = strlen(topic);
    param.message.payload.data = (uint8_t *)payload;
    param.message.payload.len = payload_len;
    param.message_id = message_id_counter++; /* Unique message ID for each publish */
    if (message_id_counter == 0) message_id_counter = 1; /* Avoid 0 */
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
    /* Heartbeat is now fully handled by heartbeat_work_handler */
    /* This function is kept for backward compatibility but should not be called directly */
    LOG_WRN("mqtt_send_heartbeat() called directly - heartbeat should be managed by work handler");
    return MQTT_SUCCESS;
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
    
    /* Store subscription info (even if not connected - will subscribe on connect) */
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
    
    /* Only try to subscribe if connected */
    if (mqtt_connected) {
        int ret = mqtt_subscribe(&client, &sub_list);
        if (ret != 0) {
            /* Don't clean up - keep subscription for retry on reconnect */
            LOG_WRN("Failed to subscribe to topic '%s' now (err: %d), will retry on reconnect", topic, ret);
            return MQTT_ERR_SUBSCRIBE_FAILED;
        }
        LOG_INF("Subscribed to topic '%s' with QoS %d", topic, qos);
    } else {
        LOG_INF("Stored subscription to topic '%s' for later (not connected yet)", topic);
    }
    
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
