/**
 * @file mqtt_audio_handler.c
 * @brief MQTT audio message handler implementation
 */

#include "mqtt_audio_handler.h"
#include "mqtt_message_parser.h"
#include "mqtt_module.h"
#include "../audio_player/audio_player.h"
#include "../file_manager/file_manager.h"
#include "../dev_info/dev_info.h"
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/fs/fs.h>
#include <stdio.h>
#include <string.h>

LOG_MODULE_REGISTER(mqtt_audio_handler, CONFIG_RPR_MODULE_MQTT_LOG_LEVEL);

#define AUDIO_ALERT_TOPIC_PREFIX "rapidreach/audio/"
#define TEMP_AUDIO_FILE_PATH "/lfs/temp_audio.opus"
#define MAX_TOPIC_LEN 128
#define MAX_AUDIO_VOLUME_PERCENT 75  /* Hardcoded maximum volume limit */

static char audio_alert_topic[MAX_TOPIC_LEN];

/**
 * @brief Priority queue for audio messages
 */
struct audio_msg_queue_item {
    mqtt_message_metadata_t metadata;
    uint8_t *opus_data;
    size_t opus_data_len;
};

#define AUDIO_QUEUE_SIZE 5
K_MSGQ_DEFINE(audio_msg_queue, sizeof(struct audio_msg_queue_item), AUDIO_QUEUE_SIZE, 4);

/* Track audio system initialization status */
static bool audio_initialized = false;

