/**
 * @file test_mqtt_parser.c
 * @brief Unit tests for MQTT message parser
 * 
 * This can be compiled as a standalone test or integrated with Zephyr's test framework.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stddef.h>

/* For standalone testing, provide minimal stubs */
#ifndef __ZEPHYR__
#define LOG_MODULE_REGISTER(name, level)
#define LOG_ERR(fmt, ...) printf("ERROR: " fmt "\n", ##__VA_ARGS__)
#define LOG_WRN(fmt, ...) printf("WARN: " fmt "\n", ##__VA_ARGS__)
#define LOG_INF(fmt, ...) printf("INFO: " fmt "\n", ##__VA_ARGS__)
#define LOG_DBG(fmt, ...) printf("DEBUG: " fmt "\n", ##__VA_ARGS__)

/* Minimal JSON parsing stub for testing without Zephyr */
#define JSON_TOK_NUMBER 1
#define JSON_TOK_TRUE 2
#define JSON_TOK_STRING 3
#define JSON_OBJ_DESCR_PRIM(type, field, tok) {#field, offsetof(type, field), tok}
#define JSON_OBJ_DESCR_ARRAY(type, field, max, arr, tok) {#field, offsetof(type, field), tok}
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

struct json_obj_descr {
    const char *field_name;
    size_t offset;
    int type;
};

/* Stub implementation - in real code this comes from Zephyr */
int json_obj_parse(char *json, size_t len, const struct json_obj_descr *descr, 
                   size_t descr_len, void *obj) {
    /* Simple parser for testing - just extract opus_data_size */
    char *size_str = strstr(json, "\"opus_data_size\"");
    if (size_str) {
        size_str = strchr(size_str, ':');
        if (size_str) {
            uint32_t *size_field = (uint32_t *)((char *)obj + descr[0].offset);
            *size_field = strtoul(size_str + 1, NULL, 10);
            return 0;
        }
    }
    return -1;
}
#endif

/* Temporarily disable Zephyr includes for standalone testing */
#define TESTING_STANDALONE
#include "mqtt_message_parser.h"

/* Include implementation inline for testing, with Zephyr deps disabled */
#ifdef TESTING_STANDALONE
#undef LOG_MODULE_REGISTER
#define LOG_MODULE_REGISTER(name, level)
/* Skip Zephyr-specific headers in parser implementation */
#define mqtt_message_parser_c_impl
#include <string.h>
#include <stdio.h>

/* Re-include the parser implementation with stubs */
static const struct json_obj_descr metadata_descr[] = {};

#include "mqtt_message_parser.c"
#endif

/* Test data */
static const uint8_t test_opus_data[] = {
    0x4f, 0x67, 0x67, 0x53, 0x00, 0x02, 0x00, 0x00,  /* OggS header */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    /* ... more Opus data ... */
};

static void test_basic_parsing(void)
{
    printf("\n=== Test: Basic Message Parsing ===\n");
    
    /* Create test message with JSON header + Opus data */
    char test_payload[1024];
    int json_len = snprintf(test_payload, sizeof(test_payload),
        "{"
        "\"opus_data_size\":%zu,"
        "\"priority\":10,"
        "\"save_to_file\":true,"
        "\"filename\":\"alert.opus\","
        "\"play_count\":3,"
        "\"volume\":85,"
        "\"interrupt_current\":true"
        "}", sizeof(test_opus_data));
    
    /* Append Opus data */
    memcpy(test_payload + json_len, test_opus_data, sizeof(test_opus_data));
    size_t total_len = json_len + sizeof(test_opus_data);
    
    /* Parse the message */
    mqtt_parsed_message_t parsed;
    int ret = mqtt_parse_message((uint8_t *)test_payload, total_len, &parsed);
    
    assert(ret == MQTT_PARSER_SUCCESS);
    assert(parsed.valid == true);
    assert(parsed.metadata.opus_data_size == sizeof(test_opus_data));
    assert(parsed.metadata.priority == 10);
    assert(parsed.metadata.volume == 85);
    assert(parsed.metadata.play_count == 3);
    assert(parsed.opus_data == (uint8_t *)test_payload + json_len);
    assert(parsed.opus_data_len == sizeof(test_opus_data));
    
    printf("✓ Basic parsing test passed\n");
}

