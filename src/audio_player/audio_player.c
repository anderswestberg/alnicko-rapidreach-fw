/**
 * @file audio_player.c
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

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/audio/codec.h>
#include <string.h>
#include <zephyr/drivers/gpio.h>
#include <stdlib.h>
#include <zephyr/logging/log.h>
#include <zephyr/fs/fs.h>

#include "opus_interface.h"
#include "ogg/ogg.h"
#include "opus_header.h"
#include "audio_player.h"

LOG_MODULE_REGISTER(audio_player, CONFIG_RPR_MODULE_AUDIO_PLAYER_LOG_LEVEL);

#define AUDIO_THREAD_PRIORITY   5
#define AUDIO_THREAD_STACK_SIZE 16384
#define OGG_CHUNK_SIZE          2048

#define I2S_CODEC_TX DT_ALIAS(i2s_codec_tx)

#define CODEC_ENABLE_VALUE  1
#define CODEC_DISABLE_VALUE 0

#define CODEC_STANDBY_GPIO_NODE DT_ALIAS(codec_standby_pin)

#define CODEC_STANDBY_GPIO_SPEC GPIO_DT_SPEC_GET(CODEC_STANDBY_GPIO_NODE, gpios)

#define SAMPLE_FREQUENCY CONFIG_RPR_SAMPLE_FREQ
#define SAMPLE_BIT_WIDTH (16U)
#define BYTES_PER_SAMPLE sizeof(int16_t)

#define MONO_CHANNELS   1U
#define STEREO_CHANNELS 2U

#ifdef CONFIG_RPR_AUDIO_PLAYER_STEREO
#define NUMBER_OF_CHANNELS STEREO_CHANNELS
#else
#define NUMBER_OF_CHANNELS MONO_CHANNELS
#endif

#define DUPLICATION_FACTOR 2

#define BLOCK_COUNT CONFIG_RPR_I2S_BLOCK_BUFFERS
#define TIMEOUT     (2000U)

#define DECODER_MS_FRAME 20

#ifdef CONFIG_RPR_DEFAULT_MUTE_STATE
#define INITIAL_MUTE true
#else
#define INITIAL_MUTE false
#endif

#define CODEC_SLEEP_TIME_MS 500

#define CODEC_PING_TIME_MS 100

#define SAMPLES_PER_BLOCK \
    (((SAMPLE_FREQUENCY / 1000) * DECODER_MS_FRAME) * NUMBER_OF_CHANNELS)

#define BLOCK_SIZE (SAMPLES_PER_BLOCK * BYTES_PER_SAMPLE)

#define AUDIO_COUNT_SILENCE_BLOCK CONFIG_RPR_AUDIO_COUNT_SILENCE_BLOCK

K_MEM_SLAB_DEFINE_STATIC(mem_slab, BLOCK_SIZE, BLOCK_COUNT, 4);

struct audio_player_cfg {
    struct gpio_dt_spec codec_standby_gpio;
#ifdef CONFIG_I2S
    struct i2s_config    i2s_cfg;
    const struct device *i2s_dev;
#endif
#ifdef CONFIG_AUDIO_CODEC
    struct audio_codec_cfg codec_cfg;
    audio_property_value_t volume;
    audio_property_value_t mute;
    const struct device   *codec_dev;
#endif
    bool           is_codec_ready;
    bool           pause;
    bool           is_sound_playing;
    struct k_event audio_event;
    char           filepath[FULL_AUDIO_PATH_MAX_LEN];
    bool           is_stream_init;
};

static DEC_Opus_ConfigTypeDef DecConfigOpus;

static struct audio_player_cfg audio_player_cfg = {
    .codec_standby_gpio = CODEC_STANDBY_GPIO_SPEC,
#ifdef CONFIG_I2S
    .i2s_dev = DEVICE_DT_GET(I2S_CODEC_TX),
#endif
#ifdef CONFIG_AUDIO_CODEC
    .codec_dev = DEVICE_DT_GET(DT_NODELABEL(audio_codec)),
#endif
    .is_codec_ready = false,
};

#ifdef CONFIG_RPR_MEASURING_DECODE_TIME
static int decoded_samples_total = 0;
#endif

/**
 * @brief Audio playback thread function. Waits for start event and processes Opus data.
 */
static void audio_thread_func(void);

K_THREAD_DEFINE(audio_thread_id,
                AUDIO_THREAD_STACK_SIZE,
                audio_thread_func,
                NULL,
                NULL,
                NULL,
                AUDIO_THREAD_PRIORITY,
                0,
                0);

/**
 * @brief Sends I2S trigger command to the I2S device.
 * 
 * @param cmd I2S trigger command (e.g., START, STOP, DRAIN).
 * @return 0 on success, negative error code otherwise.
 */
static int trigger_i2s_command(enum i2s_trigger_cmd cmd)
{
#ifdef CONFIG_I2S
    return i2s_trigger(audio_player_cfg.i2s_dev, I2S_DIR_TX, cmd);
#else
    return 0;
#endif
}

