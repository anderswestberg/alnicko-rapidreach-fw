/**
 * @file mqtt_audio_queue.c
 * @brief Audio playback queue implementation
 */

#include "mqtt_audio_queue.h"
#include "../file_manager/file_manager.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>

/* Audio player function declarations */
typedef enum {
    PLAYER_OK = 0,
    PLAYER_ERROR = -1
} player_status_t;

extern player_status_t codec_enable(void);
extern player_status_t audio_player_start(char *filename);
extern player_status_t audio_player_stop(void);
extern player_status_t audio_player_set_volume(int volume);
extern bool get_playing_status(void);

LOG_MODULE_REGISTER(audio_queue, CONFIG_RPR_MODULE_MQTT_LOG_LEVEL);

/* Audio queue configuration */
#define AUDIO_QUEUE_SIZE 10
#define AUDIO_THREAD_STACK_SIZE 2048
#define AUDIO_THREAD_PRIORITY 5

/* Message queue for audio items */
K_MSGQ_DEFINE(audio_msgq, sizeof(struct audio_queue_item), AUDIO_QUEUE_SIZE, 4);

/* Audio playback thread */
K_THREAD_STACK_DEFINE(audio_thread_stack, AUDIO_THREAD_STACK_SIZE);
static struct k_thread audio_thread;
static k_tid_t audio_thread_id = NULL;

/* State tracking */
static bool audio_initialized = false;
static bool currently_playing = false;
static bool should_stop = false;
static struct k_mutex state_mutex;

/**
 * @brief Audio playback thread function
 */
static void audio_playback_thread(void *p1, void *p2, void *p3)
{
    struct audio_queue_item item;
    int ret;
    
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);
    
    LOG_INF("Audio playback thread started");
    
    while (1) {
        /* Wait for audio item from queue */
        ret = k_msgq_get(&audio_msgq, &item, K_FOREVER);
        if (ret != 0) {
            LOG_ERR("Failed to get item from queue: %d", ret);
            continue;
        }
        
        LOG_INF("Processing audio from queue: %s (vol=%d%%, pri=%d, count=%d, interrupt=%d)",
                item.filename, item.volume, item.priority, item.play_count, item.interrupt_current);
        
        /* Check if file exists */
        if (!file_manager_exists(item.filename)) {
            LOG_ERR("Audio file not found: %s", item.filename);
            /* Clean up the file reference */
            if (strstr(item.filename, "/lfs/mqtt_audio_") != NULL) {
                file_manager_delete(item.filename);
            }
            continue;
        }
        
        /* Initialize audio system if needed */
        if (!audio_initialized) {
            LOG_INF("Initializing audio system...");
            player_status_t status = codec_enable();
            if (status == PLAYER_OK) {
                audio_initialized = true;
                LOG_INF("Audio system initialized successfully");
                /* Give codec time to fully initialize */
                k_msleep(500);
            } else {
                LOG_ERR("Failed to initialize audio system: %d", status);
                /* Clean up the temp file */
                if (strstr(item.filename, "/lfs/mqtt_audio_") != NULL) {
                    file_manager_delete(item.filename);
                }
                continue;
            }
        }
        
        /* Handle interrupt flag */
        if (item.interrupt_current && get_playing_status()) {
            LOG_INF("Interrupting current playback");
            audio_player_stop();
            k_msleep(100); /* Allow stop to complete */
        }
        
        /* Wait for any current playback to finish (if not interrupting) */
        if (!item.interrupt_current) {
            int wait_count = 0;
            while (get_playing_status() && wait_count < 300) { /* Max 30 seconds */
                k_msleep(100);
                wait_count++;
                
                /* Check if we should stop waiting */
                k_mutex_lock(&state_mutex, K_FOREVER);
                if (should_stop) {
                    should_stop = false;
                    k_mutex_unlock(&state_mutex);
                    break;
                }
                k_mutex_unlock(&state_mutex);
            }
            
            if (wait_count >= 300) {
                LOG_WRN("Timeout waiting for playback to finish");
            }
        }
        
        /* Play the audio file */
        for (uint8_t i = 0; i < item.play_count; i++) {
            if (i > 0) {
                k_msleep(500); /* Brief pause between repeats */
            }
            
            /* Set volume with codec-specific mapping */
#ifdef CONFIG_AUDIO_TAS6422DAC
            /* Map 0-100% to TAS6422DAC range (-200 to +48 in 0.5 dB steps) */
            int codec_volume;
            if (item.volume == 0) {
                codec_volume = -200;  /* Mute */
            } else if (item.volume <= 80) {
                /* 0-80% maps to -200 to 0 (mute to 0 dB) */
                codec_volume = -200 + (item.volume * 200 / 80);
            } else {
                /* 80-100% maps to 0 to +48 (0 dB to +24 dB) */
                codec_volume = (item.volume - 80) * 48 / 20;
            }
            /* Apply hardcoded maximum limit */
            if (codec_volume > (75 * 48 / 20 - 16)) { /* 75% max volume */
                codec_volume = (75 * 48 / 20 - 16);
            }
            LOG_INF("Audio volume request: %d%%, mapped codec value: %d", 
                    item.volume, codec_volume);
            audio_player_set_volume(codec_volume);
#else
            /* For other codecs, pass volume directly */
            LOG_INF("Audio volume request: %d%%", item.volume);
            audio_player_set_volume(item.volume);
#endif
            
            /* Mark as playing */
            k_mutex_lock(&state_mutex, K_FOREVER);
            currently_playing = true;
            k_mutex_unlock(&state_mutex);
            
            /* Start playback */
            LOG_INF("Calling audio_player_start for file: %s", item.filename);
            player_status_t status = audio_player_start((char *)item.filename);
            if (status != PLAYER_OK) {
                LOG_ERR("Failed to start audio playback: %d", status);
                break;
            }
            
            LOG_INF("audio_player_start returned OK for %s", item.filename);
            
            /* Give audio player time to open and start processing the file */
            k_msleep(500);
            
            /* Check initial playing status */
            bool initial_status = get_playing_status();
            LOG_INF("Initial playing status after 500ms: %s", initial_status ? "true" : "false");
            
            /* Wait for playback to complete or timeout */
            int playback_timeout = 0;
            bool was_playing = false;
            int status_check_count = 0;
            
            while (playback_timeout < 600) { /* Max 60 seconds for playback */
                bool current_status = get_playing_status();
                
                /* Log status periodically */
                if (status_check_count % 50 == 0) { /* Every 5 seconds */
                    LOG_INF("Playing status check %d: %s (was_playing=%s)", 
                            status_check_count, current_status ? "true" : "false",
                            was_playing ? "true" : "false");
                }
                status_check_count++;
                
                if (current_status) {
                    was_playing = true;
                }
                
                /* If we were playing and now we're not, playback completed */
                if (was_playing && !current_status) {
                    LOG_INF("Playback completed for %s", item.filename);
                    break;
                }
                
                /* Check if we should stop */
                k_mutex_lock(&state_mutex, K_FOREVER);
                if (should_stop) {
                    should_stop = false;
                    k_mutex_unlock(&state_mutex);
                    audio_player_stop();
                    LOG_INF("Playback interrupted by user");
                    break;
                }
                k_mutex_unlock(&state_mutex);
                
                k_msleep(100);
                playback_timeout++;
            }
            
            if (playback_timeout >= 600) {
                LOG_WRN("Playback timeout after 60 seconds");
            }
        }
        
        /* Mark as not playing */
        k_mutex_lock(&state_mutex, K_FOREVER);
        currently_playing = false;
        k_mutex_unlock(&state_mutex);
        
        /* Clean up temporary file */
        if (strstr(item.filename, "/lfs/mqtt_audio_") != NULL) {
            /* Add extra delay before deleting to ensure audio player is done with the file */
            k_msleep(1000);
            LOG_INF("Deleting temporary file: %s", item.filename);
            ret = file_manager_delete(item.filename);
            if (ret != 0) {
                LOG_WRN("Failed to delete temporary file %s: %d", item.filename, ret);
            }
        }
    }
}

