/**
 * @file alnicko_server.c
 * @brief Example API for interacting with an external test server over HTTP (Alnicko server).
 *
 * This module demonstrates how to interact with an external HTTP-based test server (Alnicko).
 * It provides example functions to:
 * - Retrieve and update device time (RTC) from the server,
 * - List and download audio files by name or index,
 * - Check for available firmware updates and download them,
 * - Send messages to the server via HTTP POST.
 *
 * @author Eugene K.
 * @copyright (C) 2025 Alnicko Lab OU. All rights reserved.
 */

#include "http_module.h"
#include <stdio.h>
#include <string.h>
#include <zephyr/logging/log.h>

#include "alnicko_server.h"

#define HTTP_GET_RESPONSE_BUF_LEN 512
#define SERVER_DELAY_MS           2000

#define ALNICKO_SERVER_AUDIO_LIST   "http://192.168.2.62:9001/api/files/audio"
#define ALNICKO_SERVER_FW_NAME      "http://192.168.2.62:9001/api/files/firmware"
#define ALNICKO_SERVER_GET_TIME     "http://192.168.2.62:9001/api/datetime"
#define ALNICKO_SERVER_POST_MESSAGE "http://192.168.2.62:9001/api/messages"
#define ALNICKO_SERVER_GET_FW       "http://192.168.2.62:9001/download/firmware/"
#define ALNICKO_SERVER_GET_AUDIO    "http://192.168.2.62:9001/download/audio/"

LOG_MODULE_REGISTER(alnicko_server, CONFIG_EXAMPLES_ALNICKO_SERVER_LOG_LEVEL);

/**
 * @brief Parses a JSON string to extract datetime and fill rtc_time struct.
 *
 * Searches for the "datetime" key in the given JSON string, then parses the value
 * using the format: "[YYYY-MM-DD] <HH:MM:SS>". On success, the values are stored
 * in the provided rtc_time structure.
 *
 * @param json_str Pointer to the JSON string containing the datetime field.
 * @param t Pointer to rtc_time structure to store parsed values.
 *
 * @return SERVER_OK on success,
 *         SERVER_ERR_FIELD_NOT_FOUND if the key is missing,
 *         SERVER_ERR_INVALID_FORMAT if the datetime format is incorrect.
 */
static server_status_t parse_datetime_json(const char      *json_str,
                                           struct rtc_time *t)
{
    const char *datetime_key = "\"datetime\":";
    char       *start        = strstr(json_str, datetime_key);
    if (!start)
        return SERVER_ERR_FIELD_NOT_FOUND;

    start += strlen(datetime_key);
    while (*start && (*start == ' ' || *start == '"'))
        start++;

    int y, M, d, h, m, s;
    if (sscanf(start, "[%d-%d-%d] <%d:%d:%d>", &y, &M, &d, &h, &m, &s) != 6) {
        return SERVER_ERR_INVALID_FORMAT;
    }

    t->tm_year = y;
    t->tm_mon  = M;
    t->tm_mday = d;
    t->tm_hour = h;
    t->tm_min  = m;
    t->tm_sec  = s;
    return SERVER_OK;
}

/**
 * @brief Parses a JSON string to extract the firmware name.
 *
 * Locates the "name" field in the JSON string and copies its value
 * into the provided buffer.
 *
 * @param json_str Pointer to the JSON string.
 * @param buffer Pointer to the output buffer where the name will be stored.
 * @param buffer_size Size of the output buffer.
 *
 * @return SERVER_OK on success,
 *         SERVER_ERR_FIELD_NOT_FOUND if "name" field is missing,
 *         SERVER_ERR_INVALID_FORMAT if the format is incorrect.
 */
static server_status_t
parse_fw_name_json(const char *json_str, char *buffer, size_t buffer_size)
{
    const char *name_key = "\"name\":";
    char       *start    = strstr(json_str, name_key);
    if (!start)
        return SERVER_ERR_FIELD_NOT_FOUND;

    start += strlen(name_key);
    while (*start && (*start == ' ' || *start == '"'))
        start++;

    char *end = strchr(start, '"');
    if (!end)
        return SERVER_ERR_INVALID_FORMAT;

    size_t len = end - start;
    if (len >= buffer_size)
        len = buffer_size - 1;

    memcpy(buffer, start, len);
    buffer[len] = '\0';
    return SERVER_OK;
}

