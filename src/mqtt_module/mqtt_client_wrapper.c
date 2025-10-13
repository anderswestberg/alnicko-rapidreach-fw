/**
 * @file mqtt_client_wrapper.c
 * @brief Thread-safe MQTT client wrapper implementation
 * 
 * This implementation provides:
 * - Separate protocol thread for MQTT operations
 * - Non-blocking publish/subscribe
 * - Automatic reconnection with exponential backoff
 * - Worker thread for message processing
 */

#include "mqtt_client_wrapper.h"

#ifdef CONFIG_RPR_MQTT_CLIENT_WRAPPER

#include <zephyr/net/mqtt.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/dns_resolve.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/kernel/thread_stack.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <zephyr/fs/fs.h>
#include "mqtt_message_parser.h"
#include "cJSON.h"

LOG_MODULE_REGISTER(mqtt_wrapper, LOG_LEVEL_DBG);

/* Utility allocation helpers for thread stacks */
static void *alloc_thread_stack_mem(size_t requested_size)
{
    size_t bytes = K_KERNEL_STACK_LEN(requested_size);
    void *mem = k_aligned_alloc(Z_KERNEL_STACK_OBJ_ALIGN, bytes);
    if (mem) {
        memset(mem, 0, bytes);
    }
    return mem;
}

/* Stack definitions moved into wrapper struct to support multiple instances
 * Old design had global stacks which caused corruption when multiple wrappers were created */

/* Message entry for work queue */
struct mqtt_msg_work {
    struct k_work work;
    char topic[128];
    uint8_t *payload;
    size_t payload_len;
    mqtt_msg_received_cb_t callback;
    void *user_data;
};

/* MQTT client wrapper structure */
struct mqtt_client_wrapper {
    /* MQTT client */
    struct mqtt_client client;
    struct sockaddr_storage broker;
    
    /* Flag to indicate large message was just processed */
    volatile bool large_msg_processed;
    uint8_t rx_buffer[CONFIG_MQTT_WRAPPER_RX_BUFFER_SIZE];
    uint8_t tx_buffer[CONFIG_MQTT_WRAPPER_TX_BUFFER_SIZE];
    char client_id[64];
    
    /* Protocol thread */
    struct k_thread protocol_thread;
    k_tid_t protocol_thread_id;
    volatile bool thread_running;
    volatile bool should_stop;
    
    /* Worker queue for message processing */
    struct k_work_q msg_work_queue;
    
    /* PER-INSTANCE thread stacks - dynamically allocated to avoid struct size issues */
    k_thread_stack_t *protocol_stack;
    k_thread_stack_t *worker_stack;
    void *protocol_stack_mem;
    void *worker_stack_mem;
    
    /* Connection state */
    struct k_mutex state_mutex;
    volatile bool connected;
    volatile bool connecting;
    bool auto_reconnect;
    int64_t last_reconnect_ms;
    int reconnect_interval_ms;
    
    /* Callbacks */
    mqtt_conn_state_cb_t conn_cb;
    void *conn_cb_data;
    
    /* Subscriptions - simple array for now */
    struct {
        char topic[128];
        mqtt_msg_received_cb_t handler;
        void *user_data;
        uint8_t qos;
        bool active;
    } subs[CONFIG_MQTT_WRAPPER_MAX_SUBSCRIPTIONS];
};

/* Forward declarations */
static void protocol_thread_func(void *p1, void *p2, void *p3);
static void mqtt_evt_handler(struct mqtt_client *const client,
                             const struct mqtt_evt *evt);
static void process_message_work(struct k_work *work);

/**
 * @brief Process received message in worker thread
 */
static void process_message_work(struct k_work *work)
{
    struct mqtt_msg_work *msg_work = CONTAINER_OF(work, struct mqtt_msg_work, work);
    
    LOG_DBG("Processing message from topic: %s (%zu bytes)", 
            msg_work->topic, msg_work->payload_len);

    /* Safety check - don't call callback with NULL payload */
    if (!msg_work->payload) {
        LOG_ERR("NULL payload in work item (len=%zu) - dropping message", 
                msg_work->payload_len);
        k_free(msg_work);
        return;
    }
    
    /* Call the application callback */
    if (msg_work->callback) {
        msg_work->callback(msg_work->topic, msg_work->payload, 
                          msg_work->payload_len, msg_work->user_data);
    }
    
    /* Free resources */
    if (msg_work->payload) {
        k_free(msg_work->payload);
    }
    k_free(msg_work);
}

/**
 * @brief Create and initialize MQTT client wrapper
 */
