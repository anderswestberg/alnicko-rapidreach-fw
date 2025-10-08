/**
 * @file mqtt_audio_handler_v2.c
 * @brief MQTT audio handler using the new thread-safe wrapper
 * 
 * This is a test implementation to validate the new MQTT wrapper
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/fs/fs.h>
#include <string.h>
#include <stdio.h>
#include "mqtt_client_wrapper.h"
#include "mqtt_message_parser.h"
#include "mqtt_audio_queue.h"
#include "../dev_info/dev_info.h"
#include "../file_manager/file_manager.h"

LOG_MODULE_REGISTER(mqtt_audio_v2, CONFIG_RPR_MODULE_MQTT_LOG_LEVEL);

/* MQTT client handle */
static mqtt_handle_t mqtt_client;

/* Audio alert topic buffer */
static char audio_alert_topic[128];

/* Connection state */
static bool mqtt_connected = false;

/**
 * @brief Connection state callback
 */
static void mqtt_conn_callback(bool connected, void *user_data)
{
    mqtt_connected = connected;
    
    if (connected) {
        LOG_INF("MQTT connected - audio handler ready");
        
        /* Send immediate status */
        const char *status = "{\"audio_handler\":\"ready\",\"version\":\"2.0\"}";
        mqtt_client_publish(mqtt_client, "device/audio/status",
                           (uint8_t *)status, strlen(status),
                           MQTT_QOS_1_AT_LEAST_ONCE, false);
    } else {
        LOG_WRN("MQTT disconnected - audio handler paused");
    }
}

/**
 * @brief Process audio alert message
 * 
 * This runs in the worker thread - safe for file I/O
 */
static void mqtt_audio_alert_handler_v2(const char *topic,
                                       const uint8_t *payload,
                                       size_t payload_len,
                                       void *user_data)
{
    mqtt_parsed_message_t parsed_msg;
    int ret;
    
    /* Check for NULL payload */
    if (!payload || payload_len < 4) {
        LOG_ERR("Invalid payload (len=%zu)", payload_len);
        return;
    }
    
    printk("HANDLER: payload_len=%zu\n", payload_len);
    
    /* Check if this is a test ping (no opusDataSize field) */
    if (payload_len < 100) {
        LOG_INF("Test ping OK");
        return;
    }
    
    /* Parse MQTT message ([4-byte hex len][JSON][Opus data]) */
    ret = mqtt_parse_message(payload, payload_len, &parsed_msg);
    if (ret < 0) {
        return;
    }
    
    /* Generate temp filename for audio */
    char temp_filename[64];
    snprintf(temp_filename, sizeof(temp_filename), "/lfs/mqtt_audio_%04x_%03d.opus",
             (uint16_t)(k_uptime_get_32() & 0xFFFF), k_cycle_get_32() & 0xFFF);
    
    /* Check if we have audio data in the message */
    printk("OPUS: size=%u ptr=%p len=%zu\n", 
           parsed_msg.metadata.opus_data_size,
           parsed_msg.opus_data,
           parsed_msg.opus_data_len);
    
    /* Dump first 16 bytes to verify OGG header */
    if (parsed_msg.opus_data && parsed_msg.opus_data_len >= 16) {
        printk("OPUS_HDR: ");
        for (int i = 0; i < 16; i++) {
            printk("%02x", parsed_msg.opus_data[i]);
        }
        printk("\n");
    }
    
    if (parsed_msg.metadata.opus_data_size > 0) {
        if (parsed_msg.opus_data && parsed_msg.opus_data_len > 0) {
            /* Save audio data to file */
            printk("WRITE: %s (%zu bytes)\n", temp_filename, parsed_msg.opus_data_len);
            ret = file_manager_write(temp_filename, parsed_msg.opus_data, 
                                   parsed_msg.opus_data_len);
            printk("WRITE: ret=%d\n", ret);
            if (ret < 0) {
                return;
            }
            
            /* Verify file was written */
            int exists = file_manager_exists(temp_filename);
            printk("FILE_EXISTS: %d\n", exists);
            
            /* Also copy to /lfs/audio/mqtt_test.opus for CLI testing */
            if (exists > 0) {
                struct fs_file_t src, dst;
                uint8_t buf[512];
                fs_file_t_init(&src);
                fs_file_t_init(&dst);
                
                if (fs_open(&src, temp_filename, FS_O_READ) == 0) {
                    if (fs_open(&dst, "/lfs/audio/mqtt_test.opus", FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC) == 0) {
                        ssize_t bytes;
                        while ((bytes = fs_read(&src, buf, sizeof(buf))) > 0) {
                            fs_write(&dst, buf, bytes);
                        }
                        fs_close(&dst);
                        printk("COPIED to /lfs/audio/mqtt_test.opus\n");
                    }
                    fs_close(&src);
                }
            }
        }
    }
    
    /* Queue for playback */
    printk("Q1\n");
    struct audio_queue_item item = {
        .volume = parsed_msg.metadata.volume,
        .priority = parsed_msg.metadata.priority,
        .play_count = parsed_msg.metadata.play_count,
        .interrupt_current = parsed_msg.metadata.interrupt_current
    };
    
    printk("Q2\n");
    
    /* Use the temp file we just created */
    strncpy(item.filename, temp_filename, sizeof(item.filename) - 1);
    
    printk("Q3\n");
    
    ret = audio_queue_add(&item);
    
    printk("Q4\n");
    
    if (ret != 0) {
        file_manager_delete(temp_filename);
    }
    
    printk("Q5\n");
}