/**
 * @brief Parses a JSON string containing a list of audio file names.
 *
 * Extracts the "files" array and retrieves each "name" field into a provided buffer.
 * The function also reads the total expected count from the "count" field.
 *
 * @param json_str Pointer to the JSON string.
 * @param names 2D array to store the extracted audio names.
 * @param rows Maximum number of rows (audio file names) that can be stored.
 * @param count Output pointer to the actual number of audio names parsed.
 *
 * @return SERVER_OK on success,
 *         SERVER_ERR_FIELD_NOT_FOUND if required fields are missing,
 *         SERVER_ERR_INVALID_FORMAT if JSON is malformed.
 */
static server_status_t parse_audio_list_json(const char *json_str,
                                             char (*names)[AUDIO_NAME_MAX_LEN],
                                             size_t  rows,
                                             size_t *count)
{
    const char *list_key    = "\"files\":[";
    const char *name_key    = "\"name\":";
    uint8_t     count_audio = 0;

    if (sscanf(json_str, "{\"count\":%hhu", &count_audio) != 1) {
        return SERVER_ERR_INVALID_FORMAT;
    }

    const char *list_start = strstr(json_str, list_key);
    if (!list_start)
        return SERVER_ERR_FIELD_NOT_FOUND;
    list_start += strlen(list_key);

    size_t parsed = 0;
    while (parsed < count_audio && parsed < rows) {
        const char *name_pos = strstr(list_start, name_key);
        if (!name_pos)
            break;

        name_pos += strlen(name_key);

        while (*name_pos == ' ' || *name_pos == '"')
            name_pos++;

        const char *end = strchr(name_pos, '"');
        if (!end)
            break;

        size_t len = end - name_pos;
        if (len >= AUDIO_NAME_MAX_LEN)
            len = AUDIO_NAME_MAX_LEN - 1;

        memcpy(names[parsed], name_pos, len);
        names[parsed][len] = '\0';

        list_start = end + 1;
        parsed++;
    }

    *count = parsed;
    return SERVER_OK;
}

/**
 * @brief Retrieves the current datetime from the Alnicko server.
 *
 * Sends an HTTP GET request to the predefined server endpoint and parses the
 * JSON response to extract the datetime information. The result is stored in
 * the provided rtc_time structure.
 *
 * @param t Pointer to rtc_time structure to store the retrieved time.
 *
 * @return SERVER_OK on success,
 *         SERVER_ERR_HTTP on HTTP failure,
 *         SERVER_ERR_INVALID_FORMAT or SERVER_ERR_FIELD_NOT_FOUND if JSON parsing fails.
 */
server_status_t alnicko_server_get_time(struct rtc_time *t)
{
    const char *url              = ALNICKO_SERVER_GET_TIME;
    uint16_t    http_status_code = 0;
    char        response_buf[HTTP_GET_RESPONSE_BUF_LEN] = { 0 };

    struct get_context get_ctx = {
        .response_buffer = response_buf,
        .buffer_capacity = sizeof(response_buf),
        .response_len    = 0,
    };

    http_status_t ret = http_get_request(url, &get_ctx, &http_status_code);

    if (ret != HTTP_CLIENT_OK) {
        LOG_ERR("GET failed: ret=%d", ret);
        return SERVER_ERR_HTTP;
    }

    LOG_INF("HTTP %d OK", http_status_code);
    LOG_DBG("GET: %s", response_buf);

    return parse_datetime_json(response_buf, t);
}

/**
 * @brief Updates the local RTC time using the current time from the Alnicko server.
 *
 * Retrieves the current time via an HTTP request to the server and sets
 * the local RTC clock accordingly.
 *
 * @return SERVER_OK on successful update,
 *         SERVER_ERR_HTTP on HTTP failure,
 *         SERVER_ERR_INVALID_FORMAT or SERVER_ERR_FIELD_NOT_FOUND if JSON parsing fails.
 *         SERVER_ERR_RTC_SET_FAILED if updating the RTC fails.
 */
server_status_t alnicko_server_update_time(void)
{
    struct rtc_time t;
    server_status_t res = alnicko_server_get_time(&t);

    if (res != SERVER_OK)
        return res;

    int ret = set_date_time(&t);
    if (ret < 0) {
        LOG_ERR("RTC update failed");
        return SERVER_ERR_RTC_SET_FAILED;
    }
    return SERVER_OK;
}

