/**
 * @file alnicko_server.h
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

#ifndef ALNICKO_SERVER_H
#define ALNICKO_SERVER_H

#include "rtc.h"

#define AUDIO_NAME_MAX_LEN     128
#define AUDIO_FILES_MAX        16
#define FW_UPDATE_NAME_MAX_LEN 128
#define POST_PAYLOAD_MAX_SIZE  256

typedef enum {
    SERVER_OK                  = 0,
    SERVER_ERR_HTTP            = -1,
    SERVER_ERR_FIELD_NOT_FOUND = -2,
    SERVER_ERR_INVALID_FORMAT  = -3,
    SERVER_ERR_RTC_SET_FAILED  = -4,
    SERVER_ERR_INVALID_INDEX   = -5
} server_status_t;

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
server_status_t alnicko_server_get_time(struct rtc_time *t);

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
server_status_t alnicko_server_update_time(void);

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
                                              size_t *count);

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
server_status_t alnicko_server_get_audio_by_name(const char *filename);

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
server_status_t alnicko_server_get_audio_by_num(uint8_t num);

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
                                                  size_t buffer_size);

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
server_status_t alnicko_server_get_update_fw(void);

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
server_status_t alnicko_server_post_message(const char *msg);

#endif // ALNICKO_SERVER_H