mqtt_handle_t mqtt_wrapper_create(const struct mqtt_client_config *config)
{
    struct mqtt_client_wrapper *w;
    
    if (!config || !config->client_id) {
        LOG_ERR("Invalid configuration");
        return NULL;
    }
    
    /* Allocate wrapper */
    w = k_calloc(1, sizeof(struct mqtt_client_wrapper));
    if (!w) {
        LOG_ERR("Failed to allocate wrapper");
        return NULL;
    }
    
    /* Allocate per-instance thread stacks with proper alignment */
    w->protocol_stack_mem = alloc_thread_stack_mem(CONFIG_MQTT_WRAPPER_PROTOCOL_STACK_SIZE);
    if (!w->protocol_stack_mem) {
        LOG_ERR("Failed to allocate protocol stack (%zu bytes)",
                (size_t)K_KERNEL_STACK_LEN(CONFIG_MQTT_WRAPPER_PROTOCOL_STACK_SIZE));
        k_free(w);
        return NULL;
    }
    w->protocol_stack = (k_thread_stack_t *)w->protocol_stack_mem;
    
    w->worker_stack_mem = alloc_thread_stack_mem(CONFIG_MQTT_WRAPPER_WORKER_STACK_SIZE);
    if (!w->worker_stack_mem) {
        LOG_ERR("Failed to allocate worker stack (%zu bytes)",
                (size_t)K_KERNEL_STACK_LEN(CONFIG_MQTT_WRAPPER_WORKER_STACK_SIZE));
        k_free(w->protocol_stack_mem);
        k_free(w);
        return NULL;
    }
    w->worker_stack = (k_thread_stack_t *)w->worker_stack_mem;
    
    LOG_INF("Allocated per-instance stacks: protocol=%p, worker=%p",
            (void*)w->protocol_stack, (void*)w->worker_stack);
    
    /* Initialize mutex */
    k_mutex_init(&w->state_mutex);
    
    /* Copy client ID */
    strncpy(w->client_id, config->client_id, sizeof(w->client_id) - 1);
    
    /* Resolve broker address */
    struct addrinfo *res;
    struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM
    };
    
    int ret = getaddrinfo(config->broker_hostname, NULL, &hints, &res);
    if (ret != 0) {
        LOG_ERR("Failed to resolve hostname %s", config->broker_hostname);
        k_free(w);
        return NULL;
    }
    
    memcpy(&w->broker, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    
    /* Set port */
    struct sockaddr_in *addr4 = (struct sockaddr_in *)&w->broker;
    addr4->sin_port = htons(config->broker_port);
    
    /* Initialize MQTT client structure */
    mqtt_client_init(&w->client);
    
    /* Configure MQTT client */
    w->client.broker = &w->broker;
    w->client.evt_cb = mqtt_evt_handler;
    w->client.client_id.utf8 = (uint8_t *)w->client_id;
    w->client.client_id.size = strlen(w->client_id);
    w->client.protocol_version = MQTT_VERSION_3_1_1;
    w->client.rx_buf = w->rx_buffer;
    w->client.rx_buf_size = sizeof(w->rx_buffer);
    w->client.tx_buf = w->tx_buffer;
    w->client.tx_buf_size = sizeof(w->tx_buffer);
    w->client.keepalive = config->keepalive_interval;
    w->client.clean_session = config->clean_session ? 1 : 0;
    w->client.user_data = w;
    
    /* Initialize work queue with PER-INSTANCE stack */
    k_work_queue_init(&w->msg_work_queue);
    k_work_queue_start(&w->msg_work_queue,
                       w->worker_stack,
                       CONFIG_MQTT_WRAPPER_WORKER_STACK_SIZE,
                       CONFIG_MQTT_WRAPPER_WORKER_THREAD_PRIORITY,
                       NULL);
    k_thread_name_set(&w->msg_work_queue.thread, "mqtt_worker");
    
    /* Set defaults */
    w->auto_reconnect = true;
    w->reconnect_interval_ms = CONFIG_MQTT_WRAPPER_RECONNECT_MIN_INTERVAL_MS;
    w->large_msg_processed = false;
    
    LOG_INF("MQTT wrapper initialized: %s", w->client_id);
    return (mqtt_handle_t)w;
}

/**
 * @brief Protocol thread - handles MQTT protocol
 */
static void protocol_thread_func(void *p1, void *p2, void *p3)
{
    struct mqtt_client_wrapper *w = (struct mqtt_client_wrapper *)p1;
    struct pollfd fds[1];
    int ret;
    
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);
    
    LOG_INF("MQTT protocol thread started");
    
    while (!w->should_stop) {
        k_mutex_lock(&w->state_mutex, K_FOREVER);
        bool is_connected = w->connected;
        bool is_connecting = w->connecting;
        k_mutex_unlock(&w->state_mutex);
        
        /* Process MQTT events even when connecting */
        if (is_connected || is_connecting) {
            /* Setup poll */
            fds[0].fd = w->client.transport.tcp.sock;
            fds[0].events = POLLIN;
            
            /* Poll with timeout */
            ret = poll(fds, 1, 100);
            
            if (ret > 0 && (fds[0].revents & POLLIN)) {
                /* Process incoming data */
                LOG_DBG("MQTT wrapper processing input...");
                ret = mqtt_input(&w->client);
                if (ret < 0 && ret != -EAGAIN) {
                    /* Suppress repeated errors during reconnect */
                    if (ret != -EBUSY) {
                        LOG_ERR("mqtt_input error: %d", ret);
                    }
                    k_mutex_lock(&w->state_mutex, K_FOREVER);
                    w->connected = false;
                    k_mutex_unlock(&w->state_mutex);
                    continue;
                }
                
                /* If we just processed a large message, give MQTT client time to settle */
                if (w->large_msg_processed) {
                    LOG_DBG("Large message processed, adding delay before next poll");
                    w->large_msg_processed = false;
                    k_sleep(K_MSEC(50));
                }
            } else if (ret < 0) {
                LOG_ERR("MQTT wrapper poll error: %d", ret);
            }
            
            /* Send keepalive */
            ret = mqtt_live(&w->client);
            if (ret < 0 && ret != -EAGAIN) {
                LOG_ERR("mqtt_live error: %d", ret);
                k_mutex_lock(&w->state_mutex, K_FOREVER);
                w->connected = false;
                k_mutex_unlock(&w->state_mutex);
            }
        } else if (w->auto_reconnect && !is_connecting) {
            /* Reconnection logic */
            int64_t now = k_uptime_get();
            if (now - w->last_reconnect_ms >= w->reconnect_interval_ms) {
                LOG_INF("MQTT wrapper attempting connection...");
                k_mutex_lock(&w->state_mutex, K_FOREVER);
                w->connecting = true;
                w->last_reconnect_ms = now;
                k_mutex_unlock(&w->state_mutex);
                
                ret = mqtt_connect(&w->client);
                if (ret < 0) {
                    LOG_ERR("MQTT wrapper connect failed: %d", ret);
                    k_mutex_lock(&w->state_mutex, K_FOREVER);
                    w->connecting = false;
                    /* Exponential backoff */
                    w->reconnect_interval_ms = MIN(
                        w->reconnect_interval_ms * 2,
                        CONFIG_MQTT_WRAPPER_RECONNECT_MAX_INTERVAL_MS
                    );
                    k_mutex_unlock(&w->state_mutex);
                } else {
                    LOG_INF("MQTT wrapper connect initiated successfully");
                }
            }
        }
        
        k_sleep(K_MSEC(10));
    }
    
    /* Disconnect if connected */
    if (w->connected) {
        mqtt_disconnect(&w->client);
    }
    
    LOG_INF("MQTT protocol thread stopped");
}