/**
 * @brief Retrieves the latest firmware update filename from the Alnicko server.
 *
 * Sends an HTTP GET request to a predefined firmware name endpoint and
 * parses the JSON response to extract the firmware file name.
 *
 * @param buffer        Pointer to the buffer where the firmware name will be stored.
 * @param buffer_size   Size of the buffer in bytes.
 *
 * @return SERVER_OK on success,
 *         SERVER_ERR_HTTP on HTTP failure,
 *         SERVER_ERR_FIELD_NOT_FOUND or SERVER_ERR_INVALID_FORMAT if parsing fails.
 */
server_status_t alnicko_server_get_update_fw_name(char  *buffer,
                                                  size_t buffer_size)
{
    const char *url              = ALNICKO_SERVER_FW_NAME;
    uint16_t    http_status_code = 0;
    char        response_buf[HTTP_GET_RESPONSE_BUF_LEN] = { 0 };

    struct get_context get_ctx = {
        .response_buffer = response_buf,
        .buffer_capacity = sizeof(response_buf),
        .response_len    = 0,
    };

    http_status_t ret = http_get_request(url, &get_ctx, &http_status_code);

    if (ret != HTTP_CLIENT_OK) {
        LOG_ERR("GET failed: ret=%d", ret);
        return SERVER_ERR_HTTP;
    }

    LOG_INF("HTTP %d OK", http_status_code);
    LOG_DBG("GET: %s", response_buf);

    return parse_fw_name_json(response_buf, buffer, buffer_size);
}

/**
 * @brief Downloads the latest firmware update from the Alnicko server.
 *
 * First retrieves the firmware filename using a GET request to the firmware name endpoint.
 * Then constructs the full firmware download URL and sends an HTTP request to download the file.
 *
 * @return SERVER_OK on success,
 *         SERVER_ERR_HTTP on download failure or name retrieval failure.
 *         SERVER_ERR_FIELD_NOT_FOUND or SERVER_ERR_INVALID_FORMAT if parsing fails.
 */
server_status_t alnicko_server_get_update_fw(void)
{
    char fw_name[FW_UPDATE_NAME_MAX_LEN] = { 0 };

    server_status_t res =
            alnicko_server_get_update_fw_name(fw_name, sizeof(fw_name));

    if (res != SERVER_OK)
        return res;

    char url[CONFIG_RPR_HTTP_MAX_URL_LENGTH];
    snprintf(url, sizeof(url), "%s%s", ALNICKO_SERVER_GET_FW, fw_name);

    LOG_INF("Downloading update from: %s", url);

    uint16_t http_status_code = 0;

    k_msleep(SERVER_DELAY_MS);

    http_status_t ret = http_download_update_request(url, &http_status_code);

    if (ret == HTTP_CLIENT_OK) {
        LOG_INF("Firmware update downloaded successfully. HTTP status: %d",
                http_status_code);
    } else {
        LOG_ERR("Update download failed. Error code: %d", ret);
        return SERVER_ERR_HTTP;
    }
    return SERVER_OK;
}

/**
 * @brief Retrieves a list of available audio files from the Alnicko server.
 *
 * Sends an HTTP GET request to the server's audio list endpoint and parses the response
 * to extract audio filenames into the provided buffer. The number of parsed entries
 * is returned via the `count` parameter.
 *
 * @param names         Output buffer to store audio filenames.
 * @param rows          Maximum number of filenames to store.
 * @param count         Pointer to variable to receive the number of files parsed.
 *
 * @return SERVER_OK on success.
 *         SERVER_ERR_HTTP on name list retrieval failure.
 *         SERVER_ERR_INVALID_FORMAT or SERVER_ERR_FIELD_NOT_FOUND if JSON parsing fails.
 */
server_status_t alnicko_server_get_audio_list(char (*names)[AUDIO_NAME_MAX_LEN],
                                              size_t  rows,
                                              size_t *count)
{
    const char *url              = ALNICKO_SERVER_AUDIO_LIST;
    uint16_t    http_status_code = 0;
    char        response_buf[HTTP_GET_RESPONSE_BUF_LEN] = { 0 };

    struct get_context get_ctx = {
        .response_buffer = response_buf,
        .buffer_capacity = sizeof(response_buf),
        .response_len    = 0,
    };

    http_status_t ret = http_get_request(url, &get_ctx, &http_status_code);
    if (ret != HTTP_CLIENT_OK) {
        LOG_ERR("GET failed: ret=%d", ret);
        return SERVER_ERR_HTTP;
    }

    LOG_INF("HTTP %d OK", http_status_code);
    LOG_DBG("GET: %s", response_buf);

    return parse_audio_list_json(response_buf, names, rows, count);
}

