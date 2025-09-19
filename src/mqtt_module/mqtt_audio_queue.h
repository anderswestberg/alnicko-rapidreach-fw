/**
 * @file mqtt_audio_queue.h
 * @brief Audio playback queue for MQTT audio messages
 */

#ifndef MQTT_AUDIO_QUEUE_H
#define MQTT_AUDIO_QUEUE_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Audio queue item structure
 */
struct audio_queue_item {
    char filename[64];      /* Path to audio file */
    uint8_t volume;         /* Volume level (0-100) */
    uint8_t priority;       /* Priority level */
    uint8_t play_count;     /* Number of times to play */
    bool interrupt_current; /* Whether to interrupt current playback */
};

/**
 * @brief Initialize the audio playback queue
 * @return 0 on success, negative error code on failure
 */
int audio_queue_init(void);

/**
 * @brief Add an audio file to the playback queue
 * @param item Pointer to audio queue item
 * @return 0 on success, -ENOMEM if queue is full
 */
int audio_queue_add(const struct audio_queue_item *item);

/**
 * @brief Check if audio is currently playing
 * @return true if playing, false otherwise
 */
bool audio_queue_is_playing(void);

/**
 * @brief Stop current playback (if interrupt flag is set)
 */
void audio_queue_stop_current(void);

#endif /* MQTT_AUDIO_QUEUE_H */
