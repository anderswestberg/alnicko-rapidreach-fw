/**
 * @file http_module.h
 * @brief HTTP client module for performing GET, POST, and file download operations over HTTP/HTTPS.
 *
 * This module provides a simple interface for sending HTTP GET and POST requests,
 * as well as downloading files to the local filesystem via HTTP or HTTPS.
 * It handles:
 *  - URL parsing and protocol selection (HTTP/HTTPS)
 *  - TLS setup using mbedTLS (if HTTPS is enabled)
 *  - Socket connection and timeout configuration
 *  - Response parsing and buffer management
 *  - Optional file storage and hash computation for downloads
 *
 * The module supports both text-based API usage (GET/POST with in-memory buffers)
 * and binary file download via the Zephyr filesystem API.
 * 
 * @author Eugene K.
 * @copyright (C) 2025 Alnicko Lab OU. All rights reserved.
 */

#ifndef _HTTP_MODULE_H_
#define _HTTP_MODULE_H_

#include <stddef.h>
#include <stdint.h>

typedef enum {
    HTTP_CLIENT_OK = 0,
    HTTP_ERR_INVALID_PARAM,
    HTTP_ERR_TLS_FAIL,
    HTTP_ERR_TLS_NOT_SUPPORTED,
    HTTP_ERR_NO_FILENAME,
    HTTP_ERR_FILE_OPEN,
    HTTP_ERR_DFU_INIT,
    HTTP_ERR_DFU_HASH_NOT_MATCH,
    HTTP_ERR_CLIENT_REQUEST,
    HTTP_BAD_STATUS_CODE,
    HTTP_URL_OK           = 0,
    HTTP_ERR_PARSE_URL    = -1,
    HTTP_SOCK_OK          = 0,
    HTTP_ERR_ADDR_RESOLVE = -2,
    HTTP_ERR_SOCK_CONNECT = -3
} http_status_t;

struct get_context {
    char  *response_buffer;
    size_t buffer_capacity;
    size_t response_len;
};

struct post_context {
    struct get_context response;
    const char        *payload;
    size_t             payload_len;
    const char       **headers;
};

/**
 * @brief Downloads a file from the specified HTTP/HTTPS URL and saves it to the local filesystem.
 *
 * This function performs an HTTP GET request to the given URL and writes the response body
 * directly to a file under the provided `base_dir` directory. It automatically handles 
 * TLS setup for HTTPS URLs, certificate registration, socket connection, and file creation.
 *
 * If enabled, also computes and prints the SHA-256 hash of the downloaded content.
 *
 * @param url               Full HTTP or HTTPS URL of the file to download.
 * @param base_dir          Base folder path where the file will be saved.
 * @param http_status_code  Pointer to store the HTTP response status code.
 *
 * @return HTTP_CLIENT_OK on success, or an appropriate `http_status_t` error code on failure.
 */
http_status_t http_download_file_request(const char *url,
                                         const char *base_dir,
                                         uint16_t   *http_status_code);

/**
 * @brief Sends an HTTP GET request to the specified URL and stores the response in a buffer.
 *
 * This function performs a GET request using the provided `url` and writes the response body
 * into the buffer defined in `get_ctx`. The HTTP status code is written to `http_status_code`.
 *
 * @param url               The target HTTP or HTTPS URL.
 * @param get_ctx           Pointer to a get_context structure containing the buffer and its size.
 * @param http_status_code  Pointer to a variable to store the HTTP response status code.
 *
 * @return HTTP_CLIENT_OK on success, or an appropriate `http_status_t` error code on failure.
 */
http_status_t http_get_request(const char         *url,
                               struct get_context *get_ctx,
                               uint16_t           *http_status_code);

/**
 * @brief Sends an HTTP POST request to the specified URL with a payload and receives the response.
 *
 * This function sends a POST request using the given `url` and payload specified in `post_ctx`.
 * It stores the response body in the `response_buffer` defined within the context, and sets the
 * HTTP status code in the variable pointed to by `http_status_code`.
 *
 * @param url               The HTTP or HTTPS endpoint.
 * @param post_ctx          Pointer to a post_context structure containing payload, headers, and response buffer.
 * @param http_status_code  Pointer to a variable to store the resulting HTTP status code.
 *
 * @return HTTP_CLIENT_OK on success, or an appropriate `http_status_t` error code on failure.
 */
http_status_t http_post_request(const char          *url,
                                struct post_context *post_ctx,
                                uint16_t            *http_status_code);

#ifdef CONFIG_RPR_MODULE_DFU
/**
 * @brief Download and flash a firmware update via HTTP(S).
 *
 * Performs an HTTP GET request to download a firmware image from the specified
 * URL and writes the data directly into the device's DFU (Device Firmware Upgrade)
 * flash partition. Optionally computes the SHA-256 hash of the downloaded image and
 * verifies it against the hash of the flashed image if integrity check is enabled.
 *
 * @param url               Full HTTP or HTTPS URL of the file to download.
 * @param http_status_code  Pointer to store the HTTP response status code.
 *
 * @return HTTP_CLIENT_OK on success, or an appropriate `http_status_t` error code on failure.
 */
http_status_t http_download_update_request(const char *url,
                                           uint16_t   *http_status_code);
#endif //CONFIG_RPR_MODULE_DFU

#endif /* _HTTP_MODULE_H_ */