/**
 * @brief Downloads an audio file from the Alnicko server by its filename.
 *
 * Constructs the full URL using the provided filename, then sends an HTTP GET request
 * to download the audio file.
 *
 * @param filename      Name of the audio file to download.
 *
 * @return SERVER_OK on success, SERVER_ERR_HTTP on failure.
 */
server_status_t alnicko_server_get_audio_by_name(const char *filename)
{
    char url[CONFIG_RPR_HTTP_MAX_URL_LENGTH];
    snprintf(url, sizeof(url), "%s%s", ALNICKO_SERVER_GET_AUDIO, filename);

    LOG_INF("Downloading audio from: %s", url);
    uint16_t http_status_code = 0;

    http_status_t ret = http_download_file_request(
            url, CONFIG_RPR_AUDIO_DEFAULT_PATH, &http_status_code);

    if (ret == HTTP_CLIENT_OK) {
        LOG_INF("Download audio successful. Status code: %d", http_status_code);
        return SERVER_OK;
    } else {
        LOG_ERR("Download failed. Error code: %d", ret);
        return SERVER_ERR_HTTP;
    }
}

/**
 * @brief Downloads an audio file from the Alnicko server by its index in the list.
 *
 * Retrieves the full list of audio filenames from the server, then downloads
 * the audio file corresponding to the given index.
 *
 * @param num      Index of the audio file in the server's list.
 *
 * @return SERVER_OK on success,
 *         SERVER_ERR_INVALID_FORMAT or SERVER_ERR_FIELD_NOT_FOUND if JSON parsing fails.
 *         SERVER_ERR_INVALID_INDEX if the index is out of bounds,
 *         SERVER_ERR_HTTP on name list retrieval failure download failure.
 */
server_status_t alnicko_server_get_audio_by_num(uint8_t num)
{
    char   names[AUDIO_FILES_MAX][AUDIO_NAME_MAX_LEN];
    size_t count = 0;

    server_status_t res =
            alnicko_server_get_audio_list(names, AUDIO_FILES_MAX, &count);

    if (res != SERVER_OK)
        return res;

    k_msleep(SERVER_DELAY_MS);

    if (num > count || num == 0)
        return SERVER_ERR_INVALID_INDEX;

    return alnicko_server_get_audio_by_name(names[--num]);
}

/**
 * @brief Sends a user-defined message to the Alnicko server via HTTP POST.
 *
 * Constructs a JSON payload from the provided message and sends it to the server
 * using an HTTP POST request. Logs the status and response.
 *
 * @param msg  String message to be sent in the POST body.
 *
 * @return SERVER_OK on success,
 *         SERVER_ERR_HTTP if the HTTP request failed.
 */
server_status_t alnicko_server_post_message(const char *msg)
{
    const char *url              = ALNICKO_SERVER_POST_MESSAGE;
    uint16_t    http_status_code = 0;
    char        response_buf[HTTP_GET_RESPONSE_BUF_LEN] = { 0 };

    char payload[POST_PAYLOAD_MAX_SIZE];

    snprintf(payload, sizeof(payload), "{\"message\":\"%s\"}", msg);

    const char *post_headers[] = { "Content-Type: application/json\r\n", NULL };

    struct post_context post_ctx = {
        .response = {
            .response_buffer = response_buf,
            .buffer_capacity = sizeof(response_buf),
            .response_len = 0,
        },
        .payload = payload,
        .payload_len = strlen(payload),
        .headers = post_headers,
    };

    LOG_INF("Sending POST request to: %s", url);

    http_status_t ret = http_post_request(url, &post_ctx, &http_status_code);
    if (ret == HTTP_CLIENT_OK) {
        LOG_INF("HTTP %d OK", http_status_code);
        LOG_DBG("POST response: %s", response_buf);
        return SERVER_OK;
    } else {
        LOG_ERR("POST failed: ret=%d", ret);
        return SERVER_ERR_HTTP;
    }
}