void mqtt_audio_alert_handler(const char *topic, const uint8_t *payload, size_t payload_len)
{
    mqtt_parsed_message_t parsed_msg;
    int ret;
    const char *temp_audio_file = NULL;
    bool audio_in_file = false;
    
    LOG_INF("Processing audio alert on topic '%s' (%zu bytes)", topic, payload_len);
    
    /* Log the JSON payload for debugging */
    if (payload_len < 512) {
        LOG_INF("JSON payload: %.*s", (int)payload_len, (const char *)payload);
    } else {
        LOG_INF("JSON payload (first 512 chars): %.512s...", (const char *)payload);
    }
    
    /* Parse the MQTT message - use JSON-only parser for chunked messages */
    ret = mqtt_parse_json_only((const char *)payload, payload_len, &parsed_msg);
    if (ret != MQTT_PARSER_SUCCESS) {
        LOG_ERR("Failed to parse MQTT audio message: %s", 
                mqtt_parser_error_string(ret));
        return;
    }
    
    /* Check if audio data was parsed from payload or is in file */
    if (parsed_msg.opus_data_len == 0 && parsed_msg.metadata.opus_data_size > 0) {
        /* Audio data is in file (MQTT module saved it) */
        audio_in_file = true;
        /* Prefer the last file the MQTT module wrote, fallback to known names */
        extern char g_mqtt_last_temp_file[];
        if (g_mqtt_last_temp_file && g_mqtt_last_temp_file[0] != '\0' &&
            file_manager_exists(g_mqtt_last_temp_file) == 1) {
            temp_audio_file = g_mqtt_last_temp_file;
        } else if (file_manager_exists("/lfs/mqtt_audio_1.opus") == 1) {
            temp_audio_file = "/lfs/mqtt_audio_1.opus";
        } else {
            temp_audio_file = "/lfs/mqtt_audio_0.opus";
        }
        LOG_INF("Audio data stored in file %s, JSON metadata parsed successfully", temp_audio_file);
    }
    
    LOG_INF("Parsed audio: priority=%d, size=%u, play_count=%d, volume=%d%%, interrupt=%d, save=%d, file='%s'",
            parsed_msg.metadata.priority,
            parsed_msg.metadata.opus_data_size,
            parsed_msg.metadata.play_count,
            parsed_msg.metadata.volume,
            parsed_msg.metadata.interrupt_current,
            parsed_msg.metadata.save_to_file,
            parsed_msg.metadata.save_to_file ? parsed_msg.metadata.filename : "none");
    
    /* Apply hardcoded maximum volume limit */
    if (parsed_msg.metadata.volume > MAX_AUDIO_VOLUME_PERCENT) {
        LOG_WRN("Volume %d%% exceeds max limit of %d%%, capping to %d%%", 
                parsed_msg.metadata.volume, MAX_AUDIO_VOLUME_PERCENT, MAX_AUDIO_VOLUME_PERCENT);
        parsed_msg.metadata.volume = MAX_AUDIO_VOLUME_PERCENT;
    }
    
    /* Handle based on priority and current playback state */
    if (parsed_msg.metadata.interrupt_current) {
        /* Stop current playback if needed */
        LOG_INF("Interrupting current playback for priority %d message",
                parsed_msg.metadata.priority);
        if (audio_initialized) {
            audio_player_stop();
            k_msleep(100); /* Allow stop to complete */
        }
    } else {
        /* Wait for current playback to finish - but only if audio system is initialized */
        if (!audio_initialized) {
            /* Try to initialize audio if not done yet */
            player_status_t status = codec_enable();
            if (status == PLAYER_OK) {
                audio_initialized = true;
                LOG_INF("Audio system initialized on first use");
            } else {
                LOG_WRN("Failed to initialize audio system: %d", status);
            }
        }
        
        if (audio_initialized) {
            int wait_count = 0;
            while (get_playing_status() && wait_count < 300) { /* Max wait 30 seconds */
                LOG_DBG("Waiting for current playback to finish... (%d/300)", wait_count);
                k_msleep(100);
                wait_count++;
            }
            
            if (wait_count >= 300) {
                LOG_WRN("Timeout waiting for playback to finish, proceeding anyway");
            } else if (wait_count > 0) {
                LOG_INF("Waited %d ms for previous playback to finish", wait_count * 100);
            }
        }
    }
    
    /* Handle file save or direct playback */
    if (parsed_msg.metadata.save_to_file && strlen(parsed_msg.metadata.filename) > 0) {
        /* Create audio directory if it doesn't exist */
        struct fs_dirent entry;
        int dir_ret = fs_stat("/lfs/audio", &entry);
        if (dir_ret < 0) {
            /* Directory doesn't exist, create it */
            dir_ret = fs_mkdir("/lfs/audio");
            if (dir_ret < 0 && dir_ret != -EEXIST) {
                LOG_WRN("Failed to create audio directory: %d", dir_ret);
            } else if (dir_ret == 0) {
                LOG_INF("Created audio directory /lfs/audio");
            }
        }
        
        /* Save Opus data to specified file in audio directory */
        char filepath[128];
        snprintf(filepath, sizeof(filepath), "/lfs/audio/%s", parsed_msg.metadata.filename);
        
        if (audio_in_file) {
            /* Audio is already in temp file, copy it to the destination */
            LOG_INF("Copying audio from temp file %s to %s", temp_audio_file, filepath);
            
            /* Read from temp file and write to destination */
            struct fs_file_t src_file, dst_file;
            fs_file_t_init(&src_file);
            fs_file_t_init(&dst_file);
            
            ret = fs_open(&src_file, temp_audio_file, FS_O_READ);
            if (ret < 0) {
                LOG_ERR("Failed to open temp file %s: %d", temp_audio_file, ret);
                return;
            }
            
            ret = fs_open(&dst_file, filepath, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
            if (ret < 0) {
                LOG_ERR("Failed to create destination file %s: %d", filepath, ret);
                fs_close(&src_file);
                return;
            }
            
            /* Copy file in chunks */
            uint8_t buffer[256];
            ssize_t bytes_read;
            while ((bytes_read = fs_read(&src_file, buffer, sizeof(buffer))) > 0) {
                ret = fs_write(&dst_file, buffer, bytes_read);
                if (ret < bytes_read) {
                    LOG_ERR("Failed to write to destination file: %d", ret);
                    fs_close(&src_file);
                    fs_close(&dst_file);
                    return;
                }
            }
            
            fs_close(&src_file);
            fs_close(&dst_file);
            
            if (bytes_read < 0) {
                LOG_ERR("Error reading from temp file: %zd", bytes_read);
                return;
            }
            
            LOG_INF("Successfully saved audio to '%s'", filepath);
            
            /* Delete the temp file after successful copy */
            file_manager_delete(temp_audio_file);
        } else {
            /* Audio data is in memory, write directly */
            ret = file_manager_write(filepath, parsed_msg.opus_data, parsed_msg.opus_data_len);
            if (ret < 0) {
                LOG_ERR("Failed to save audio file '%s': %d", filepath, ret);
                return;
            }
            LOG_INF("Saved audio to '%s'", filepath);
        }
        
        /* Set volume before playback */
        if (parsed_msg.metadata.volume <= 100) {
#if DT_NODE_HAS_COMPAT(DT_NODELABEL(audio_codec), ti_tas6422dac)
            /* Map 0-100% to TAS6422DAC range (-200 to +48 in 0.5 dB steps) */
            int codec_volume;
            if (parsed_msg.metadata.volume == 0) {
                codec_volume = -200;  /* Mute */
            } else if (parsed_msg.metadata.volume <= 80) {
                /* 0-80% maps to -200 to 0 (mute to 0 dB) */
                codec_volume = -200 + (parsed_msg.metadata.volume * 200 / 80);
            } else {
                /* 80-100% maps to 0 to +48 (0 dB to +24 dB) */
                codec_volume = (parsed_msg.metadata.volume - 80) * 48 / 20;
            }
            LOG_INF("Audio volume request: %d%%, mapped codec value: %d", 
                    parsed_msg.metadata.volume, codec_volume);
            if (codec_volume < MINIMUM_CODEC_VOLUME) {
                codec_volume = MINIMUM_CODEC_VOLUME;
            }
            if (codec_volume > MAXIMUM_CODEC_VOLUME) {
                codec_volume = MAXIMUM_CODEC_VOLUME;
            }
            ret = audio_player_set_volume(codec_volume);
            if (ret != PLAYER_OK) {
                LOG_ERR("audio_player_set_volume failed: %d", ret);
            }
#else
            /* For other codecs, pass volume directly */
            LOG_INF("Audio volume request: %d%% (direct)", parsed_msg.metadata.volume);
            ret = audio_player_set_volume(parsed_msg.metadata.volume);
            if (ret != PLAYER_OK) {
                LOG_ERR("audio_player_set_volume failed: %d", ret);
            }
#endif
        }
        
        /* Ensure audio system is initialized before playback */
        if (!audio_initialized) {
            player_status_t status = codec_enable();
            if (status == PLAYER_OK) {
                audio_initialized = true;
                LOG_INF("Audio system initialized before playback");
            } else {
                LOG_ERR("Failed to initialize audio system: %d", status);
                return;
            }
        }
        
        /* Start playback from saved file (non-blocking) */
        ret = audio_player_start(filepath);
        if (ret != PLAYER_OK) {
            LOG_ERR("Failed to start audio playback: %d", ret);
        } else {
            LOG_INF("Audio playback started from saved file '%s'", filepath);
            /* TODO: Handle play_count > 1 and infinite playback in audio player */
            if (parsed_msg.metadata.play_count != 1) {
                LOG_WRN("play_count=%d not yet supported, playing once", 
                        parsed_msg.metadata.play_count);
            }
        }
    } else {
            /* Use temporary file for immediate playback */
            const char *play_file = audio_in_file ? temp_audio_file : TEMP_AUDIO_FILE_PATH;
            
            if (!audio_in_file) {
                /* Save to temporary file */
                ret = file_manager_write(TEMP_AUDIO_FILE_PATH, parsed_msg.opus_data, parsed_msg.opus_data_len);
                if (ret < 0) {
                    LOG_ERR("Failed to save temporary audio file: %d", ret);
                    return;
                }
            }
            
            /* Set volume if supported */
            if (parsed_msg.metadata.volume <= 100) {
#if DT_NODE_HAS_COMPAT(DT_NODELABEL(audio_codec), ti_tas6422dac)
                /* Map 0-100% to TAS6422DAC range (-200 to +48 in 0.5 dB steps)
                 * Web volume:  Codec value:  dB:
                 * 0%          -200          -100 dB (mute)
                 * 15%         -120          -60 dB
                 * 50%         -50           -25 dB  
                 * 80%         0             0 dB
                 * 100%        48            +24 dB (max)
                 */
                int codec_volume;
                if (parsed_msg.metadata.volume == 0) {
                    codec_volume = -200;  /* Mute */
                } else if (parsed_msg.metadata.volume <= 80) {
                    /* 0-80% maps to -200 to 0 (mute to 0 dB) */
                    codec_volume = -200 + (parsed_msg.metadata.volume * 200 / 80);
                } else {
                    /* 80-100% maps to 0 to +48 (0 dB to +24 dB) */
                    codec_volume = (parsed_msg.metadata.volume - 80) * 48 / 20;
                }
                LOG_INF("Audio volume request: %d%%, mapped codec value: %d", 
                        parsed_msg.metadata.volume, codec_volume);
                if (codec_volume < MINIMUM_CODEC_VOLUME) {
                    codec_volume = MINIMUM_CODEC_VOLUME;
                }
                if (codec_volume > MAXIMUM_CODEC_VOLUME) {
                    codec_volume = MAXIMUM_CODEC_VOLUME;
                }
                ret = audio_player_set_volume(codec_volume);
                if (ret != PLAYER_OK) {
                    LOG_ERR("audio_player_set_volume failed: %d", ret);
                }
#else
                /* For other codecs, pass volume directly */
                LOG_INF("Audio volume request: %d%% (direct)", parsed_msg.metadata.volume);
                ret = audio_player_set_volume(parsed_msg.metadata.volume);
                if (ret != PLAYER_OK) {
                    LOG_ERR("audio_player_set_volume failed: %d", ret);
                }
#endif
                if (ret != PLAYER_OK) {
                    LOG_WRN("Failed to set volume: %d", ret);
                }
            }
            
            /* Ensure audio system is initialized before playback */
            if (!audio_initialized) {
                player_status_t status = codec_enable();
                if (status == PLAYER_OK) {
                    audio_initialized = true;
                    LOG_INF("Audio system initialized before playback");
                } else {
                    LOG_ERR("Failed to initialize audio system: %d", status);
                    if (!audio_in_file) {
                        file_manager_delete(TEMP_AUDIO_FILE_PATH);
                    }
                    return;
                }
            }
            
            /* Start playback (non-blocking) */
            ret = audio_player_start(play_file);
            if (ret != PLAYER_OK) {
                LOG_ERR("Failed to start audio playback: %d", ret);
            } else {
                LOG_INF("Audio playback started from '%s'", play_file);
                /* TODO: Handle play_count > 1 and infinite playback in audio player */
                if (parsed_msg.metadata.play_count != 1) {
                    LOG_WRN("play_count=%d not yet supported, playing once", 
                            parsed_msg.metadata.play_count);
                }
            }
            
            /* TODO: Delete temporary file after playback completes.
             * For now, we'll leave it and it will be overwritten by next audio.
             * Proper solution would be to have audio player notify when done. */
            LOG_DBG("Temporary audio file kept for playback: %s", play_file);
        }

}

const char *mqtt_get_audio_alert_topic(void)
{
    /* Build topic once using device ID */
    if (audio_alert_topic[0] == '\0') {
        size_t len;
        const char *device_id = dev_info_get_device_id_str(&len);
        if (device_id) {
            snprintf(audio_alert_topic, sizeof(audio_alert_topic),
                     "%s%s", AUDIO_ALERT_TOPIC_PREFIX, device_id);
        } else {
            /* Fallback to wildcard */
            strncpy(audio_alert_topic, "rapidreach/audio/+", sizeof(audio_alert_topic));
        }
    }
    
    return audio_alert_topic;
}

int mqtt_audio_handler_init(void)
{
    const char *topic = mqtt_get_audio_alert_topic();
    mqtt_status_t ret;
    
    LOG_INF("Subscribing to audio alert topic: %s", topic);
    
    /* Subscribe to audio alert topic */
    ret = mqtt_module_subscribe(topic, 1, mqtt_audio_alert_handler);
    if (ret != MQTT_SUCCESS) {
        LOG_ERR("Failed to subscribe to audio alert topic: %d", ret);
        /* Continue anyway - the subscription will be retried on reconnect */
    } else {
        LOG_INF("Successfully subscribed to audio alert topic");
    }
    
    /* Also subscribe to broadcast topic */
    ret = mqtt_module_subscribe("rapidreach/audio/broadcast", 1, mqtt_audio_alert_handler);
    if (ret != MQTT_SUCCESS) {
        LOG_WRN("Failed to subscribe to broadcast audio topic: %d", ret);
    } else {
        LOG_INF("Successfully subscribed to broadcast audio topic");
    }
    
    LOG_INF("MQTT audio handler initialized");
    return 0;  /* Return success even if subscription fails - it will retry */
}
