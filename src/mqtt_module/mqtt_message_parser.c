/**
 * @file mqtt_message_parser.c
 * @brief Implementation of MQTT message parser for mixed JSON/binary payloads (RDP-119)
 */

#include "mqtt_message_parser.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <zephyr/logging/log.h>
#include <zephyr/data/json.h>

LOG_MODULE_REGISTER(mqtt_parser, CONFIG_RPR_MODULE_MQTT_LOG_LEVEL);

/* JSON parsing descriptors for Zephyr's JSON library */
static const struct json_obj_descr metadata_descr[] = {
    JSON_OBJ_DESCR_PRIM(mqtt_message_metadata_t, opus_data_size, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(mqtt_message_metadata_t, priority, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_ARRAY(mqtt_message_metadata_t, filename, 64, filename, JSON_TOK_STRING),
    JSON_OBJ_DESCR_PRIM(mqtt_message_metadata_t, play_count, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(mqtt_message_metadata_t, volume, JSON_TOK_NUMBER),
};

int mqtt_parse_message(const uint8_t *payload, size_t payload_len, 
                      mqtt_parsed_message_t *parsed_msg)
{
    const uint8_t *json_end = NULL;
    int json_len;
    char json_buffer[MQTT_MSG_MAX_JSON_HEADER_SIZE + 1];
    
    if (!payload || !parsed_msg || payload_len == 0) {
        LOG_ERR("Invalid parameters");
        return MQTT_PARSER_ERR_INVALID_PARAM;
    }
    
    /* Clear output structure */
    memset(parsed_msg, 0, sizeof(mqtt_parsed_message_t));
    
    /* Extract JSON header */
    json_len = mqtt_extract_json_header(payload, payload_len, &json_end);
    if (json_len < 0) {
        LOG_ERR("Failed to extract JSON header: %d", json_len);
        return json_len;
    }
    
    if (json_len > MQTT_MSG_MAX_JSON_HEADER_SIZE) {
        LOG_ERR("JSON header too long: %d bytes", json_len);
        return MQTT_PARSER_ERR_JSON_TOO_LONG;
    }
    
    /* Copy JSON to null-terminated buffer */
    memcpy(json_buffer, payload, json_len);
    json_buffer[json_len] = '\0';
    
    LOG_DBG("Extracted JSON header (%d bytes): %s", json_len, json_buffer);
    
    /* Parse JSON metadata */
    int ret = mqtt_parse_json_metadata(json_buffer, &parsed_msg->metadata);
    if (ret < 0) {
        LOG_ERR("Failed to parse JSON metadata: %d", ret);
        return ret;
    }
    
    /* Store JSON header size */
    parsed_msg->metadata.json_header_size = json_len;
    
    /* Calculate Opus data position and validate size */
    size_t opus_offset = json_len;
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
    int brace_count = 0;
    bool in_string = false;
    bool escape_next = false;
    size_t i;
    
    if (!payload || payload_len < 2) {
        return MQTT_PARSER_ERR_TOO_SHORT;
    }
    
    /* JSON must start with '{' */
    if (payload[0] != '{') {
        LOG_ERR("Payload does not start with JSON object");
        return MQTT_PARSER_ERR_NO_JSON;
    }
    
    /* Find the end of JSON by counting braces, respecting string literals */
    for (i = 0; i < payload_len && i < MQTT_MSG_MAX_JSON_HEADER_SIZE; i++) {
        char c = (char)payload[i];
        
        if (escape_next) {
            escape_next = false;
            continue;
        }
        
        if (c == '\\') {
            escape_next = true;
            continue;
        }
        
        if (c == '"' && !escape_next) {
            in_string = !in_string;
            continue;
        }
        
        if (!in_string) {
            if (c == '{') {
                brace_count++;
            } else if (c == '}') {
                brace_count--;
                if (brace_count == 0) {
                    /* Found end of JSON object */
                    if (json_end) {
                        *json_end = payload + i + 1;
                    }
                    return i + 1;  /* Return length including closing brace */
                }
            }
        }
    }
    
    LOG_ERR("No valid JSON object found in first %d bytes", i);
    return MQTT_PARSER_ERR_INVALID_JSON;
}

int mqtt_parse_json_metadata(const char *json_str, 
                            mqtt_message_metadata_t *metadata)
{
    int ret;
    
    if (!json_str || !metadata) {
        return MQTT_PARSER_ERR_INVALID_PARAM;
    }
    
    /* Set defaults */
    memset(metadata, 0, sizeof(mqtt_message_metadata_t));
    metadata->priority = 5;        /* Default medium priority */
    metadata->play_count = 1;      /* Play once by default */
    metadata->volume = 40;         /* 40% volume by default */
    metadata->interrupt_current = false;
    metadata->save_to_file = false;
    
    /* Parse JSON using Zephyr's JSON library */
    ret = json_obj_parse((char *)json_str, strlen(json_str),
                        metadata_descr, ARRAY_SIZE(metadata_descr),
                        metadata);
    
    if (ret < 0) {
        LOG_ERR("JSON parsing failed: %d", ret);
        
        /* Try manual parsing for required fields at minimum */
        char *size_str = strstr(json_str, "\"opus_data_size\"");
        if (size_str) {
            size_str = strchr(size_str, ':');
            if (size_str) {
                metadata->opus_data_size = strtoul(size_str + 1, NULL, 10);
            }
        }
        
        /* Parse boolean fields manually */
        char *save_str = strstr(json_str, "\"save_to_file\"");
        if (save_str) {
            save_str = strchr(save_str, ':');
            if (save_str) {
                /* Skip whitespace */
                save_str++;
                while (*save_str == ' ') save_str++;
                metadata->save_to_file = (strncmp(save_str, "true", 4) == 0);
            }
        }
        
        char *interrupt_str = strstr(json_str, "\"interrupt_current\"");
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
        
        /* Even if full parsing failed, we can proceed with just the size */
        LOG_WRN("Using partial metadata, opus_data_size=%u", metadata->opus_data_size);
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
