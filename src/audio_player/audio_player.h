/**
 * @file audio_player.h
 * @brief Audio player module for Opus-encoded audio playback using I2S and audio codec.
 *
 * This module provides functionality to play back Opus-encoded audio files in a Zephyr-based system.
 * It supports audio output via I2S and control over an external audio codec.
 * 
 * The audio data is parsed as Ogg/Opus stream and decoded on the fly using the Opus decoder.
 * The decoded PCM audio is sent to the I2S interface for playback. The module handles playback 
 * control (start, pause, stop), volume and mute control (if codec is present), and internal buffering.
 * 
 * @author Eugene K.
 * @copyright (C) 2025 Alnicko Lab OU. All rights reserved.
 */

#ifndef AUDIO_PLAYER_H_
#define AUDIO_PLAYER_H_

#define AUDIO_EVT_START BIT(0)
#define AUDIO_EVT_STOP  BIT(1)
#define AUDIO_EVT_PAUSE BIT(2)
#define AUDIO_EVT_PING       BIT(4)
#define AUDIO_EVT_PING_REPLY BIT(8)
#define AUDIO_EVT_PING_STOP  BIT(16)

#if defined(CONFIG_DT_HAS_CIRRUS_CS43L22_ENABLED)
#define INITIAL_VOLUME       85
#define MINIMUM_CODEC_VOLUME 0
#define MAXIMUM_CODEC_VOLUME 100
#elif defined(CONFIG_DT_HAS_TI_TAS6422DAC_ENABLED)
#define INITIAL_VOLUME       CONFIG_RPR_DEFAULT_VOLUME_LEVEL
#define MINIMUM_CODEC_VOLUME (-100 * 2)
#define MAXIMUM_CODEC_VOLUME (24 * 2)
#else
#define MINIMUM_CODEC_VOLUME 0
#define MAXIMUM_CODEC_VOLUME 0
#endif

#define FULL_AUDIO_PATH_MAX_LEN \
    (CONFIG_RPR_FOLDER_PATH_MAX_LEN + CONFIG_RPR_FILENAME_MAX_LEN)

typedef enum {
    PLAYER_OK = 0,
    PLAYER_EMPTY_DATA,
    PLAYER_ERROR_INVALID_PARAM,
    PLAYER_ERROR_GPIO_INIT,
    PLAYER_ERROR_GPIO_SET,
    PLAYER_ERROR_I2S_INIT,
    PLAYER_ERROR_I2S_CFG,
    PLAYER_ERROR_CODEC_INIT,
    PLAYER_ERROR_CODEC_CFG,
    PLAYER_VOLUME_FAILED,
    PLAYER_ERROR_I2S,
    PLAYER_ERROR_BUSY,
    PLAYER_ERROR_CODEC_STOP
} player_status_t;

/**
 * @brief Sets the output volume of the audio codec.
 * 
 * @param volume Desired volume level (within allowed codec range).
 * @return PLAYER_OK on success, error code otherwise.
 */
player_status_t audio_player_set_volume(int volume);

/**
 * @brief Mutes or unmutes the audio output.
 * 
 * @param mute true to mute, false to unmute.
 * @return PLAYER_OK on success, error code otherwise.
 */
player_status_t audio_player_set_mute(bool mute);

/**
 * @brief Starts playback of an Opus audio stream.
 * 
 * @param filepath Full path to the Opus audio file to play.
 * @return PLAYER_OK on success, error code otherwise.
 */
player_status_t audio_player_start(const char *filepath);

/**
 * @brief Stops current audio playback.
 * 
 * @return PLAYER_OK on success, error code otherwise.
 */
player_status_t audio_player_stop(void);

/**
 * @brief Pauses or resumes playback.
 * 
 * @param state true to pause, false to resume.
 * @return PLAYER_OK on success, error code otherwise.
 */
player_status_t audio_player_pause(bool state);

/**
 * @brief Returns the current mute status.
 * 
 * @return true if muted, false otherwise.
 */
bool get_mute_status(void);

/**
 * @brief Returns the current volume level.
 * 
 * @return Volume level in configured scale.
 */
int get_volume(void);

/**
 * @brief Returns the current playback status.
 * 
 * @return true if playback is active, false otherwise.
 */
bool get_playing_status(void);
/**
 * @brief Returns the current pause status.
 * 
 * @return true if playback is paused, false otherwise.
 */
bool get_pause_status(void);

/**
 * @brief Enable the audio codec via the enable GPIO pin and initialize the audio player.
 *
 * @return PLAYER_OK on success,
 *         PLAYER_ERROR_GPIO_INIT if GPIO is not ready,
 *         PLAYER_ERROR_GPIO_SET if setting GPIO fails,
 *         or an error code from audio_player_init().
 */
player_status_t codec_enable(void);

/**
 * @brief Disable the audio codec via the enable GPIO pin.
 *
 * @return PLAYER_OK on success, PLAYER_ERROR_GPIO_INIT or PLAYER_ERROR_GPIO_SET on failure.
 */
player_status_t codec_disable(void);

/**
 * @brief Waits for ping reply or stop signal from audio thread.
 * 
 * Can be used with a watchdog.
 *
 * @return AUDIO_EVT_PING_REPLY or AUDIO_EVT_PING_STOP bitmask.
 */
uint32_t audio_player_ping(void);

#endif /* AUDIO_PLAYER_H_ */