/**
 * @brief MQTT event handler
 */
static void mqtt_evt_handler(struct mqtt_client *const client,
                             const struct mqtt_evt *evt)
{
    /* Safety checks */
    if (!client || !evt) {
        LOG_ERR("NULL client or evt in event handler");
        return;
    }
    
    struct mqtt_client_wrapper *w = (struct mqtt_client_wrapper *)client->user_data;
    if (!w) {
        LOG_ERR("NULL wrapper user_data");
        return;
    }
    
    switch (evt->type) {
    case MQTT_EVT_CONNACK:
        if (evt->result == 0) {
            LOG_INF("MQTT WRAPPER CONNECTED SUCCESSFULLY!");
            k_mutex_lock(&w->state_mutex, K_FOREVER);
            w->connected = true;
            w->connecting = false;
            w->reconnect_interval_ms = CONFIG_MQTT_WRAPPER_RECONNECT_MIN_INTERVAL_MS;
            k_mutex_unlock(&w->state_mutex);
            
            /* Notify callback */
            if (w->conn_cb) {
                w->conn_cb(true, w->conn_cb_data);
            }
            
            /* Re-subscribe to active topics */
            for (int i = 0; i < CONFIG_MQTT_WRAPPER_MAX_SUBSCRIPTIONS; i++) {
                if (w->subs[i].active) {
                    struct mqtt_topic topic = {
                        .topic = {
                            .utf8 = (uint8_t *)w->subs[i].topic,
                            .size = strlen(w->subs[i].topic)
                        },
                        .qos = w->subs[i].qos
                    };
                    struct mqtt_subscription_list list = {
                        .list = &topic,
                        .list_count = 1,
                        .message_id = 1000 + i
                    };
                    mqtt_subscribe(client, &list);
                    LOG_INF("Re-subscribing to %s", w->subs[i].topic);
                }
            }
        } else {
            LOG_ERR("MQTT WRAPPER CONNECTION FAILED: result=%d", evt->result);
            k_mutex_lock(&w->state_mutex, K_FOREVER);
            w->connecting = false;
            k_mutex_unlock(&w->state_mutex);
        }
        break;
        
    case MQTT_EVT_DISCONNECT:
        LOG_WRN("MQTT disconnected");
        k_mutex_lock(&w->state_mutex, K_FOREVER);
        w->connected = false;
        k_mutex_unlock(&w->state_mutex);
        
        if (w->conn_cb) {
            w->conn_cb(false, w->conn_cb_data);
        }
        break;
        
    case MQTT_EVT_PUBLISH:
        {
            const struct mqtt_publish_param *pub = &evt->param.publish;
            static uint32_t last_message_id = 0;
            static int64_t last_publish_time = 0;
            int64_t now = k_uptime_get();
            
            LOG_DBG("MQTT_EVT_PUBLISH: msg_id=%u, topic=%.*s, len=%u, time_since_last=%lld",
                    pub->message_id,
                    pub->message.topic.topic.size, pub->message.topic.topic.utf8,
                    pub->message.payload.len,
                    now - last_publish_time);
                    
            if (pub->message_id == last_message_id && (now - last_publish_time) < 100) {
                LOG_WRN("Duplicate MQTT_EVT_PUBLISH detected! msg_id=%u", pub->message_id);
            }
            
            last_message_id = pub->message_id;
            last_publish_time = now;
            
            /* Send PUBACK immediately */
            if (pub->message.topic.qos == MQTT_QOS_1_AT_LEAST_ONCE) {
                struct mqtt_puback_param puback = {
                    .message_id = pub->message_id
                };
                mqtt_publish_qos1_ack(client, &puback);
            }
            
            /* Extract topic */
            char topic[128];
            size_t topic_len = MIN(pub->message.topic.topic.size, sizeof(topic) - 1);
            memcpy(topic, pub->message.topic.topic.utf8, topic_len);
            topic[topic_len] = '\0';
            
            /* Find handler */
            mqtt_msg_received_cb_t handler = NULL;
            void *user_data = NULL;
            
            for (int i = 0; i < CONFIG_MQTT_WRAPPER_MAX_SUBSCRIPTIONS; i++) {
                if (w->subs[i].active && 
                    strcmp(w->subs[i].topic, topic) == 0) {
                    handler = w->subs[i].handler;
                    user_data = w->subs[i].user_data;
                    break;
                }
            }
            
            if (handler && pub->message.payload.len > 0) {
                /* For messages up to 32KB, use in-memory buffer */
                if (pub->message.payload.len <= 32768) {
                    /* Allocate work item */
                    struct mqtt_msg_work *work = k_malloc(sizeof(*work));
                    if (work) {
                        strncpy(work->topic, topic, sizeof(work->topic) - 1);
                        work->callback = handler;
                        work->user_data = user_data;
                        
                        /* Allocate and read payload */
                        work->payload = k_malloc(pub->message.payload.len);
                        if (work->payload) {
                            /* Read complete payload - loop until all bytes received */
                            size_t total_read = 0;
                            while (total_read < pub->message.payload.len) {
                                int ret = mqtt_read_publish_payload_blocking(
                                    client, 
                                    work->payload + total_read, 
                                    pub->message.payload.len - total_read);
                                if (ret < 0) {
                                    LOG_ERR("Payload read error: %d", ret);
                                    k_free(work->payload);
                                    k_free(work);
                                    goto cleanup_pub;
                                }
                                total_read += ret;
                            }
                            
                            if (total_read == pub->message.payload.len) {
                                work->payload_len = pub->message.payload.len;
                                k_work_init(&work->work, process_message_work);
                                int submit_ret = k_work_submit_to_queue(&w->msg_work_queue, &work->work);
                                if (submit_ret < 0) {
                                    LOG_ERR("Failed to submit work: %d", submit_ret);
                                    k_free(work->payload);
                                    k_free(work);
                                }
                            }
                            
                        cleanup_pub:
                            (void)0; /* Label target */
                        } else {
                            LOG_ERR("Failed to allocate %zu bytes for payload", pub->message.payload.len);
                            k_free(work);
                            /* Consume payload */
                            uint8_t discard[512];
                            size_t remaining = pub->message.payload.len;
                            while (remaining > 0) {
                                size_t chunk = MIN(remaining, sizeof(discard));
                                int discard_ret = mqtt_read_publish_payload_blocking(client, discard, chunk);
                                if (discard_ret <= 0) break;
                                remaining -= discard_ret;
                            }
                        }
                    } else {
                        LOG_ERR("Failed to allocate work item");
                        /* Consume payload */
                        uint8_t discard[512];
                        size_t remaining = pub->message.payload.len;
                        while (remaining > 0) {
                            size_t chunk = MIN(remaining, sizeof(discard));
                            mqtt_read_publish_payload_blocking(client, discard, chunk);
                            remaining -= chunk;
                        }
                    }
                } else {
                    /* Large message - check if it's audio and handle accordingly */
                    if (strstr(topic, "rapidreach/audio/") != NULL) {
                        LOG_INF("Processing large audio message (%u bytes) via streaming to file", 
                                pub->message.payload.len);
                        
                        /* Mark that we're processing a large message */
                        w->large_msg_processed = true;
                        
                        /* First, read up to 512 bytes to get the JSON header */
                        uint8_t json_buf[512];
                        size_t total_len = pub->message.payload.len;
                        size_t json_read = MIN(total_len, sizeof(json_buf));
                        
                        LOG_DBG("Audio message: total_len=%zu, reading first %zu bytes", total_len, json_read);
                        int ret = mqtt_read_publish_payload_blocking(client, json_buf, json_read);
                        LOG_DBG("Initial read returned %d bytes (requested %zu)", ret, json_read);
                        if (ret < 0) {
                            LOG_ERR("Failed to read JSON header: %d", ret);
                            /* CRITICAL: Must consume the ENTIRE message to keep MQTT client in sync */
                            size_t remaining = total_len;  /* ALL bytes since none were read */
                            uint8_t discard[512];
                            while (remaining > 0) {
                                size_t chunk = MIN(remaining, sizeof(discard));
                                ret = mqtt_read_publish_payload_blocking(client, discard, chunk);
                                if (ret < 0) {
                                    LOG_ERR("Failed to discard remaining data: %d", ret);
                                    break;
                                } else if (ret == 0) {
                                    LOG_ERR("Unexpected 0 bytes when discarding");
                                    break;
                                }
                                remaining -= ret;  /* Use ACTUAL bytes read, not requested */
                            }
                        } else if (ret != json_read) {
                            LOG_WRN("Partial initial read: got %d bytes, expected %zu", ret, json_read);
                            json_read = ret;  /* Update to actual bytes read */
                        }
                        
                        if (ret >= 0) {
                                /* Skip 4-byte hex length header */
                                int json_start = 4;
                                
                                /* Find the end of JSON */
                                int json_end = -1;
                                int brace_count = 0;
                                bool in_string = false;
                                bool escape_next = false;
                                
                                for (int i = json_start; i < (int)json_read; i++) {
                                    if (!escape_next) {
                                        if (json_buf[i] == '"' && !in_string) {
                                            in_string = true;
                                        } else if (json_buf[i] == '"' && in_string) {
                                            in_string = false;
                                        } else if (!in_string) {
                                            if (json_buf[i] == '{') brace_count++;
                                            else if (json_buf[i] == '}') {
                                                brace_count--;
                                                if (brace_count == 0) {
                                                    json_end = i;
                                                    break;
                                                }
                                            }
                                        } else if (json_buf[i] == '\\') {
                                            escape_next = true;
                                            continue;
                                        }
                                    }
                                    escape_next = false;
                                }
                            
                            if (json_end >= 0 && json_end < (int)json_read - 1) {
                                /* Parse JSON header */
                                json_buf[json_end + 1] = '\0';
                                
                                
                                /* Parse JSON directly for file-based audio */
                                LOG_INF("JSON header (%d bytes): %.*s", json_end - json_start + 1, json_end - json_start + 1, &json_buf[json_start]);
                                
                                cJSON *root = cJSON_Parse((const char *)&json_buf[json_start]);
                                if (root) {
                                    cJSON *saveToFile = cJSON_GetObjectItem(root, "saveToFile");
                                    cJSON *filename = cJSON_GetObjectItem(root, "filename");
                                    
                                    LOG_INF("saveToFile field: %s, filename field: %s",
                                            saveToFile ? (cJSON_IsBool(saveToFile) ? (cJSON_IsTrue(saveToFile) ? "true" : "false") : "not bool") : "null",
                                            filename ? (cJSON_IsString(filename) ? filename->valuestring : "not string") : "null");
                                    
                                    /* For large messages, always save to file (temporary or permanent) */
                                    if (cJSON_IsString(filename) && filename->valuestring) {
                                        /* Determine if this is permanent or temporary storage */
                                        bool is_permanent = cJSON_IsBool(saveToFile) && cJSON_IsTrue(saveToFile);
                                        
                                        /* Open file for writing */
                                        struct fs_file_t file;
                                        fs_file_t_init(&file);
                                        
                                        char filepath[128];
                                        if (is_permanent) {
                                            snprintf(filepath, sizeof(filepath), "/lfs/%s", filename->valuestring);
                                        } else {
                                            /* Use temp prefix for temporary files */
                                            snprintf(filepath, sizeof(filepath), "/lfs/temp_%s", filename->valuestring);
                                        }
                                    
                                    /* For temporary files, try to clean up old ones first */
                                    if (!is_permanent) {
                                        struct fs_dir_t dir;
                                        fs_dir_t_init(&dir);
                                        if (fs_opendir(&dir, "/lfs") == 0) {
                                            struct fs_dirent entry;
                                            int old_files_deleted = 0;
                                            while (fs_readdir(&dir, &entry) == 0 && entry.name[0] != '\0') {
                                                if (strncmp(entry.name, "temp_", 5) == 0) {
                                                    char old_file[128];
                                                    snprintf(old_file, sizeof(old_file), "/lfs/%s", entry.name);
                                                    if (fs_unlink(old_file) == 0) {
                                                        old_files_deleted++;
                                                    }
                                                }
                                            }
                                            fs_closedir(&dir);
                                            if (old_files_deleted > 0) {
                                                LOG_INF("Cleaned up %d old temporary files", old_files_deleted);
                                            }
                                        }
                                    }
                                    
                                    /* Check filesystem status before writing */
                                    struct fs_statvfs stats;
                                    if (fs_statvfs("/lfs", &stats) == 0) {
                                        /* Use f_frsize (fragment size) not f_bsize for space calculation */
                                        unsigned long free_kb = (stats.f_frsize * stats.f_bfree) / 1024;
                                        unsigned long total_kb = (stats.f_frsize * stats.f_blocks) / 1024;
                                        LOG_INF("Filesystem: %lu KB free of %lu KB total", free_kb, total_kb);
                                        
                                        if (free_kb < 100) {
                                            LOG_WRN("Low filesystem space! Only %lu KB free", free_kb);
                                        }
                                    }
                                    
                                    ret = fs_open(&file, filepath, FS_O_CREATE | FS_O_WRITE);
                                    if (ret == 0) {
                                        LOG_INF("Writing audio to %s file: %s", 
                                                is_permanent ? "permanent" : "temporary", filepath);
                                        
                                        /* Calculate how much audio data is in the first chunk */
                                        /* IMPORTANT: json_end is the position of the null terminator in the JSON.
                                         * The actual header includes: 4-byte prefix + JSON + null terminator
                                         * So header_total_size = json_end + 1 (for null) */
                                        
                                        /* CRITICAL: Ensure json_end is valid before using it */
                                        if (json_end < 0) {
                                            LOG_ERR("CRITICAL ERROR: json_end is %d (negative!), cannot calculate header size", json_end);
                                            fs_close(&file);
                                            break;
                                        }
                                        
                                        size_t header_total_size = (size_t)(json_end + 1);  /* Total header size including 4-byte prefix */
                                        size_t audio_in_first = 0;
                                        
                                        LOG_INF("Header calculation: json_read=%zu, json_end=%d, header_total_size=%zu", 
                                                json_read, json_end, header_total_size);
                                        
                                        /* Safety check to prevent underflow */
                                        if (json_read >= header_total_size) {
                                            audio_in_first = json_read - header_total_size;
                                        } else {
                                            LOG_WRN("json_read (%zu) < header_total_size (%zu), no audio in first chunk", 
                                                    json_read, header_total_size);
                                        }
                                        
                                        if (audio_in_first > 0) {
                                            LOG_INF("Writing first chunk: %zu bytes (header was %zu bytes)", 
                                                    audio_in_first, header_total_size);
                                            ret = fs_write(&file, &json_buf[json_end + 1], audio_in_first);
                                            if (ret < 0) {
                                                LOG_ERR("Failed to write first chunk: %d", ret);
                                                fs_close(&file);
                                                /* Consume rest of payload */
                                                size_t remaining = (total_len > json_read) ? (total_len - json_read) : 0;
                                                uint8_t discard[512];
                                                while (remaining > 0) {
                                                    size_t chunk = MIN(remaining, sizeof(discard));
                                                    mqtt_read_publish_payload_blocking(client, discard, chunk);
                                                    remaining -= chunk;
                                                }
                                                break;
                                            }
                                        }
                                        
                                        /* Read and write remaining data in chunks */
                                        size_t bytes_written = audio_in_first;
                                        
                                        /* Calculate remaining bytes safely */
                                        size_t remaining = 0;
                                        if (total_len > json_read) {
                                            remaining = total_len - json_read;
                                        } else {
                                            LOG_ERR("ERROR: total_len (%zu) <= json_read (%zu), this should not happen!", 
                                                    total_len, json_read);
                                        }
                                        
                                        LOG_INF("MQTT tracking: total_len=%zu, json_read=%zu, remaining=%zu", 
                                                total_len, json_read, remaining);
                                        
                                        /* Use 512-byte chunks to reduce stack usage in deeply nested handler */
                                        uint8_t chunk_buf[512];  /* Reduced to prevent stack exhaustion */
                                        
                                        LOG_INF("Starting to write remaining %zu bytes (total payload: %zu, already read: %zu)", 
                                                remaining, total_len, json_read);
                                        LOG_INF("Audio data: first chunk %zu bytes (actual value: 0x%zx), expecting %zu more bytes", 
                                                audio_in_first, audio_in_first, remaining);
                                        
                                        /* Extra safety check */
                                        if (audio_in_first > total_len || remaining > total_len) {
                                            LOG_ERR("CRITICAL: Integer underflow detected! audio_in_first=%zu (0x%zx), remaining=%zu (0x%zx)", 
                                                    audio_in_first, audio_in_first, remaining, remaining);
                                            fs_close(&file);
                                            break;
                                        }
                                        
                                        /* Give system time to settle before intensive file operations */
                                        k_sleep(K_MSEC(50));
                                        LOG_DBG("Starting file write loop");
                                        
                                        int loop_count = 0;
                                        while (remaining > 0) {
                                            size_t chunk_size = MIN(remaining, sizeof(chunk_buf));
                                            
                                            /* Reduce logging frequency to prevent buffer overflow */
                                            if (loop_count % 10 == 0) {
                                                LOG_INF("Reading chunk %d: %zu bytes (remaining: %zu)", 
                                                        loop_count, chunk_size, remaining);
                                            }
                                            loop_count++;
                                            
                                            /* Add timeout detection */
                                            uint32_t start_time = k_uptime_get_32();
                                            ret = mqtt_read_publish_payload_blocking(client, chunk_buf, chunk_size);
                                            uint32_t elapsed = k_uptime_get_32() - start_time;
                                            
                                            if (elapsed > 1000) {
                                                LOG_WRN("MQTT read took %u ms", elapsed);
                                            }
                                            
                                            if (ret < 0) {
                                                LOG_ERR("Failed to read chunk: %d", ret);
                                                break;
                                            } else if (ret == 0) {
                                                LOG_ERR("Read returned 0 bytes, expected %zu", chunk_size);
                                                break;
                                            } else if (ret != chunk_size) {
                                                /* This is normal - we might get less than requested */
                                                LOG_DBG("Partial read: got %d bytes, requested %zu", ret, chunk_size);
                                            }
                                            
                                            /* Only log every 10th write to reduce log spam */
                                            if ((loop_count - 1) % 10 == 0) {
                                                LOG_DBG("Read %d bytes, writing to file...", ret);
                                            }
                                            
                                            /* Add timing to detect fs_write hangs */
                                            int64_t write_start = k_uptime_get();
                                            int write_ret = fs_write(&file, chunk_buf, ret);
                                            int64_t write_time = k_uptime_get() - write_start;
                                            
                                            if (write_ret < 0) {
                                                LOG_ERR("Failed to write chunk: %d", write_ret);
                                                break;
                                            }
                                            
                                            if (write_time > 100) {
                                                LOG_WRN("Slow fs_write: %lld ms for %d bytes", write_time, ret);
                                            }
                                            
                                            bytes_written += ret;  /* Track bytes consumed from MQTT */
                                            remaining -= ret;      /* Track bytes still to read from MQTT */
                                            
                                            /* Yield more frequently to prevent blocking */
                                            k_yield();
                                            
                                            /* More aggressive yielding when filesystem is slow */
                                            if (write_time > 50) {
                                                LOG_DBG("Yielding after slow write");
                                                k_sleep(K_MSEC(5));  /* Extra yield for slow writes */
                                            }
                                            
                                            /* Progress update every 4KB for more frequent breathing room */
                                            if (bytes_written % 4096 == 0) {
                                                int percent = (bytes_written * 100) / total_len;
                                                LOG_INF("Progress: %d%% (%zu/%zu bytes)", percent, bytes_written, total_len);
                                                k_sleep(K_MSEC(20));  /* Give filesystem more time for housekeeping */
                                            }
                                        }
                                        
                                        LOG_INF("Loop complete - bytes_written: %zu, remaining: %zu", 
                                                bytes_written, remaining);
                                        
                                        /* Verify we've consumed exactly the expected amount */
                                        size_t total_consumed = json_read + bytes_written;
                                        if (total_consumed != total_len) {
                                            LOG_ERR("CRITICAL: Data mismatch! Expected %zu bytes, consumed %zu (json:%zu + file:%zu)",
                                                    total_len, total_consumed, json_read, bytes_written);
                                            
                                            /* Try to consume any missing bytes to fix MQTT state */
                                            if (total_consumed < total_len) {
                                                size_t missing = total_len - total_consumed;
                                                LOG_ERR("Attempting to consume %zu missing bytes", missing);
                                                uint8_t discard[512];
                                                while (missing > 0) {
                                                    size_t chunk = MIN(missing, sizeof(discard));
                                                    int ret = mqtt_read_publish_payload_blocking(client, discard, chunk);
                                                    if (ret <= 0) {
                                                        LOG_ERR("Failed to consume missing bytes: %d", ret);
                                                        break;
                                                    }
                                                    missing -= ret;
                                                }
                                            }
                                        }
                                        
                                        if (remaining == 0) {
                                            LOG_INF("All data read successfully, closing file...");
                                        } else {
                                            LOG_ERR("Loop exited with %zu bytes remaining!", remaining);
                                            /* Consume any remaining data to keep MQTT in sync */
                                            uint8_t discard[512];
                                            while (remaining > 0) {
                                                size_t chunk = MIN(remaining, sizeof(discard));
                                                int ret = mqtt_read_publish_payload_blocking(client, discard, chunk);
                                                if (ret <= 0) break;
                                                remaining -= ret;
                                            }
                                        }
                                        
                                        /* Skip explicit sync - fs_close should handle it */
                                        /* fs_sync(&file); */
                                        LOG_INF("Closing file (with implicit sync)...");
                                        fs_close(&file);
                                        LOG_INF("Audio file written: %zu bytes", bytes_written);
                                        
                                        /* Queue for playback if handler exists */
                                        if (handler) {
                                            /* Create a minimal message with file reference */
                                            char msg_buf[256];
                                            /* Cannot use snprintf with \x00 null byte - build manually */
                                            msg_buf[0] = 0x00;  /* Audio type prefix byte 1 */
                                            msg_buf[1] = 0x01;  /* Audio type prefix byte 2 */
                                            int json_len = snprintf(&msg_buf[2], sizeof(msg_buf) - 2,
                                                "{\"file\":\"%s\"}", filepath);
                                            int msg_len = json_len + 2;  /* Add the 2 prefix bytes */
                                            
                                            LOG_DBG("Queueing playback message: json_len=%d, msg_len=%d, file=%s", 
                                                    json_len, msg_len, filepath);
                                            
                                            struct mqtt_msg_work *work = k_malloc(sizeof(*work));
                                            if (work) {
                                                strncpy(work->topic, topic, sizeof(work->topic) - 1);
                                                work->callback = handler;
                                                work->user_data = user_data;
                                                work->payload = k_malloc(msg_len);
                                                if (work->payload) {
                                                    memcpy(work->payload, msg_buf, msg_len);
                                                    work->payload_len = msg_len;
                                                    k_work_submit(&work->work);
                                                    LOG_INF("Audio file queued for playback: %s", filepath);
                                                } else {
                                                    LOG_ERR("Failed to allocate payload for playback message");
                                                    k_free(work);
                                                }
                                            } else {
                                                LOG_ERR("Failed to allocate work item for playback");
                                            }
                                        }
                                    } else {
                                        LOG_ERR("Failed to open file %s: %d", filepath, ret);
                                        /* Consume remaining data */
                                        size_t remaining = (total_len > json_read) ? (total_len - json_read) : 0;
                                        uint8_t discard[512];
                                        while (remaining > 0) {
                                            size_t chunk = MIN(remaining, sizeof(discard));
                                            mqtt_read_publish_payload_blocking(client, discard, chunk);
                                            remaining -= chunk;
                                        }
                                    }
                                    } else {
                                        /* Not for file or parse failed - consume data */
                                        LOG_WRN("Audio message not for file storage or missing filename");
                                        size_t remaining = (total_len > json_read) ? (total_len - json_read) : 0;
                                        uint8_t discard[512];
                                        while (remaining > 0) {
                                            size_t chunk = MIN(remaining, sizeof(discard));
                                            mqtt_read_publish_payload_blocking(client, discard, chunk);
                                            remaining -= chunk;
                                        }
                                    }
                                    cJSON_Delete(root);
                                } else {
                                    LOG_ERR("Failed to parse JSON header");
                                    /* Consume remaining data */
                                    size_t remaining = total_len - json_read;
                                    uint8_t discard[512];
                                    while (remaining > 0) {
                                        size_t chunk = MIN(remaining, sizeof(discard));
                                        mqtt_read_publish_payload_blocking(client, discard, chunk);
                                        remaining -= chunk;
                                    }
                                }
                            } else {
                                /* JSON not complete in first chunk - consume data */
                                LOG_ERR("JSON header not found in first 512 bytes");
                                size_t remaining = total_len - json_read;
                                uint8_t discard[512];
                                while (remaining > 0) {
                                    size_t chunk = MIN(remaining, sizeof(discard));
                                    mqtt_read_publish_payload_blocking(client, discard, chunk);
                                    remaining -= chunk;
                                }
                            }
                        }
                        
                        /* Log final status for debugging phantom message issues */
                        LOG_INF("Audio message processing complete: msg_id=%u, advertised_len=%u",
                                pub->message_id, pub->message.payload.len);
                    } else {
                        /* Non-audio large message - just consume it */
                        LOG_WRN("Large non-audio message (%u bytes) - discarding", 
                                pub->message.payload.len);
                        uint8_t discard[512];
                        size_t remaining = pub->message.payload.len;
                        while (remaining > 0) {
                            size_t chunk = MIN(remaining, sizeof(discard));
                            mqtt_read_publish_payload_blocking(client, discard, chunk);
                            remaining -= chunk;
                        }
                    }
                }
            }
            
            /* CRITICAL: Ensure we don't leave MQTT client in a bad state */
            LOG_INF("MQTT_EVT_PUBLISH complete: msg_id=%u, advertised_len=%u, topic=%.*s", 
                    pub->message_id, pub->message.payload.len,
                    pub->message.topic.topic.size, pub->message.topic.topic.utf8);
        }
        break;
        
    default:
        break;
    }
    
    LOG_DBG("mqtt_evt_handler returning for event type %d", evt->type);
}