/**
 * @brief Initializes audio player including I2S and audio codec configuration.
 * 
 * @return PLAYER_OK on success, error code otherwise.
 */
static player_status_t audio_player_init(void)
{
    player_status_t ret_status;
    int             ret;

    /* Check if already initialized */
    if (audio_player_cfg.is_codec_ready) {
        LOG_DBG("Audio player already initialized");
        return PLAYER_OK;
    }

    if (!device_is_ready(audio_player_cfg.codec_standby_gpio.port)) {
        LOG_ERR("GPIO EN for codec is not ready");
        return PLAYER_ERROR_GPIO_INIT;
    }

    if (!gpio_is_ready_dt(&audio_player_cfg.codec_standby_gpio)) {
        LOG_ERR("Codec enable GPIO not ready");
        return PLAYER_ERROR_GPIO_INIT;
    }

    ret = gpio_pin_configure_dt(&audio_player_cfg.codec_standby_gpio,
                                GPIO_OUTPUT_HIGH);

    if (ret != 0) {
        LOG_ERR("Failed to enable codec GPIO (err %d)", ret);
        return PLAYER_ERROR_GPIO_SET;
    }

#ifdef CONFIG_I2S
    if (!device_is_ready(audio_player_cfg.i2s_dev)) {
        LOG_ERR("I2S device is not ready %s", audio_player_cfg.i2s_dev->name);
        return PLAYER_ERROR_I2S_INIT;
    }
    struct i2s_config *i2s_config = &audio_player_cfg.i2s_cfg;

    i2s_config->word_size = SAMPLE_BIT_WIDTH;
    i2s_config->channels  = NUMBER_OF_CHANNELS;
    i2s_config->format    = I2S_FMT_DATA_FORMAT_I2S;
    i2s_config->options   = I2S_OPT_BIT_CLK_MASTER | I2S_OPT_FRAME_CLK_MASTER;
    i2s_config->frame_clk_freq = SAMPLE_FREQUENCY;
    i2s_config->mem_slab       = &mem_slab;
    i2s_config->block_size     = BLOCK_SIZE;
    i2s_config->timeout        = SYS_FOREVER_MS;

    ret = i2s_configure(audio_player_cfg.i2s_dev, I2S_DIR_TX, i2s_config);

    if (ret != 0) {
        LOG_ERR("I2S config is failed");
        return PLAYER_ERROR_I2S_CFG;
    }
#else
    LOG_WRN("I2S not supported");
#endif
#ifdef CONFIG_AUDIO_CODEC
    if (!device_is_ready(audio_player_cfg.codec_dev)) {
        LOG_ERR("Codec device is not ready %s",
                audio_player_cfg.codec_dev->name);
        return PLAYER_ERROR_CODEC_INIT;
    }
    struct audio_codec_cfg *codec_cfg     = &audio_player_cfg.codec_cfg;
    codec_cfg->dai_route                  = AUDIO_ROUTE_PLAYBACK;
    codec_cfg->dai_type                   = AUDIO_DAI_TYPE_I2S;
    codec_cfg->dai_cfg.i2s.word_size      = SAMPLE_BIT_WIDTH;
    codec_cfg->dai_cfg.i2s.channels       = NUMBER_OF_CHANNELS;
    codec_cfg->dai_cfg.i2s.format         = I2S_FMT_DATA_FORMAT_I2S;
    codec_cfg->dai_cfg.i2s.options        = I2S_OPT_FRAME_CLK_MASTER;
    codec_cfg->dai_cfg.i2s.frame_clk_freq = SAMPLE_FREQUENCY;
    codec_cfg->dai_cfg.i2s.mem_slab       = &mem_slab;
    codec_cfg->dai_cfg.i2s.block_size     = BLOCK_SIZE;
    ret = audio_codec_configure(audio_player_cfg.codec_dev, codec_cfg);
    k_msleep(CODEC_SLEEP_TIME_MS);

    audio_player_cfg.mute.mute = false;

#else
    LOG_WRN("Audio codec not supported");
#endif
    audio_player_cfg.is_codec_ready   = true;
    audio_player_cfg.pause            = false;
    audio_player_cfg.is_sound_playing = false;
    audio_player_cfg.is_stream_init   = false;

#ifdef CONFIG_AUDIO_CODEC
    ret_status = audio_player_set_volume(INITIAL_VOLUME);
    if (ret_status != PLAYER_OK) {
        LOG_ERR("Codec config is failed");
        return PLAYER_ERROR_CODEC_CFG;
    }
    ret_status = audio_player_set_mute(INITIAL_MUTE);
    if (ret_status != PLAYER_OK) {
        LOG_ERR("Codec config mute is failed");
        return PLAYER_ERROR_CODEC_CFG;
    }

#ifdef CONFIG_RPR_AUDIO_AUTO_MUTE
    audio_codec_stop_output(audio_player_cfg.codec_dev);
    audio_player_cfg.mute.mute = true;
#endif
#endif

    LOG_DBG("Audio player initialized successfully");

#ifdef CONFIG_RPR_AUDIO_ENABLE_STANDBY_WHEN_IDLE
    ret = gpio_pin_set_dt(&audio_player_cfg.codec_standby_gpio,
                          CODEC_DISABLE_VALUE);
    if (ret != 0) {
        LOG_ERR("Failed to disable codec GPIO (err %d)", ret);
        return PLAYER_ERROR_GPIO_SET;
    }
#endif

    return PLAYER_OK;
}

