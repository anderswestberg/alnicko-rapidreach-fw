/*
 * Copyright (c) 2025 RapidReach
 * 
 * MQTT Chunked Audio Transfer Protocol
 * 
 * This module implements a protocol for transferring large audio files
 * over MQTT by splitting them into multiple messages.
 */

#ifndef MQTT_AUDIO_CHUNKED_H_
#define MQTT_AUDIO_CHUNKED_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maximum chunk size for audio data (must fit in MQTT message)
 * 
 * We use 32KB chunks which should work well with most MQTT brokers.
 * Adjust based on your broker's max message size and network conditions.
 */
#define MQTT_AUDIO_CHUNK_SIZE      (32 * 1024)

/**
 * @brief Maximum number of concurrent audio transfers
 */
#define MQTT_MAX_CONCURRENT_TRANSFERS 2

/**
 * @brief Timeout for incomplete transfers (in seconds)
 */
#define MQTT_TRANSFER_TIMEOUT_SEC    30

/**
 * @brief Audio transfer state
 */
typedef enum {
    TRANSFER_STATE_IDLE = 0,
    TRANSFER_STATE_RECEIVING,
    TRANSFER_STATE_COMPLETE,
    TRANSFER_STATE_ERROR,
    TRANSFER_STATE_TIMEOUT
} mqtt_transfer_state_t;

/**
 * @brief Chunk message types
 */
typedef enum {
    CHUNK_TYPE_START = 1,    /**< First chunk with metadata */
    CHUNK_TYPE_DATA = 2,     /**< Data chunk */
    CHUNK_TYPE_END = 3,      /**< Final chunk */
    CHUNK_TYPE_ABORT = 4     /**< Abort transfer */
} mqtt_chunk_type_t;

/**
 * @brief Transfer metadata from START chunk
 */
typedef struct {
    char transfer_id[32];    /**< Unique transfer ID */
    uint32_t total_size;     /**< Total size of audio file */
    uint32_t chunk_size;     /**< Size of each chunk */
    uint32_t total_chunks;   /**< Total number of chunks */
    uint8_t priority;        /**< Playback priority */
    uint32_t volume;         /**< Volume level (0-100) */
    uint32_t play_count;     /**< Number of times to play */
    bool interrupt_current;  /**< Interrupt current playback */
    char filename[64];       /**< Optional filename to save */
} mqtt_transfer_metadata_t;

/**
 * @brief Active transfer tracking
 */
typedef struct {
    bool active;                          /**< Transfer slot in use */
    char transfer_id[32];                 /**< Unique transfer ID */
    mqtt_transfer_state_t state;          /**< Current transfer state */
    mqtt_transfer_metadata_t metadata;    /**< Transfer metadata */
    char temp_filename[64];               /**< Temporary file being written */
    uint32_t chunks_received;             /**< Number of chunks received */
    uint32_t bytes_received;              /**< Total bytes received */
    int64_t start_time;                   /**< Transfer start time */
    int64_t last_chunk_time;             /**< Last chunk receive time */
    struct fs_file_t file;                /**< File handle */
    uint8_t *buffer;                      /**< Optional memory buffer */
    size_t buffer_size;                   /**< Buffer size if using memory */
} mqtt_audio_transfer_t;

/**
 * @brief Chunk header structure (prepended to each chunk)
 */
typedef struct {
    uint8_t type;            /**< Chunk type (mqtt_chunk_type_t) */
    uint8_t version;         /**< Protocol version (1) */
    uint16_t reserved;       /**< Reserved for future use */
    char transfer_id[32];    /**< Transfer ID */
    uint32_t chunk_num;      /**< Chunk number (0-based) */
    uint32_t chunk_size;     /**< Size of data in this chunk */
    uint32_t crc32;          /**< CRC32 of chunk data (optional) */
} __packed mqtt_chunk_header_t;

/**
 * @brief Initialize chunked audio transfer system
 * 
 * @return 0 on success, negative error code on failure
 */
int mqtt_audio_chunked_init(void);

/**
 * @brief Handle incoming chunked audio message
 * 
 * This function should be called from the MQTT message handler
 * when a chunked audio topic is received.
 * 
 * @param topic MQTT topic
 * @param payload Message payload
 * @param payload_len Payload length
 * @return 0 on success, negative error code on failure
 */
int mqtt_audio_chunked_handle_message(const char *topic,
                                     const uint8_t *payload,
                                     size_t payload_len);

/**
 * @brief Process START chunk
 * 
 * @param transfer Transfer context
 * @param header Chunk header
 * @param data Chunk data (JSON metadata)
 * @param data_len Data length
 * @return 0 on success, negative error code on failure
 */
int mqtt_audio_chunked_process_start(mqtt_audio_transfer_t *transfer,
                                    const mqtt_chunk_header_t *header,
                                    const uint8_t *data,
                                    size_t data_len);

/**
 * @brief Process DATA chunk
 * 
 * @param transfer Transfer context
 * @param header Chunk header
 * @param data Audio data
 * @param data_len Data length
 * @return 0 on success, negative error code on failure
 */
int mqtt_audio_chunked_process_data(mqtt_audio_transfer_t *transfer,
                                   const mqtt_chunk_header_t *header,
                                   const uint8_t *data,
                                   size_t data_len);

/**
 * @brief Process END chunk
 * 
 * @param transfer Transfer context
 * @param header Chunk header
 * @return 0 on success, negative error code on failure
 */
int mqtt_audio_chunked_process_end(mqtt_audio_transfer_t *transfer,
                                  const mqtt_chunk_header_t *header);

/**
 * @brief Abort a transfer
 * 
 * @param transfer_id Transfer ID to abort (NULL to abort all)
 * @return 0 on success, negative error code on failure
 */
int mqtt_audio_chunked_abort(const char *transfer_id);

/**
 * @brief Check for timed out transfers
 * 
 * This should be called periodically to clean up stale transfers.
 * 
 * @return Number of transfers cleaned up
 */
int mqtt_audio_chunked_check_timeouts(void);

/**
 * @brief Get transfer statistics
 * 
 * @param transfer_id Transfer ID (NULL for overall stats)
 * @param active_transfers Number of active transfers
 * @param total_bytes Total bytes transferred
 * @param transfer_rate Average transfer rate in bytes/sec
 * @return 0 on success, negative error code on failure
 */
int mqtt_audio_chunked_get_stats(const char *transfer_id,
                                uint32_t *active_transfers,
                                uint64_t *total_bytes,
                                uint32_t *transfer_rate);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_AUDIO_CHUNKED_H_ */