/**
 * @brief Connect to MQTT broker
 */
int mqtt_client_connect(mqtt_handle_t handle, 
                       mqtt_conn_state_cb_t conn_cb,
                       void *user_data)
{
    struct mqtt_client_wrapper *w = (struct mqtt_client_wrapper *)handle;
    
    if (!w) {
        return -EINVAL;
    }
    
    w->conn_cb = conn_cb;
    w->conn_cb_data = user_data;
    
    /* Start protocol thread with PER-INSTANCE stack */
    if (!w->thread_running) {
        w->thread_running = true;
        w->should_stop = false;
        w->protocol_thread_id = k_thread_create(
            &w->protocol_thread,
            w->protocol_stack,
            CONFIG_MQTT_WRAPPER_PROTOCOL_STACK_SIZE,
            protocol_thread_func,
            w, NULL, NULL,
            CONFIG_MQTT_WRAPPER_PROTOCOL_THREAD_PRIORITY,
            0, K_NO_WAIT);
        k_thread_name_set(w->protocol_thread_id, "mqtt_protocol");
    }
    
    /* Initiate connection */
    k_mutex_lock(&w->state_mutex, K_FOREVER);
    w->connecting = true;
    k_mutex_unlock(&w->state_mutex);
    
    int ret = mqtt_connect(&w->client);
    if (ret < 0) {
        LOG_ERR("Failed to initiate connection: %d", ret);
        k_mutex_lock(&w->state_mutex, K_FOREVER);
        w->connecting = false;
        k_mutex_unlock(&w->state_mutex);
    } else {
        LOG_INF("MQTT wrapper connection initiated, waiting for CONNACK...");
    }
    
    return ret;
}