int audio_queue_init(void)
{
    if (audio_thread_id != NULL) {
        LOG_WRN("Audio queue already initialized");
        return 0;
    }
    
    k_mutex_init(&state_mutex);
    
    /* Create audio playback thread */
    audio_thread_id = k_thread_create(&audio_thread,
                                      audio_thread_stack,
                                      K_THREAD_STACK_SIZEOF(audio_thread_stack),
                                      audio_playback_thread,
                                      NULL, NULL, NULL,
                                      AUDIO_THREAD_PRIORITY, 0, K_NO_WAIT);
    
    if (audio_thread_id == NULL) {
        LOG_ERR("Failed to create audio playback thread");
        return -ENOMEM;
    }
    
    k_thread_name_set(audio_thread_id, "audio_queue");
    LOG_INF("Audio queue initialized");
    
    return 0;
}

int audio_queue_add(const struct audio_queue_item *item)
{
    int ret;
    
    if (!item) {
        return -EINVAL;
    }
    
    /* Try to add to queue */
    ret = k_msgq_put(&audio_msgq, item, K_NO_WAIT);
    if (ret != 0) {
        LOG_ERR("Audio queue full, dropping message");
        return -ENOMEM;
    }
    
    LOG_INF("Added audio to queue: %s (queue depth: %d/%d)", 
            item->filename, k_msgq_num_used_get(&audio_msgq), AUDIO_QUEUE_SIZE);
    
    return 0;
}

bool audio_queue_is_playing(void)
{
    bool playing;
    
    k_mutex_lock(&state_mutex, K_FOREVER);
    playing = currently_playing;
    k_mutex_unlock(&state_mutex);
    
    return playing;
}

void audio_queue_stop_current(void)
{
    k_mutex_lock(&state_mutex, K_FOREVER);
    should_stop = true;
    k_mutex_unlock(&state_mutex);
}