/**
 * @brief Sets the output volume of the audio codec.
 * 
 * @param volume Desired volume level (within allowed codec range).
 * @return PLAYER_OK on success, error code otherwise.
 */
player_status_t audio_player_set_volume(int volume)
{
#ifdef CONFIG_AUDIO_CODEC
    const struct device *codec_dev = audio_player_cfg.codec_dev;

    int ret;
    if (!audio_player_cfg.is_codec_ready) {
        LOG_ERR("Codec device is not ready %s", codec_dev->name);
        return PLAYER_ERROR_CODEC_INIT;
    }
    if (volume < MINIMUM_CODEC_VOLUME) {
        volume = MINIMUM_CODEC_VOLUME;
        LOG_WRN("The set value is above/below the required volume value. Maximum: %d, Minimum: %d",
                MAXIMUM_CODEC_VOLUME,
                MINIMUM_CODEC_VOLUME);
    }

    if (volume > MAXIMUM_CODEC_VOLUME) {
        volume = MAXIMUM_CODEC_VOLUME;
        LOG_WRN("The set value is above/below the required volume value. Maximum: %d, Minimum: %d",
                MAXIMUM_CODEC_VOLUME,
                MINIMUM_CODEC_VOLUME);
    }

    audio_player_cfg.volume.vol = volume;
#if defined(CONFIG_DT_HAS_CIRRUS_CS43L22_ENABLED)

    ret = audio_codec_set_property(codec_dev,
                                   AUDIO_PROPERTY_OUTPUT_VOLUME,
                                   AUDIO_CHANNEL_HEADPHONE_LEFT,
                                   audio_player_cfg.volume);
    ret = audio_codec_set_property(codec_dev,
                                   AUDIO_PROPERTY_OUTPUT_VOLUME,
                                   AUDIO_CHANNEL_HEADPHONE_RIGHT,
                                   audio_player_cfg.volume);
#elif DT_NODE_HAS_COMPAT(DT_NODELABEL(audio_codec), ti_tas6422dac)
    ret = audio_codec_set_property(codec_dev,
                                   AUDIO_PROPERTY_OUTPUT_VOLUME,
                                   AUDIO_CHANNEL_ALL,
                                   audio_player_cfg.volume);

#endif

    if (audio_codec_apply_properties(codec_dev) || ret) {
        LOG_ERR("Codec volume set is failed %d", ret);
        return PLAYER_ERROR_CODEC_CFG;
    }
    LOG_DBG("The volume level is set %d", audio_player_cfg.volume.vol);

#else
    LOG_WRN("Audio codec not supported");
#endif
    return PLAYER_OK;
}

/**
 * @brief Mutes or unmutes the audio output.
 * 
 * @param mute true to mute, false to unmute.
 * @return PLAYER_OK on success, error code otherwise.
 */
player_status_t audio_player_set_mute(bool mute)
{
#ifdef CONFIG_AUDIO_CODEC
    int                  ret;
    const struct device *codec_dev = audio_player_cfg.codec_dev;

    if (!audio_player_cfg.is_codec_ready) {
        LOG_ERR("Codec device is not ready %s",
                audio_player_cfg.codec_dev->name);
        return PLAYER_ERROR_CODEC_INIT;
    }

    audio_player_cfg.mute.mute = mute;

    ret = audio_codec_set_property(codec_dev,
                                   AUDIO_PROPERTY_OUTPUT_MUTE,
                                   AUDIO_CHANNEL_ALL,
                                   audio_player_cfg.mute);
    if (audio_codec_apply_properties(codec_dev) || ret) {
        LOG_ERR("Mute set/unset is failed %d", ret);
        return PLAYER_ERROR_CODEC_CFG;
    }

    LOG_DBG("Mute is %s", audio_player_cfg.mute.mute ? "true" : "false");
#else
    LOG_WRN("Audio codec not supported");

#endif

    return PLAYER_OK;
}

/**
 * @brief Fills I2S buffer with silence blocks or starts I2S stream.
 * 
 * @return PLAYER_OK or PLAYER_ERROR_I2S.
 */
static player_status_t audio_player_fill_silence(void)
{
#ifdef CONFIG_I2S
    int ret = 0;
    for (int i = 0; i < AUDIO_COUNT_SILENCE_BLOCK; i++) {

        void *zero_block;
        ret = k_mem_slab_alloc(&mem_slab, &zero_block, K_FOREVER);
        if (ret < 0) {
            LOG_ERR("Failed to allocate zero TX block");
            return PLAYER_ERROR_I2S;
        }
        memset(zero_block, 0, BLOCK_SIZE);
        ret = i2s_write(audio_player_cfg.i2s_dev, zero_block, BLOCK_SIZE);
        if (ret < 0) {
            LOG_ERR("Failed to i2s write zero TX block");
            return PLAYER_ERROR_I2S;
        }

        if (!audio_player_cfg.is_sound_playing) {
            ret = trigger_i2s_command(I2S_TRIGGER_START);
            if (ret < 0) {
                LOG_ERR("Failed to set trigger start");
                return PLAYER_ERROR_I2S;
            }
            audio_player_cfg.is_sound_playing = true;
        }
    }
#endif
    return PLAYER_OK;
}