/**
 * @brief Disconnect from MQTT broker
 */
int mqtt_client_disconnect(mqtt_handle_t handle)
{
    struct mqtt_client_wrapper *w = (struct mqtt_client_wrapper *)handle;
    
    if (!w) {
        return -EINVAL;
    }
    
    /* Stop auto-reconnect */
    w->auto_reconnect = false;
    
    /* Stop thread */
    if (w->thread_running) {
        w->should_stop = true;
        k_thread_join(&w->protocol_thread, K_SECONDS(5));
        w->thread_running = false;
    }
    
    return 0;
}

/**
 * @brief Subscribe to topic
 */
int mqtt_client_subscribe(mqtt_handle_t handle,
                         const struct mqtt_subscription_config *sub)
{
    struct mqtt_client_wrapper *w = (struct mqtt_client_wrapper *)handle;
    
    if (!w || !sub || !sub->topic || !sub->callback) {
        return -EINVAL;
    }
    
    /* Find free slot */
    int slot = -1;
    for (int i = 0; i < CONFIG_MQTT_WRAPPER_MAX_SUBSCRIPTIONS; i++) {
        if (!w->subs[i].active) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        LOG_ERR("No free subscription slots");
        return -ENOMEM;
    }
    
    /* Store subscription */
    strncpy(w->subs[slot].topic, sub->topic, sizeof(w->subs[slot].topic) - 1);
    w->subs[slot].handler = sub->callback;
    w->subs[slot].user_data = sub->user_data;
    w->subs[slot].qos = sub->qos;
    w->subs[slot].active = true;
    
    /* Subscribe if connected */
    k_mutex_lock(&w->state_mutex, K_FOREVER);
    bool connected = w->connected;
    k_mutex_unlock(&w->state_mutex);
    
    if (connected) {
        struct mqtt_topic topic = {
            .topic = {
                .utf8 = (uint8_t *)sub->topic,
                .size = strlen(sub->topic)
            },
            .qos = sub->qos
        };
        struct mqtt_subscription_list list = {
            .list = &topic,
            .list_count = 1,
            .message_id = 2000 + slot
        };
        
        int ret = mqtt_subscribe(&w->client, &list);
        if (ret < 0) {
            LOG_ERR("Subscribe failed: %d", ret);
        } else {
            LOG_INF("Subscribed to %s", sub->topic);
        }
        return ret;
    }
    
    LOG_INF("Subscription to %s queued", sub->topic);
    return 0;
}

