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
#include <string.h>
#include <errno.h>

LOG_MODULE_REGISTER(mqtt_wrapper, CONFIG_RPR_MODULE_MQTT_LOG_LEVEL);

/* Stack definitions - must be outside struct */
K_THREAD_STACK_DEFINE(mqtt_protocol_stack, CONFIG_MQTT_WRAPPER_PROTOCOL_STACK_SIZE);
K_THREAD_STACK_DEFINE(mqtt_worker_stack, CONFIG_MQTT_WRAPPER_WORKER_STACK_SIZE);

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
    
    /* Initialize work queue */
    k_work_queue_init(&w->msg_work_queue);
    k_work_queue_start(&w->msg_work_queue,
                       mqtt_worker_stack,
                       K_THREAD_STACK_SIZEOF(mqtt_worker_stack),
                       CONFIG_MQTT_WRAPPER_WORKER_THREAD_PRIORITY,
                       NULL);
    k_thread_name_set(&w->msg_work_queue.thread, "mqtt_worker");
    
    /* Set defaults */
    w->auto_reconnect = true;
    w->reconnect_interval_ms = CONFIG_MQTT_WRAPPER_RECONNECT_MIN_INTERVAL_MS;
    
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
                    LOG_ERR("mqtt_input error: %d", ret);
                    k_mutex_lock(&w->state_mutex, K_FOREVER);
                    w->connected = false;
                    k_mutex_unlock(&w->state_mutex);
                    continue;
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
    struct mqtt_client_wrapper *w = (struct mqtt_client_wrapper *)client->user_data;
    
    LOG_INF("MQTT wrapper event handler called, type: %d", evt->type);
    
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
            LOG_INF("MQTT_EVT_PUBLISH received!");
            
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
            LOG_INF("Received message on topic: %s (len=%d)", topic, topic_len);
            
            /* Find handler */
            mqtt_msg_received_cb_t handler = NULL;
            void *user_data = NULL;
            
            LOG_INF("Looking for handler for topic: %s", topic);
            for (int i = 0; i < CONFIG_MQTT_WRAPPER_MAX_SUBSCRIPTIONS; i++) {
                if (w->subs[i].active) {
                    LOG_DBG("Checking subscription %d: %s (active=%d)", 
                            i, w->subs[i].topic, w->subs[i].active);
                    if (strcmp(w->subs[i].topic, topic) == 0) {
                        handler = w->subs[i].handler;
                        user_data = w->subs[i].user_data;
                        LOG_INF("Found handler for topic %s", topic);
                        break;
                    }
                }
            }
            
            if (!handler) {
                LOG_WRN("No handler found for topic: %s", topic);
            }
            
            if (handler && pub->message.payload.len > 0) {
                /* Allocate work item */
                struct mqtt_msg_work *work = k_malloc(sizeof(*work));
                if (work) {
                    strncpy(work->topic, topic, sizeof(work->topic) - 1);
                    work->callback = handler;
                    work->user_data = user_data;
                    
                    /* Allocate and read payload */
                    work->payload = k_malloc(pub->message.payload.len);
                    if (work->payload) {
                        int ret = mqtt_read_publish_payload_blocking(
                            client, work->payload, pub->message.payload.len);
                        if (ret >= 0) {
                            work->payload_len = pub->message.payload.len;
                            k_work_init(&work->work, process_message_work);
                            k_work_submit_to_queue(&w->msg_work_queue, &work->work);
                            LOG_DBG("Queued message from %s", topic);
                        } else {
                            LOG_ERR("Failed to read payload: %d", ret);
                            k_free(work->payload);
                            k_free(work);
                        }
                    } else {
                        LOG_ERR("Failed to allocate payload");
                        k_free(work);
                    }
                } else {
                    LOG_ERR("Failed to allocate work item");
                }
            }
        }
        break;
        
    default:
        break;
    }
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
    
    /* Start protocol thread */
    if (!w->thread_running) {
        w->thread_running = true;
        w->should_stop = false;
        w->protocol_thread_id = k_thread_create(
            &w->protocol_thread,
            mqtt_protocol_stack,
            K_THREAD_STACK_SIZEOF(mqtt_protocol_stack),
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
    
    /* Free wrapper */
    k_free(w);
}

#endif /* CONFIG_RPR_MQTT_CLIENT_WRAPPER */