/* Header declaration for mqtt_audio_handler_v2.h functionality */
int mqtt_audio_handler_v2_init(void);
void mqtt_audio_handler_v2_test(void);

/**
 * @brief Initialize MQTT audio handler v2
 */
int mqtt_audio_handler_v2_init(void)
{
    int ret;
    size_t device_id_len;
    const char *device_id = dev_info_get_device_id_str(&device_id_len);
    if (!device_id || device_id_len == 0) {
        LOG_ERR("Failed to get device ID");
        return -EINVAL;
    }
    
    /* Build topic */
    snprintf(audio_alert_topic, sizeof(audio_alert_topic),
             "rapidreach/audio/%s", device_id);
    
    LOG_INF("Initializing MQTT audio handler v2");
    printk("INIT1\n");
    LOG_INF("Audio alert topic: %s", audio_alert_topic);
    printk("INIT2\n");
    
    /* Initialize audio queue and playback thread */
    printk("INIT3\n");
    ret = audio_queue_init();
    printk("INIT4\n");
    if (ret < 0) {
        LOG_ERR("Failed to initialize audio queue: %d", ret);
        return ret;
    }
    
    /* Configure MQTT client with unique ID for wrapper test */
    char wrapper_client_id[64];
    snprintf(wrapper_client_id, sizeof(wrapper_client_id), "%s-wrapper", device_id);
    
    struct mqtt_client_config config = {
        .broker_hostname = CONFIG_RPR_MQTT_BROKER_HOST,
        .broker_port = CONFIG_RPR_MQTT_BROKER_PORT,
        .client_id = wrapper_client_id,
        .keepalive_interval = CONFIG_RPR_MQTT_KEEPALIVE_SEC,
        .clean_session = true
    };
    
    /* Create MQTT client */
    mqtt_client = mqtt_wrapper_create(&config);
    if (!mqtt_client) {
        LOG_ERR("Failed to create MQTT client");
        return -ENOMEM;
    }
    
    /* Connect */
    ret = mqtt_client_connect(mqtt_client, mqtt_conn_callback, NULL);
    if (ret < 0) {
        LOG_ERR("Failed to initiate connection: %d", ret);
        return ret;
    }
    
    /* Subscribe to audio alert topic */
    struct mqtt_subscription_config sub = {
        .topic = audio_alert_topic,
        .qos = MQTT_QOS_1_AT_LEAST_ONCE,
        .callback = mqtt_audio_alert_handler_v2,
        .user_data = NULL
    };
    
    ret = mqtt_client_subscribe(mqtt_client, &sub);
    if (ret < 0) {
        LOG_ERR("Failed to subscribe to audio topic: %d", ret);
        /* Will auto-subscribe on connect */
    }
    
    /* Subscribe to test topic */
    struct mqtt_subscription_config test_sub = {
        .topic = "test/mqtt/wrapper",
        .qos = MQTT_QOS_1_AT_LEAST_ONCE,
        .callback = mqtt_audio_alert_handler_v2,
        .user_data = NULL
    };
    
    mqtt_client_subscribe(mqtt_client, &test_sub);
    
    LOG_INF("MQTT audio handler v2 initialized");
    return 0;
}

/**
 * @brief Test function to verify wrapper functionality
 */
void mqtt_audio_handler_v2_test(void)
{
    static int test_counter = 0;
    char payload[128];
    
    if (!mqtt_connected) {
        LOG_WRN("Not connected, skipping test");
        return;
    }
    
    /* Send test message */
    snprintf(payload, sizeof(payload),
             "{\"test\":%d,\"timestamp\":%lld,\"handler\":\"v2\"}",
             ++test_counter, k_uptime_get());
    
    int ret = mqtt_client_publish(mqtt_client,
                                 "device/audio/test",
                                 (uint8_t *)payload, strlen(payload),
                                 MQTT_QOS_1_AT_LEAST_ONCE,
                                 false);
    
    if (ret < 0) {
        LOG_ERR("Test publish failed: %d", ret);
    } else {
        LOG_INF("Test message %d published", test_counter);
    }
}