/**
 * @brief Stops the current audio playback and resets playback state.
 */
static void stop_audio_playback(void)
{
    if (!audio_player_cfg.is_sound_playing) {
        LOG_WRN("Audio is not playing");
        return;
    }

#ifdef CONFIG_AUDIO_CODEC
#ifdef CONFIG_RPR_AUDIO_AUTO_MUTE
    audio_codec_stop_output(audio_player_cfg.codec_dev);
    audio_player_cfg.mute.mute = true;
#endif
#else
    LOG_WRN("Audio codec not supported");
#endif

#ifdef CONFIG_I2S

    if (audio_player_cfg.pause) {
        if (trigger_i2s_command(I2S_TRIGGER_DROP) < 0) {
            LOG_ERR("Failed to set trigger drain");
        }

    } else {
        if (audio_player_fill_silence() != PLAYER_OK) {
            LOG_ERR("Failed to fill silence");
        }

        if (trigger_i2s_command(I2S_TRIGGER_DRAIN) < 0) {
            LOG_ERR("Failed to set trigger drain");
        }
    }
#else
    LOG_WRN("Audio I2S not supported");
#endif

    audio_player_cfg.is_sound_playing = false;
    audio_player_cfg.pause            = false;

#ifdef CONFIG_RPR_AUDIO_ENABLE_STANDBY_WHEN_IDLE
    int ret = gpio_pin_set_dt(&audio_player_cfg.codec_standby_gpio,
                              CODEC_DISABLE_VALUE);
    if (ret != 0) {
        LOG_ERR("Failed to disable codec GPIO (err %d)", ret);
    }
#endif

    LOG_DBG("Stoped audio playback");
}

/**
 * @brief Starts audio playback (or resumes if paused).
 * 
 * @return PLAYER_OK or error code.
 */
static player_status_t start_audio_playback(void)
{
    if (audio_player_cfg.is_sound_playing && !audio_player_cfg.pause) {
        LOG_WRN("Audio is playing");
        return PLAYER_OK;
    }

    LOG_DBG("Starting audio playback...");

#ifdef CONFIG_I2S

    if (!audio_player_cfg.is_sound_playing) {
        if (audio_player_fill_silence() != PLAYER_OK) {
            return PLAYER_ERROR_I2S;
        }
    } else if (audio_player_cfg.pause) {
        int ret = trigger_i2s_command(I2S_TRIGGER_START);
        if (ret < 0) {
            LOG_ERR("Failed to set trigger start");
            return PLAYER_ERROR_I2S;
        }
    }
#else
    LOG_WRN("Audio I2S not supported");
    audio_player_cfg.is_sound_playing = true;
#endif

#ifdef CONFIG_AUDIO_CODEC
#ifdef CONFIG_RPR_AUDIO_AUTO_MUTE
    audio_codec_start_output(audio_player_cfg.codec_dev);
    audio_player_cfg.mute.mute = false;
#endif
#else
    LOG_WRN("Audio codec not supported");
#endif

    audio_player_cfg.pause = false;

    return PLAYER_OK;
}

/**
 * @brief Pauses current playback and stops I2S stream.
 * 
 * @return PLAYER_OK or error code.
 */
static player_status_t pause_audio_playback(void)
{
    if (!audio_player_cfg.is_sound_playing && audio_player_cfg.pause) {
        LOG_WRN("Audio is not playing");
        return PLAYER_OK;
    }

    LOG_DBG("Pausing audio playback...");

#ifdef CONFIG_AUDIO_CODEC
#ifdef CONFIG_RPR_AUDIO_AUTO_MUTE
    audio_codec_stop_output(audio_player_cfg.codec_dev);
    audio_player_cfg.mute.mute = true;
#endif
#else
    LOG_WRN("Audio codec not supported");
#endif

#ifdef CONFIG_I2S

    if (audio_player_fill_silence() != PLAYER_OK) {
        LOG_ERR("Failed to fill_silence");
        return PLAYER_ERROR_I2S;
    }

    if (trigger_i2s_command(I2S_TRIGGER_STOP) < 0) {
        LOG_ERR("Failed to set trigger stop");
        return PLAYER_ERROR_I2S;
    }

#else
    LOG_WRN("Audio I2S not supported");
#endif

    audio_player_cfg.pause = true;

    return PLAYER_OK;
}

/**
 * @brief Initializes Ogg stream and Opus decoder.
 * 
 * @param oy Ogg sync state.
 * @param os Ogg stream state.
 * @param og Pointer to the current Ogg page.
 * @param op Pointer to the current Ogg packet.
 * @return true if successful, false otherwise.
 */
