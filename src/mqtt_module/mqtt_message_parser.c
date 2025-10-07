/**
 * @file mqtt_message_parser.c
 * @brief Implementation of MQTT message parser for mixed JSON/binary payloads (RDP-119)
 */

#include "mqtt_message_parser.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

LOG_MODULE_REGISTER(mqtt_parser, CONFIG_RPR_MODULE_MQTT_LOG_LEVEL);

/* Using cJSON library instead of Zephyr's JSON parser for better compatibility */

int mqtt_parse_json_only(const char *json_str, size_t json_len,
                        mqtt_parsed_message_t *parsed_msg)
{
    if (!json_str || !parsed_msg || json_len == 0) {
        LOG_ERR("Invalid parameters");
        return MQTT_PARSER_ERR_INVALID_PARAM;
    }
    
    /* Clear output structure */
    memset(parsed_msg, 0, sizeof(mqtt_parsed_message_t));
    
    /* Parse JSON metadata directly */
    int ret = mqtt_parse_json_metadata(json_str, &parsed_msg->metadata);
    if (ret < 0) {
        LOG_ERR("Failed to parse JSON metadata: %d", ret);
        return ret;
    }
    
    /* Store JSON header size */
    parsed_msg->metadata.json_header_size = json_len;
    
    /* For JSON-only messages, opus data is stored in file */
    parsed_msg->opus_data = NULL;
    parsed_msg->opus_data_len = 0;
    parsed_msg->valid = true;
    
    return MQTT_PARSER_SUCCESS;
}

/* Static buffer to avoid stack overflow (1KB+ is too much for worker thread stack) */
static char json_parse_buffer[MQTT_MSG_MAX_JSON_HEADER_SIZE + 1];

int mqtt_parse_message(const uint8_t *payload, size_t payload_len, 
                      mqtt_parsed_message_t *parsed_msg)
{
    const uint8_t *json_end = NULL;
    int json_len;
    
    if (!payload || !parsed_msg || payload_len == 0) {
        return MQTT_PARSER_ERR_INVALID_PARAM;
    }
    
    /* Clear output structure */
    memset(parsed_msg, 0, sizeof(mqtt_parsed_message_t));
    
    /* Extract JSON header */
    json_len = mqtt_extract_json_header(payload, payload_len, &json_end);
    
    if (json_len < 0) {
        printk("X_err\n");
        return json_len;
    }
    
    if (json_len > MQTT_MSG_MAX_JSON_HEADER_SIZE) {
        printk("X_big\n");
        return MQTT_PARSER_ERR_JSON_TOO_LONG;
    }
    
    /* Copy JSON to null-terminated buffer (skip 4-byte length prefix) */
    if (json_len >= sizeof(json_parse_buffer) || json_len <= 0) {
        return MQTT_PARSER_ERR_JSON_TOO_LONG;
    }
    
    /* Copy JSON data */
    memcpy(json_parse_buffer, payload + 4, json_len);
    json_parse_buffer[json_len] = '\0';
    
    /* Parse JSON metadata */
    int ret = mqtt_parse_json_metadata(json_parse_buffer, &parsed_msg->metadata);
    if (ret < 0) {
        LOG_ERR("Failed to parse JSON metadata: %d", ret);
        return ret;
    }
    
    /* Store JSON header size */
    parsed_msg->metadata.json_header_size = json_len;
    
    /* Calculate Opus data position and validate size (4-byte prefix + JSON) */
    size_t opus_offset = 4 + json_len;
    size_t remaining_len = payload_len - opus_offset;
    
    if (remaining_len < parsed_msg->metadata.opus_data_size) {
        /* This might be a file-based audio message where opus data is stored separately */
        if (remaining_len == 0 && parsed_msg->metadata.opus_data_size > 0) {
            LOG_WRN("No opus data in payload, expected %u bytes (likely stored in file)",
                    parsed_msg->metadata.opus_data_size);
            parsed_msg->opus_data = NULL;
            parsed_msg->opus_data_len = 0;
            parsed_msg->valid = true;
            return MQTT_PARSER_SUCCESS;
        }
        LOG_ERR("Opus data size mismatch: expected %u, available %zu",
                parsed_msg->metadata.opus_data_size, remaining_len);
        return MQTT_PARSER_ERR_SIZE_MISMATCH;
    }
    
    /* Set Opus data pointer and length */
    parsed_msg->opus_data = payload + opus_offset;
    parsed_msg->opus_data_len = parsed_msg->metadata.opus_data_size;
    parsed_msg->valid = true;
    
    LOG_INF("Parsed MQTT message: JSON=%d bytes, Opus=%zu bytes, priority=%d",
            json_len, parsed_msg->opus_data_len, parsed_msg->metadata.priority);
    
    return MQTT_PARSER_SUCCESS;
}