/**
 * @brief Publish message
 */
int mqtt_client_publish(mqtt_handle_t handle,
                       const char *topic,
                       const uint8_t *payload,
                       size_t payload_len,
                       uint8_t qos,
                       bool retain)
{
    struct mqtt_client_wrapper *w = (struct mqtt_client_wrapper *)handle;
    
    if (!w || !topic) {
        return -EINVAL;
    }
    
    k_mutex_lock(&w->state_mutex, K_FOREVER);
    bool connected = w->connected;
    k_mutex_unlock(&w->state_mutex);
    
    if (!connected) {
        LOG_WRN("Not connected");
        return -ENOTCONN;
    }
    
    /* Publish directly for now (could queue later) */
    static uint16_t msg_id = 1;
    struct mqtt_publish_param param = {
        .message = {
            .topic = {
                .topic = {
                    .utf8 = (uint8_t *)topic,
                    .size = strlen(topic)
                },
                .qos = qos
            },
            .payload = {
                .data = (uint8_t *)payload,
                .len = payload_len
            }
        },
        .message_id = msg_id++,
        .retain_flag = retain ? 1 : 0
    };
    
    int ret = mqtt_publish(&w->client, &param);
    if (ret < 0) {
        LOG_ERR("Publish failed: %d", ret);
    }
    
    return ret;
}