static bool audio_player_stream_and_decoder_init(ogg_sync_state   *oy,
                                                 ogg_stream_state *os,
                                                 ogg_page         *og,
                                                 ogg_packet       *op)
{
    printk("\n=== DECODER INIT START ===\n");
    
    int stream = ogg_stream_init(os, ogg_page_serialno(og));
    printk("Stream init result: %d\n", stream);
    if (!stream)
        audio_player_cfg.is_stream_init = true;
    else {
        printk("FAILED: stream init\n");
        LOG_ERR("Failed stream init - %d", stream);
        return false;
    }

    ogg_stream_pagein(os, og);

    int packet_result = ogg_stream_packetout(os, op);
    printk("Packet out result: %d\n", packet_result);
    if (packet_result != 1) {
        printk("FAILED: packet out\n");
        LOG_ERR("Invalid Opus header packet");
        return false;
    }

    OpusHeader header;
    int parse_result = opus_header_parse(op->packet, op->bytes, &header);
    printk("Header parse result: %d, sample_rate=%d, channels=%d\n", 
           parse_result, header.input_sample_rate, header.channels);
    if (parse_result == 0) {
        printk("FAILED: header parse\n");
        LOG_ERR("Cannot parse Opus header");
        return false;
    }

    printk("SUCCESS: Header parsed OK\n");
    LOG_INF("Decoder init: checking if already configured...");
    if (DEC_Opus_IsConfigured()) {
        LOG_ERR("Opus decoder is already configured");
        return false;
    }
    LOG_DBG("Sample_freq: %d, channel: %d",
            header.input_sample_rate,
            header.channels);

    /* Use hardcoded 48000 Hz to match I2S configuration */
    /* Device-server encodes all audio to 48000 Hz */
    DecConfigOpus.sample_freq = SAMPLE_FREQUENCY;  /* Fixed 48000 Hz */
    DecConfigOpus.channels    = MONO_CHANNELS;     /* Fixed 1 channel */
    DecConfigOpus.ms_frame    = DECODER_MS_FRAME;  /* 20ms frames */
    
    LOG_INF("Configuring Opus decoder: %u Hz, %u channels (hardcoded)",
            DecConfigOpus.sample_freq, DecConfigOpus.channels);

    uint32_t dec_size = DEC_Opus_getMemorySize(&DecConfigOpus);

    LOG_DBG("dec_size: %d", dec_size);

    DecConfigOpus.pInternalMemory = malloc(dec_size * DUPLICATION_FACTOR);
    if (!DecConfigOpus.pInternalMemory) {
        LOG_ERR("Decoder memory allocation failed");
        return false;
    }

    int opus_err;
    if (DEC_Opus_Init(&DecConfigOpus, &opus_err) != OPUS_SUCCESS) {
        LOG_ERR("Decoder init failed: %d", opus_err);
        return false;
    }

    return true;
}

/**
 * @brief Duplicates each sample in-place within the given buffer.
 *
 * The buffer must be preallocated with at least `samples * DUPLICATION_FACTOR` elements.
 * Each original sample is duplicated sequentially, starting from the end,
 * so that buffer content like [1, 2, 3] becomes [1, 1, 2, 2, 3, 3].
 *
 * @param buffer Pointer to the buffer containing original samples.
 *               MAST have enough space for doubled output.
 * @param samples Number of original 16-bit samples in the buffer.
 *
 * @return Total number of samples after duplication (samples * DUPLICATION_FACTOR), or 0 on error.
 */
static size_t duplicate_samples(int16_t *buffer, size_t samples)
{
    if (!buffer || samples <= 0)
        return 0;

    for (int i = (int)samples - 1; i >= 0; --i) {
        buffer[i * DUPLICATION_FACTOR]     = buffer[i];
        buffer[i * DUPLICATION_FACTOR + 1] = buffer[i];
    }

    return samples * DUPLICATION_FACTOR;
}

/**
 * @brief Decodes an Opus packet and writes PCM data to I2S output.
 * 
 * @param op Pointer to decoded Ogg Opus packet.
 * @return true on success, false on failure.
 */