int mqtt_extract_json_header(const uint8_t *payload, size_t payload_len, 
                            const uint8_t **json_end)
{
    /* New protocol: first 4 bytes are hex length of JSON */
    if (payload_len < 4) {
        return MQTT_PARSER_ERR_TOO_SHORT;
    }
    
    /* Parse 4-byte hex length prefix */
    char len_str[5];
    memcpy(len_str, payload, 4);
    len_str[4] = '\0';
    
    char *endptr;
    unsigned long json_len = strtoul(len_str, &endptr, 16);
    
    if (endptr != len_str + 4) {
        LOG_ERR("Invalid length prefix: %.4s", len_str);
        return MQTT_PARSER_ERR_INVALID_JSON;
    }
    
    if (json_len > MQTT_MSG_MAX_JSON_HEADER_SIZE) {
        LOG_ERR("JSON header too long: %lu bytes", json_len);
        return MQTT_PARSER_ERR_JSON_TOO_LONG;
    }
    
    if (payload_len < 4 + json_len) {
        LOG_ERR("Payload too short for JSON: need %lu, have %zu", 4 + json_len, payload_len);
        return MQTT_PARSER_ERR_TOO_SHORT;
    }
    
    /* Verify JSON starts after length prefix */
    if (payload[4] != '{') {
        LOG_ERR("JSON doesn't start with '{' at offset 4");
        return MQTT_PARSER_ERR_NO_JSON;
    }
    
    if (json_end) {
        *json_end = payload + 4 + json_len;
    }
    
    return (int)json_len;
}

int mqtt_parse_json_metadata(const char *json_str, 
                            mqtt_message_metadata_t *metadata)
{
    if (!json_str || !metadata) {
        return MQTT_PARSER_ERR_INVALID_PARAM;
    }
    
    /* Set defaults */
    memset(metadata, 0, sizeof(mqtt_message_metadata_t));
    metadata->priority = 5;
    metadata->play_count = 1;
    metadata->volume = 40;
    metadata->interrupt_current = false;
    metadata->save_to_file = false;
    
    /* Parse JSON using cJSON library */
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        LOG_ERR("cJSON parse failed");
        return MQTT_PARSER_ERR_INVALID_JSON;
    }
    
    /* Extract required field: opusDataSize */
    cJSON *size = cJSON_GetObjectItem(root, "opusDataSize");
    if (cJSON_IsNumber(size)) {
        metadata->opus_data_size = (uint32_t)size->valuedouble;
    }
    
    /* Extract optional fields */
    cJSON *priority = cJSON_GetObjectItem(root, "priority");
    if (cJSON_IsNumber(priority)) {
        metadata->priority = (uint8_t)priority->valuedouble;
    }
    
    cJSON *volume = cJSON_GetObjectItem(root, "volume");
    if (cJSON_IsNumber(volume)) {
        metadata->volume = (uint32_t)volume->valuedouble;
        if (metadata->volume > 100) {
            metadata->volume = 100;
        }
    }
    
    cJSON *playCount = cJSON_GetObjectItem(root, "playCount");
    if (cJSON_IsNumber(playCount)) {
        metadata->play_count = (uint32_t)playCount->valuedouble;
    }
    
    cJSON *saveToFile = cJSON_GetObjectItem(root, "saveToFile");
    if (cJSON_IsBool(saveToFile)) {
        metadata->save_to_file = cJSON_IsTrue(saveToFile);
    }
    
    cJSON *interruptCurrent = cJSON_GetObjectItem(root, "interruptCurrent");
    if (cJSON_IsBool(interruptCurrent)) {
        metadata->interrupt_current = cJSON_IsTrue(interruptCurrent);
    }
    
    cJSON *filename = cJSON_GetObjectItem(root, "filename");
    if (cJSON_IsString(filename) && filename->valuestring) {
        strncpy(metadata->filename, filename->valuestring, sizeof(metadata->filename) - 1);
    }
    
    /* Validate required field */
    if (metadata->opus_data_size == 0) {
        cJSON_Delete(root);
        LOG_ERR("Missing required field: opus_data_size");
        return MQTT_PARSER_ERR_MISSING_FIELD;
    }
    
    cJSON_Delete(root);
    return MQTT_PARSER_SUCCESS;
}

