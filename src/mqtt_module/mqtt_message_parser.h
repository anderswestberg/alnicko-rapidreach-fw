/**
 * @file mqtt_message_parser.h
 * @brief MQTT message parser for mixed JSON/binary payloads (RDP-119)
 * 
 * Parses MQTT messages containing a JSON header with metadata followed by
 * binary Opus audio data. The JSON header contains size information and
 * other metadata for proper audio playback.
 */

#ifndef MQTT_MESSAGE_PARSER_H
#define MQTT_MESSAGE_PARSER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maximum size for JSON header in MQTT message
 */
#define MQTT_MSG_MAX_JSON_HEADER_SIZE 1024

/**
 * @brief MQTT message metadata extracted from JSON header
 */
typedef struct {
    uint32_t opus_data_size;     /**< Size of Opus audio data in bytes */
    uint8_t priority;            /**< Message priority (0-255, higher = more important) */
    bool save_to_file;           /**< Whether to save audio to file */
    char filename[64];           /**< Filename if save_to_file is true */
    uint32_t play_count;         /**< Number of times to play audio (0 = infinite) */
    uint32_t volume;             /**< Volume level (0-100) */
    bool interrupt_current;      /**< Whether to interrupt currently playing audio */
    uint32_t json_header_size;   /**< Size of the JSON header itself */
} mqtt_message_metadata_t;

/**
 * @brief Parsed MQTT message containing metadata and audio data pointers
 */
typedef struct {
    mqtt_message_metadata_t metadata;  /**< Extracted metadata from JSON header */
    const uint8_t *opus_data;         /**< Pointer to Opus audio data */
    size_t opus_data_len;             /**< Actual length of Opus data */
    bool valid;                       /**< Whether parsing was successful */
} mqtt_parsed_message_t;

/**
 * @brief Parse MQTT message with JSON header and binary Opus data
 * 
 * Extracts JSON metadata and provides pointer to binary Opus data.
 * The message format is: [JSON header][binary Opus data]
 * 
 * @param payload Raw MQTT message payload
 * @param payload_len Total length of payload
 * @param parsed_msg Output structure containing parsed data
 * @return 0 on success, negative error code on failure
 */
int mqtt_parse_message(const uint8_t *payload, size_t payload_len, 
                      mqtt_parsed_message_t *parsed_msg);

/**
 * @brief Parse JSON-only metadata (for pre-extracted JSON from chunked messages)
 * 
 * @param json_str Null-terminated JSON string
 * @param json_len Length of the JSON string
 * @param parsed_msg Output structure to store parsed data
 * @return MQTT_PARSER_SUCCESS on success, error code otherwise
 */
int mqtt_parse_json_only(const char *json_str, size_t json_len,
                        mqtt_parsed_message_t *parsed_msg);

/**
 * @brief Validate JSON header format and required fields
 * 
 * @param json_str JSON string to validate
 * @param json_len Length of JSON string
 * @return true if valid, false otherwise
 */
bool mqtt_validate_json_header(const char *json_str, size_t json_len);

/**
 * @brief Extract JSON header from mixed payload
 * 
 * Finds the end of JSON header (looking for closing brace followed by binary data)
 * 
 * @param payload Raw payload data
 * @param payload_len Total payload length
 * @param json_end Output: pointer to end of JSON header
 * @return Length of JSON header, or -1 on error
 */
int mqtt_extract_json_header(const uint8_t *payload, size_t payload_len, 
                            const uint8_t **json_end);

/**
 * @brief Parse metadata from JSON header string
 * 
 * @param json_str JSON header string (null-terminated)
 * @param metadata Output metadata structure
 * @return 0 on success, negative error code on failure
 */
int mqtt_parse_json_metadata(const char *json_str, 
                            mqtt_message_metadata_t *metadata);

/**
 * @brief Get human-readable error string for parser errors
 * 
 * @param error_code Error code from parser functions
 * @return Error description string
 */
const char *mqtt_parser_error_string(int error_code);

/* Error codes */
#define MQTT_PARSER_SUCCESS           0
#define MQTT_PARSER_ERR_INVALID_PARAM -1
#define MQTT_PARSER_ERR_TOO_SHORT     -2
#define MQTT_PARSER_ERR_NO_JSON       -3
#define MQTT_PARSER_ERR_INVALID_JSON  -4
#define MQTT_PARSER_ERR_MISSING_FIELD -5
#define MQTT_PARSER_ERR_SIZE_MISMATCH -6
#define MQTT_PARSER_ERR_JSON_TOO_LONG -7

#ifdef __cplusplus
}
#endif

#endif /* MQTT_MESSAGE_PARSER_H */