static bool audio_player_decode_and_write(ogg_packet *op)
{
#ifdef CONFIG_RPR_MEASURING_DECODE_TIME
    uint64_t decode_start = k_uptime_get();
#endif
    
    int decoded_samples = DEC_Opus_Decode(
            op->packet, op->bytes, DecConfigOpus.pInternalMemory);
    
#ifdef CONFIG_RPR_MEASURING_DECODE_TIME
    uint64_t decode_time = k_uptime_delta(&decode_start);
    if (decode_time > 10) {  // Log only slow decodes (>10ms)
        LOG_DBG("Opus decode took %lld ms for packet (%u bytes -> %d samples)", 
                decode_time, op->bytes, decoded_samples);
    }
#endif
    
    if (decoded_samples < 0) {
        LOG_ERR("Opus decoding error: %d", decoded_samples);
        return false;
    }
    
    /* Yield after decode to allow MQTT thread to run */
    k_yield();

    int16_t *pcm_ptr = (int16_t *)DecConfigOpus.pInternalMemory;
    int      samples_remaining;

    /* IMPORTANT: Always duplicate samples for proper I2S output */
    /* This converts mono decoded data to the stereo layout the I2S expects */
    samples_remaining = duplicate_samples(pcm_ptr, decoded_samples);
    
    LOG_DBG("Decoded %d samples, after duplication: %d samples", 
            decoded_samples, samples_remaining);

    while (samples_remaining > 0) {
        void *mem_block;
        int   samples_to_copy = MIN(SAMPLES_PER_BLOCK, samples_remaining);

#ifdef CONFIG_I2S
        if (k_mem_slab_alloc(&mem_slab, &mem_block, Z_TIMEOUT_TICKS(TIMEOUT))) {
            LOG_ERR("Failed to allocate TX block");
            return false;
        }

        memcpy(mem_block, pcm_ptr, samples_to_copy * BYTES_PER_SAMPLE);
        memset((uint8_t *)mem_block + samples_to_copy * BYTES_PER_SAMPLE,
               0,
               BLOCK_SIZE - samples_to_copy * BYTES_PER_SAMPLE);

        if (i2s_write(audio_player_cfg.i2s_dev, mem_block, BLOCK_SIZE) < 0) {
            LOG_ERR("Failed to write I2S");
            return false;
        }
#endif

#ifdef CONFIG_RPR_MEASURING_DECODE_TIME
        decoded_samples_total++;
#endif
        pcm_ptr += samples_to_copy;
        samples_remaining -= samples_to_copy;
    }

    return true;
}

/**
 * @brief Releases all allocated audio resources after playback.
 * 
 * @param os Ogg stream state.
 * @param oy Ogg sync state.
 */
static void audio_player_cleanup(ogg_stream_state *os, ogg_sync_state *oy)
{
    if (DEC_Opus_IsConfigured()) {
        DEC_Opus_Deinit();
    }

    if (DecConfigOpus.pInternalMemory) {
        free(DecConfigOpus.pInternalMemory);
        DecConfigOpus.pInternalMemory = NULL;
    }

    if (audio_player_cfg.is_stream_init) {
        ogg_stream_clear(os);
        audio_player_cfg.is_stream_init = false;
    }

    ogg_sync_clear(oy);
}

/**
 * @brief Handles STOP and PAUSE/RESUME events during playback.
 * 
 * @return true if playback should be stopped, false otherwise.
 */
static bool handle_audio_control_events(void)
{
    uint32_t new_evt =
            k_event_wait(&audio_player_cfg.audio_event,
                         AUDIO_EVT_PAUSE | AUDIO_EVT_STOP | AUDIO_EVT_PING,
                         false,
                         K_NO_WAIT);

    if (new_evt & AUDIO_EVT_STOP) {
        LOG_DBG("Stop event received");
        return true;
    }

    if (new_evt & AUDIO_EVT_PAUSE) {
        LOG_DBG("Pause event received");

        pause_audio_playback();

        LOG_DBG("Waiting for Resume (START) or Stop (STOP) event...");

        new_evt = k_event_wait(&audio_player_cfg.audio_event,
                               AUDIO_EVT_START | AUDIO_EVT_STOP,
                               true,
                               K_FOREVER);

        if (new_evt & AUDIO_EVT_STOP) {
            LOG_DBG("Stop event received during pause");
            return true;
        }

        if (new_evt & AUDIO_EVT_START) {
            LOG_DBG("Resume event received");
            start_audio_playback();
        }
    }

    if (new_evt & AUDIO_EVT_PING) {
        k_event_post(&audio_player_cfg.audio_event, AUDIO_EVT_PING_REPLY);
    }

    return false;
}

/**
 * @brief Audio playback thread function. Waits for start event and processes Opus data.
 */