#if 0  /* OLD manual parsing code - removed */
int mqtt_parse_json_metadata_OLD(const char *json_str, 
                            mqtt_message_metadata_t *metadata)
{
    int ret;
    
    if (ret < 0) {
        /* Zephyr's JSON parser rejects boolean 'false' values - use manual parser */
        
        /* Try manual parsing for required fields at minimum */
        char *size_str = strstr(json_str, "\"opusDataSize\"");
        if (size_str) {
            size_str = strchr(size_str, ':');
            if (size_str) {
                metadata->opus_data_size = strtoul(size_str + 1, NULL, 10);
            }
        }
        
        /* Parse volume manually */
        char *vol_str = strstr(json_str, "\"volume\"");
        if (vol_str) {
            vol_str = strchr(vol_str, ':');
            if (vol_str) {
                metadata->volume = strtoul(vol_str + 1, NULL, 10);
                if (metadata->volume > 100) {
                    metadata->volume = 100;
                }
            }
        }
        
        /* Parse priority manually */
        char *pri_str = strstr(json_str, "\"priority\"");
        if (pri_str) {
            pri_str = strchr(pri_str, ':');
            if (pri_str) {
                metadata->priority = strtoul(pri_str + 1, NULL, 10);
            }
        }
        
        /* Parse playCount manually */
        char *count_str = strstr(json_str, "\"playCount\"");
        if (count_str) {
            count_str = strchr(count_str, ':');
            if (count_str) {
                metadata->play_count = strtoul(count_str + 1, NULL, 10);
            }
        }
        
        /* Parse filename manually */
        char *filename_str = strstr(json_str, "\"filename\"");
        if (filename_str) {
            filename_str = strchr(filename_str, ':');
            if (filename_str) {
                /* Skip whitespace and quote */
                filename_str++;
                while (*filename_str == ' ' || *filename_str == '"') filename_str++;
                
                /* Find end quote */
                char *end_quote = strchr(filename_str, '"');
                if (end_quote) {
                    size_t len = end_quote - filename_str;
                    if (len > sizeof(metadata->filename) - 1) {
                        len = sizeof(metadata->filename) - 1;
                    }
                    memcpy(metadata->filename, filename_str, len);
                    metadata->filename[len] = '\0';
                }
            }
        }
        
        /* Parse boolean fields manually */
        char *save_str = strstr(json_str, "\"saveToFile\"");
        if (save_str) {
            save_str = strchr(save_str, ':');
            if (save_str) {
                /* Skip whitespace */
                save_str++;
                while (*save_str == ' ') save_str++;
                metadata->save_to_file = (strncmp(save_str, "true", 4) == 0);
            }
        }
        
        char *interrupt_str = strstr(json_str, "\"interruptCurrent\"");
        if (interrupt_str) {
            interrupt_str = strchr(interrupt_str, ':');
            if (interrupt_str) {
                /* Skip whitespace */
                interrupt_str++;
                while (*interrupt_str == ' ') interrupt_str++;
                metadata->interrupt_current = (strncmp(interrupt_str, "true", 4) == 0);
            }
        }
        
        if (metadata->opus_data_size == 0) {
            LOG_ERR("Missing required field: opus_data_size");
            return MQTT_PARSER_ERR_MISSING_FIELD;
        }
        
        /* Manual parsing succeeded */
        return MQTT_PARSER_SUCCESS;
    }
    
    /* Validate parsed data */
    if (metadata->opus_data_size == 0) {
        LOG_ERR("Invalid opus_data_size: 0");
        return MQTT_PARSER_ERR_MISSING_FIELD;
    }
    
    if (metadata->volume > 100) {
        LOG_WRN("Volume %u > 100, clamping to 100", metadata->volume);
        metadata->volume = 100;
    }
    
    LOG_DBG("Parsed metadata: size=%u, priority=%u, save=%d, file=%s, count=%u, vol=%u",
            metadata->opus_data_size, metadata->priority, 
            metadata->save_to_file, metadata->filename,
            metadata->play_count, metadata->volume);
    
    return MQTT_PARSER_SUCCESS;
}
#endif  /* End old manual parsing code */

bool mqtt_validate_json_header(const char *json_str, size_t json_len)
{
    if (!json_str || json_len < 2) {
        return false;
    }
    
    /* Basic validation: starts with { and ends with } */
    if (json_str[0] != '{' || json_str[json_len - 1] != '}') {
        return false;
    }
    
    /* Check for required field */
    if (!strstr(json_str, "opus_data_size")) {
        return false;
    }
    
    return true;
}

const char *mqtt_parser_error_string(int error_code)
{
    switch (error_code) {
        case MQTT_PARSER_SUCCESS:
            return "Success";
        case MQTT_PARSER_ERR_INVALID_PARAM:
            return "Invalid parameter";
        case MQTT_PARSER_ERR_TOO_SHORT:
            return "Payload too short";
        case MQTT_PARSER_ERR_NO_JSON:
            return "No JSON header found";
        case MQTT_PARSER_ERR_INVALID_JSON:
            return "Invalid JSON format";
        case MQTT_PARSER_ERR_MISSING_FIELD:
            return "Missing required field";
        case MQTT_PARSER_ERR_SIZE_MISMATCH:
            return "Opus data size mismatch";
        case MQTT_PARSER_ERR_JSON_TOO_LONG:
            return "JSON header too long";
        default:
            return "Unknown error";
    }
}