static void test_minimal_json(void)
{
    printf("\n=== Test: Minimal JSON Header ===\n");
    
    /* Test with only required field */
    char test_payload[256];
    int json_len = snprintf(test_payload, sizeof(test_payload),
        "{\"opus_data_size\":100}");
    
    /* Add dummy Opus data */
    memset(test_payload + json_len, 0xAA, 100);
    
    mqtt_parsed_message_t parsed;
    int ret = mqtt_parse_message((uint8_t *)test_payload, json_len + 100, &parsed);
    
    assert(ret == MQTT_PARSER_SUCCESS);
    assert(parsed.metadata.opus_data_size == 100);
    assert(parsed.opus_data_len == 100);
    
    printf("✓ Minimal JSON test passed\n");
}

static void test_invalid_json(void)
{
    printf("\n=== Test: Invalid JSON Handling ===\n");
    
    /* Test 1: No opening brace */
    const char *bad_json1 = "\"opus_data_size\":50}";
    mqtt_parsed_message_t parsed;
    int ret = mqtt_parse_message((uint8_t *)bad_json1, strlen(bad_json1), &parsed);
    assert(ret == MQTT_PARSER_ERR_NO_JSON);
    
    /* Test 2: Unclosed JSON */
    const char *bad_json2 = "{\"opus_data_size\":50";
    ret = mqtt_parse_message((uint8_t *)bad_json2, strlen(bad_json2), &parsed);
    assert(ret == MQTT_PARSER_ERR_INVALID_JSON);
    
    /* Test 3: Missing required field */
    const char *bad_json3 = "{\"priority\":5}xxx";
    ret = mqtt_parse_message((uint8_t *)bad_json3, strlen(bad_json3), &parsed);
    assert(ret == MQTT_PARSER_ERR_MISSING_FIELD);
    
    printf("✓ Invalid JSON tests passed\n");
}

static void test_size_mismatch(void)
{
    printf("\n=== Test: Size Mismatch Handling ===\n");
    
    /* JSON claims 1000 bytes but only 50 available */
    char test_payload[100];
    int json_len = snprintf(test_payload, sizeof(test_payload),
        "{\"opus_data_size\":1000}");
    
    mqtt_parsed_message_t parsed;
    int ret = mqtt_parse_message((uint8_t *)test_payload, json_len + 50, &parsed);
    assert(ret == MQTT_PARSER_ERR_SIZE_MISMATCH);
    
    printf("✓ Size mismatch test passed\n");
}

static void test_json_extraction(void)
{
    printf("\n=== Test: JSON Extraction ===\n");
    
    /* Test extraction with nested objects and strings */
    const char *complex_json = 
        "{"
        "\"metadata\":{"
            "\"version\":\"1.0\","
            "\"escaped\\\"quote\":true"
        "},"
        "\"opus_data_size\":10"
        "}BINARYDATA";
    
    const uint8_t *json_end;
    int json_len = mqtt_extract_json_header((uint8_t *)complex_json, 
                                           strlen(complex_json), &json_end);
    
    /* Should find the closing } before BINARYDATA */
    assert(json_len > 0);
    assert(json_end == (uint8_t *)complex_json + json_len);
    assert(memcmp(json_end, "BINARYDATA", 10) == 0);
    
    printf("✓ JSON extraction test passed\n");
}

static void test_error_strings(void)
{
    printf("\n=== Test: Error String Lookup ===\n");
    
    assert(strcmp(mqtt_parser_error_string(MQTT_PARSER_SUCCESS), "Success") == 0);
    assert(strcmp(mqtt_parser_error_string(MQTT_PARSER_ERR_INVALID_PARAM), "Invalid parameter") == 0);
    assert(strcmp(mqtt_parser_error_string(-999), "Unknown error") == 0);
    
    printf("✓ Error string tests passed\n");
}

int main(void)
{
    printf("MQTT Message Parser Unit Tests\n");
    printf("==============================\n");
    
    test_basic_parsing();
    test_minimal_json();
    test_invalid_json();
    test_size_mismatch();
    test_json_extraction();
    test_error_strings();
    
    printf("\n✅ All tests passed!\n\n");
    return 0;
}

#ifdef __ZEPHYR__
/* Zephyr test integration */
#include <zephyr/ztest.h>

ZTEST_SUITE(mqtt_parser_tests, NULL, NULL, NULL, NULL, NULL);

ZTEST(mqtt_parser_tests, test_all)
{
    test_basic_parsing();
    test_minimal_json();
    test_invalid_json();
    test_size_mismatch();
    test_json_extraction();
    test_error_strings();
}
#endif