static void audio_thread_func(void)
{
    LOG_INF("Audio player thread started");
    k_event_init(&audio_player_cfg.audio_event);
    
    /* Initialize audio player if not already done */
    if (audio_player_init() != PLAYER_OK) {
        LOG_ERR("Failed to initialize audio player");
    }

    while (1) {
        uint32_t evt = k_event_wait(&audio_player_cfg.audio_event,
                                    AUDIO_EVT_START | AUDIO_EVT_PING,
                                    true,
                                    K_FOREVER);
        
        /* Handle ping events quietly */
        if (evt & AUDIO_EVT_PING) {
            k_event_post(&audio_player_cfg.audio_event, AUDIO_EVT_PING_REPLY);
            continue;
        }

        if (evt & AUDIO_EVT_START) {
            LOG_INF("Received AUDIO_EVT_START, file: %s", audio_player_cfg.filepath);

            ogg_sync_state   oy;
            ogg_page         og;
            ogg_packet       op;
            ogg_stream_state os;

            struct fs_file_t file;
            fs_file_t_init(&file);

            ogg_sync_init(&oy);

            bool first_packet_skipped = false;
            bool stopped              = false;

            if (start_audio_playback() != PLAYER_OK) {
                LOG_ERR("start_audio_playback() failed");
                continue;
            }

            if (fs_open(&file, audio_player_cfg.filepath, FS_O_READ) < 0) {
                LOG_ERR("Cannot open audio file: %s",
                        audio_player_cfg.filepath);
                continue;
            }

#ifdef CONFIG_RPR_MEASURING_DECODE_TIME
            decoded_samples_total = 0;
            uint64_t decode_time_start = k_uptime_get();
#endif

            LOG_INF("Playback start");

#ifndef CONFIG_I2S
            LOG_WRN("Sound output is disabled");
#endif

            while (!stopped) {
                char   *buffer   = ogg_sync_buffer(&oy, OGG_CHUNK_SIZE);
                ssize_t read_len = fs_read(&file, buffer, OGG_CHUNK_SIZE);

                if (read_len <= 0) {
                    break;
                }

                ogg_sync_wrote(&oy, read_len);
                
                /* Yield after each file read to allow MQTT thread to run */
                k_yield();

                while (ogg_sync_pageout(&oy, &og) == 1 && !stopped) {
                    if (!audio_player_cfg.is_stream_init) {
                        if (!audio_player_stream_and_decoder_init(
                                    &oy, &os, &og, &op)) {
                            stopped = true;
                        }
                        continue;
                    }

                    ogg_stream_pagein(&os, &og);
                    while (ogg_stream_packetout(&os, &op) == 1 && !stopped) {
                        // Skip the first packet (OpusTags)
                        if (!first_packet_skipped) {
                            first_packet_skipped = true;
                            continue;
                        }

                        if (!audio_player_decode_and_write(&op)) {
                            stopped = true;
                            break;
                        }
                        if (handle_audio_control_events()) {
                            stopped = true;
                            break;
                        }
                        
                        /* Yield periodically to allow other threads (like MQTT) to run */
                        static int yield_counter = 0;
                        if (++yield_counter >= 1) {  /* Yield after every packet for better MQTT handling */
                            yield_counter = 0;
                            k_yield();
                            /* Allow a bit more time for network processing */
                            k_msleep(1);
                        }
                    }
                }
            }

#ifdef CONFIG_RPR_MEASURING_DECODE_TIME
            int64_t total_decode_time = k_uptime_delta(&decode_time_start);
            LOG_INF("Total time (decode + I2S output): %lld ms for %d packets", 
                    total_decode_time, decoded_samples_total);
            LOG_INF("Note: Time includes I2S writes and yields - pure decode is typically <1ms per packet");
#endif

            LOG_INF("Playback finished");
            stop_audio_playback();
            fs_close(&file);
            audio_player_cleanup(&os, &oy);
        }

        k_event_post(&audio_player_cfg.audio_event, AUDIO_EVT_PING_STOP);
        k_msleep(100);
    }
}

/**
 * @brief Starts playback of an Opus audio stream.
 * 
 * @param filepath Full path to the Opus audio file to play.
 * @return PLAYER_OK on success, error code otherwise.
 */
player_status_t audio_player_start(const char *filepath)
{
    if (!audio_player_cfg.is_codec_ready) {
        LOG_ERR("Device is not ready");
        return PLAYER_ERROR_CODEC_INIT;
    }

    if (audio_player_cfg.is_sound_playing) {
        LOG_ERR("Device is busy");
        return PLAYER_ERROR_BUSY;
    }

    if (!filepath) {
        LOG_ERR("Filepath is empty");
        return PLAYER_EMPTY_DATA;
    }

    if (strlen(filepath) >= FULL_AUDIO_PATH_MAX_LEN) {
        LOG_ERR("Audio path is too long");
        return PLAYER_ERROR_INVALID_PARAM;
    }

#ifdef CONFIG_RPR_AUDIO_ENABLE_STANDBY_WHEN_IDLE
    int ret = gpio_pin_set_dt(&audio_player_cfg.codec_standby_gpio,
                              CODEC_ENABLE_VALUE);
    if (ret != 0) {
        LOG_ERR("Failed to enable codec GPIO (err %d)", ret);
        return PLAYER_ERROR_GPIO_SET;
    }
#endif

    strncpy(audio_player_cfg.filepath,
            filepath,
            sizeof(audio_player_cfg.filepath) - 1);
    audio_player_cfg.filepath[sizeof(audio_player_cfg.filepath) - 1] = '\0';

    LOG_INF("Posting AUDIO_EVT_START for file: %s", filepath);
    k_event_post(&audio_player_cfg.audio_event, AUDIO_EVT_START);
    return PLAYER_OK;
}

/**
 * @brief Pauses or resumes playback.
 * 
 * @param state true to pause, false to resume.
 * @return PLAYER_OK on success, error code otherwise.
 */