/**
 * @brief Check if connected
 */
bool mqtt_client_is_connected(mqtt_handle_t handle)
{
    struct mqtt_client_wrapper *w = (struct mqtt_client_wrapper *)handle;
    
    if (!w) {
        return false;
    }
    
    k_mutex_lock(&w->state_mutex, K_FOREVER);
    bool connected = w->connected;
    k_mutex_unlock(&w->state_mutex);
    
    return connected;
}

/**
 * @brief Set auto-reconnect
 */
void mqtt_client_set_auto_reconnect(mqtt_handle_t handle, bool enable)
{
    struct mqtt_client_wrapper *w = (struct mqtt_client_wrapper *)handle;
    
    if (w) {
        w->auto_reconnect = enable;
    }
}

/**
 * @brief Deinitialize client
 */
void mqtt_client_deinit(mqtt_handle_t handle)
{
    struct mqtt_client_wrapper *w = (struct mqtt_client_wrapper *)handle;
    
    if (!w) {
        return;
    }
    
    /* Disconnect first */
    mqtt_client_disconnect(handle);
    
    /* Stop work queue */
    k_work_queue_drain(&w->msg_work_queue, false);
    
    /* Free per-instance stacks */
    if (w->protocol_stack_mem) {
        k_free(w->protocol_stack_mem);
    }
    if (w->worker_stack_mem) {
        k_free(w->worker_stack_mem);
    }
    
    /* Free wrapper */
    k_free(w);
}

#endif /* CONFIG_RPR_MQTT_CLIENT_WRAPPER */