player_status_t audio_player_pause(bool state)
{
    if (!audio_player_cfg.is_codec_ready) {
        LOG_ERR("Device is not ready");
        return PLAYER_ERROR_CODEC_INIT;
    }

    if (!audio_player_cfg.is_sound_playing)
        return PLAYER_OK;

    k_event_post(&audio_player_cfg.audio_event,
                 state ? AUDIO_EVT_PAUSE : AUDIO_EVT_START);

    return PLAYER_OK;
}

/**
 * @brief Stops current audio playback.
 * 
 * @return PLAYER_OK on success, error code otherwise.
 */
player_status_t audio_player_stop(void)
{
    if (!audio_player_cfg.is_codec_ready) {
        LOG_ERR("Device is not ready");
        return PLAYER_ERROR_CODEC_INIT;
    }
    if (!audio_player_cfg.is_sound_playing)
        return PLAYER_OK;

    k_event_post(&audio_player_cfg.audio_event, AUDIO_EVT_STOP);

    return PLAYER_OK;
}

/**
 * @brief Returns the current mute status.
 * 
 * @return true if muted, false otherwise.
 */
bool get_mute_status(void)
{
#ifdef CONFIG_AUDIO_CODEC
    return audio_player_cfg.mute.mute;
#else
    return false;
#endif
}

/**
 * @brief Returns the current volume level.
 * 
 * @return Volume level in configured scale.
 */
int get_volume(void)
{
#ifdef CONFIG_AUDIO_CODEC
    return audio_player_cfg.volume.vol;
#else
    return 0;
#endif
}

/**
 * @brief Returns the current playback status.
 * 
 * @return true if playback is active, false otherwise.
 */
bool get_playing_status(void)
{
    return audio_player_cfg.is_sound_playing;
}

/**
 * @brief Returns the current pause status.
 * 
 * @return true if playback is paused, false otherwise.
 */
bool get_pause_status(void)
{
    return audio_player_cfg.pause;
}

/**
 * @brief Enable the audio codec via the enable GPIO pin and initialize the audio player.
 *
 * @return PLAYER_OK on success,
 *         PLAYER_ERROR_GPIO_INIT if GPIO is not ready,
 *         PLAYER_ERROR_GPIO_SET if setting GPIO fails,
 *         or an error code from audio_player_init().
 */
player_status_t codec_enable(void)
{

    if (!gpio_is_ready_dt(&audio_player_cfg.codec_standby_gpio)) {
        LOG_ERR("Codec standby GPIO not ready");
        return PLAYER_ERROR_GPIO_INIT;
    }

    int ret = gpio_pin_set_dt(&audio_player_cfg.codec_standby_gpio,
                              CODEC_ENABLE_VALUE);

    if (ret != 0) {
        LOG_ERR("Failed to standby codec GPIO (err %d)", ret);
        return PLAYER_ERROR_GPIO_SET;
    }
    LOG_DBG("Audio codec GPIO set to ENABLED");

    player_status_t init_result = audio_player_init();
    if (init_result != PLAYER_OK) {
        LOG_ERR("Audio player initialization failed after GPIO enable");
        return init_result;
    }

    LOG_DBG("Audio codec ENABLED and initialized");
    return PLAYER_OK;
}

/**
 * @brief Disable the audio codec via the enable GPIO pin.
 *
 * @return PLAYER_OK on success, PLAYER_ERROR_GPIO_INIT, 
 * PLAYER_ERROR_GPIO_SET or PLAYER_ERROR_CODEC_STOP on failure.
 */
player_status_t codec_disable(void)
{

    if (audio_player_cfg.is_sound_playing) {
        player_status_t stop_status = audio_player_stop();
        if (stop_status != PLAYER_OK) {
            LOG_ERR("Failed to stop audio playback before disabling codec");
            return PLAYER_ERROR_CODEC_STOP;
        }
        k_msleep(CODEC_SLEEP_TIME_MS);
    }

    if (!gpio_is_ready_dt(&audio_player_cfg.codec_standby_gpio)) {
        LOG_ERR("Codec standby GPIO not ready");
        return PLAYER_ERROR_GPIO_INIT;
    }

    int ret = gpio_pin_set_dt(&audio_player_cfg.codec_standby_gpio,
                              CODEC_DISABLE_VALUE);
    if (ret != 0) {
        LOG_ERR("Failed to standby codec GPIO (err %d)", ret);
        return PLAYER_ERROR_GPIO_SET;
    }

    audio_player_cfg.is_codec_ready = false;

    LOG_DBG("Audio codec DISABLED");
    return PLAYER_OK;
}

/**
 * @brief Waits for ping reply or stop signal from audio thread.
 * 
 * Can be used with a watchdog.
 *
 * @return AUDIO_EVT_PING_REPLY or AUDIO_EVT_PING_STOP bitmask.
 */
uint32_t audio_player_ping(void)
{
    k_event_post(&audio_player_cfg.audio_event, AUDIO_EVT_PING);

    uint32_t result = k_event_wait(&audio_player_cfg.audio_event,
                                   AUDIO_EVT_PING_REPLY | AUDIO_EVT_PING_STOP,
                                   false,
                                   K_MSEC(CODEC_PING_TIME_MS));

    return result;
}