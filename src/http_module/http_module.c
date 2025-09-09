/**
 * @file http_module.c
 * @brief HTTP client module for performing GET, POST, and file/update download operations over HTTP/HTTPS.
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

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/http/client.h>

#include "http_module.h"

#ifdef CONFIG_MBEDTLS
#include "mbedtls/md.h"
#endif

#ifdef CONFIG_RPR_MODULE_DFU
#include "dfu_manager.h"
#endif

#ifdef CONFIG_NET_SOCKETS_SOCKOPT_TLS
#include <zephyr/net/tls_credentials.h>
#include "ca_certificate.h"
#endif

LOG_MODULE_REGISTER(http_module, CONFIG_RPR_MODULE_HTTP_LOG_LEVEL);

#define HTTP_TIMEOUT_MS       (180 * MSEC_PER_SEC)
#define SOCKET_TIMEOUT_MS     (5 * MSEC_PER_SEC)
#define ADDRINFO_TIMEOUT_MS   (2 * MSEC_PER_SEC)
#define RESOLVE_ATTEMPTS      5
#define HTTP_STATUS_OK        200
#define bytes2KiB(Bytes)      (Bytes / (1024u))
#define HASH_SIZE_MAX_LEN     32
#define HTTPS_PORT            "443"
#define HTTP_PORT             "80"
#define HTTP_PATH_SHIFT_RIGHT 1
#define INTERNAL_SERVER_ERROR 500

#define FULL_FILE_PATH_MAX_LEN \
    (CONFIG_RPR_FOLDER_PATH_MAX_LEN + CONFIG_RPR_FILENAME_MAX_LEN)

typedef enum {
    HTTP_CTX_NONE = 0,
    HTTP_CTX_DOWNLOAD,
    HTTP_CTX_UPDATE,
    HTTP_CTX_GET,
    HTTP_CTX_POST,
} http_context_type_t;

struct url_context {
    const char *host;
    const char *port;
    char        url[CONFIG_RPR_HTTP_MAX_URL_LENGTH];
    bool        is_tls;
    const char *path;
};

#ifdef CONFIG_RPR_HASH_CALCULATION
struct http_dwn_hash_context {
    char                     response_hash[HASH_SIZE_MAX_LEN];
    mbedtls_md_context_t     hash_ctx;
    const mbedtls_md_info_t *hash_info;
};
#endif

struct download_context {
    const char      *folder_path;
    char             filepath[FULL_FILE_PATH_MAX_LEN];
    size_t           filesize;
    struct fs_file_t file;
#ifdef CONFIG_RPR_HASH_CALCULATION
    struct http_dwn_hash_context dwn_hash_ctx;
#endif
};

struct update_context {
    size_t filesize;
#ifdef CONFIG_RPR_MODULE_DFU
    struct dfu_storage_context dfu_ctx;
#endif
#ifdef CONFIG_RPR_HASH_CALCULATION
    struct http_dwn_hash_context dwn_hash_ctx;
#endif
};

union http_context_union {
    struct download_context *download;
    struct update_context   *update;
    struct get_context      *get;
    struct post_context     *post;
};

struct http_context {
    http_context_type_t      type;
    struct url_context       url_context;
    uint16_t                *http_status_code;
    union http_context_union ctx;
};

#ifdef CONFIG_RPR_HASH_CALCULATION

/**
 * @brief Prints the hexadecimal representation of a hash.
 *
 * @param p   Pointer to the binary hash data.
 * @param len Length of the hash data in bytes.
 */
void print_hash(const unsigned char *p, int len)
{
    if (!p || len <= 0) {
        LOG_WRN("Invalid hash input");
        return;
    }

    char hash_str[2 * len + 1];

    for (int i = 0; i < len; i++) {
        sprintf(&hash_str[i * 2], "%02x", p[i]);
    }

    hash_str[2 * len] = '\0';

    LOG_INF("Hash: %s", hash_str);
}
#endif

/**
 * @brief Logs detailed information from a given addrinfo structure.
 *
 * @param ai Pointer to the addrinfo structure to log.
 */
static void dbg_print_addrinfo(const struct addrinfo *ai)
{
    if (!ai) {
        LOG_WRN("Invalid addrinfo input");
        return;
    }

    LOG_DBG(" addrinfo @%p: ai_family=%d, "
            "ai_socktype=%d, "
            "ai_protocol=%d, "
            "sa_family=%d, "
            "sin_port=%x",
            ai,
            ai->ai_family,
            ai->ai_socktype,
            ai->ai_protocol,
            ai->ai_addr->sa_family,
            ((struct sockaddr_in *)ai->ai_addr)->sin_port);
}

/**
 * @brief Registers TLS certificates required for HTTPS connections.
 *
 * This function loads all certificates from the `ca_certificates[]` array and registers them.
 *
 * @return 0 on success, or negative error code on failure.
 */
static int http_register_certificates(void)
{
#ifdef CONFIG_NET_SOCKETS_SOCKOPT_TLS
    int ret = -1;

    for (int i = 0; i < ARRAY_SIZE(ca_certificates); i++) {
        ret = tls_credential_add(CA_CERTIFICATE_TAG + i,
                                 TLS_CREDENTIAL_CA_CERTIFICATE,
                                 ca_certificates[i],
                                 strlen(ca_certificates[i]));
        if (ret < 0) {
            LOG_ERR("Failed to register certificate %d (err: %d)", i, ret);
            return ret;
        }
    }

    LOG_INF("TLS credentials added successfully");
#else
    LOG_INF("TLS not supported in build");
#endif
    return 0;
}

SYS_INIT(http_register_certificates,
         APPLICATION,
         CONFIG_APPLICATION_INIT_PRIORITY);

/**
 * @brief Parses the given URL and fills the HTTP context with its components.
 *
 * Extracts the scheme (HTTP/HTTPS), host, port, and path from the provided URL,
 * and updates the corresponding fields in the `http_context` structure.
 * If the context type is HTTP_CTX_DOWNLOAD, also constructs a local file path
 * based on the URL and folder path.
 *
 * @param url        The input URL string.
 * @param http_ctx   Pointer to the HTTP context to populate.
 *
 * @return HTTP_URL_OK on success, or a corresponding error code from `http_status_t`.
 */
static http_status_t parse_url(const char *url, struct http_context *http_ctx)
{

    if (!url || !http_ctx) {
        LOG_ERR("Invalid arguments for parse url");
        return HTTP_ERR_INVALID_PARAM;
    }

    char               *host_ptr, *path_ptr, *port_ptr;
    struct url_context *ctx = &http_ctx->url_context;

    char *url_copy = ctx->url;

    if (strlen(url) >= CONFIG_RPR_HTTP_MAX_URL_LENGTH - HTTP_PATH_SHIFT_RIGHT) {
        LOG_ERR("URL too long. Increase CONFIG_RPR_HTTP_MAX_URL_LENGTH");
        return HTTP_ERR_PARSE_URL;
    }

    strncpy(url_copy, url, CONFIG_RPR_HTTP_MAX_URL_LENGTH);
    url_copy[CONFIG_RPR_HTTP_MAX_URL_LENGTH - 1] = '\0';

    if (strncmp(url_copy, "http://", strlen("http://")) == 0) {
        ctx->is_tls = false;
        /* Parse host part */
        host_ptr = url_copy + strlen("http://");
#ifdef CONFIG_NET_SOCKETS_SOCKOPT_TLS
    } else if (strncmp(url_copy, "https://", strlen("https://")) == 0) {
        ctx->is_tls = true;
        /* Parse host part */
        host_ptr = url_copy + strlen("https://");
#endif
    } else {
        LOG_ERR("Only http "
#ifdef CONFIG_NET_SOCKETS_SOCKOPT_TLS
                "and https "
#endif
                "URLs are supported");
        return HTTP_ERR_PARSE_URL;
    }

    /* Parse path part */
    path_ptr = strchr(host_ptr, '/');
    if (path_ptr) {
        // Shift right by 1 to insert '/'
        size_t path_size = strlen(path_ptr) + 1; // include '\0'
        if ((path_ptr - url_copy) + path_size + HTTP_PATH_SHIFT_RIGHT <
            CONFIG_RPR_HTTP_MAX_URL_LENGTH) {
            memmove(path_ptr + HTTP_PATH_SHIFT_RIGHT, path_ptr, path_size);
            *path_ptr = '\0';
            path_ptr++;

        } else {
            LOG_ERR("URL too long after inserting slash");
            return HTTP_ERR_PARSE_URL;
        }

    } else {
        path_ptr = "";
    }

    /* Store optional port part */
    port_ptr = strchr(host_ptr, ':');
    if (port_ptr) {
        *port_ptr = '\0';
        port_ptr++;
    } else {
        port_ptr = ctx->is_tls ? HTTPS_PORT : HTTP_PORT;
    }

    LOG_DBG("Parsing url for http%s://%s:%s%s",
            (ctx->is_tls ? "s" : ""),
            host_ptr,
            port_ptr,
            path_ptr);

    ctx->host = host_ptr;
    ctx->port = port_ptr;
    ctx->path = path_ptr;

    if (http_ctx->type == HTTP_CTX_DOWNLOAD) {
        const char              *filename = strrchr(path_ptr, '/');
        struct download_context *dl_ctx   = http_ctx->ctx.download;

        filename = filename ? filename + 1 : path_ptr;
        if (*filename == '\0') {
            LOG_ERR("Filename could not be determined from URL");
            return HTTP_ERR_NO_FILENAME;
        }
        snprintf(dl_ctx->filepath,
                 FULL_FILE_PATH_MAX_LEN,
                 "%s/%s",
                 dl_ctx->folder_path,
                 filename);

        LOG_DBG("Download to: %s", dl_ctx->filepath);
    }

    return HTTP_URL_OK;
}

/**
 * @brief Resolves the remote host and establishes a TCP (or with TLS) socket connection.
 *
 * @param sock       Pointer to the socket file descriptor to be initialized.
 * @param http_ctx   Pointer to the HTTP context containing connection parameters.
 *
 * @return HTTP_SOCK_OK on success, or a corresponding error code from `http_status_t`
 */
static http_status_t connect_socket(int *sock, struct http_context *http_ctx)
{

    if (!sock || !http_ctx) {
        LOG_ERR("Invalid arguments for connect socket");
        return HTTP_ERR_INVALID_PARAM;
    }

    struct url_context *ctx = &http_ctx->url_context;

    struct addrinfo hints   = { .ai_family   = AF_INET,
                                .ai_socktype = SOCK_STREAM };
    struct timeval  timeout = { .tv_sec = SOCKET_TIMEOUT_MS };

    struct addrinfo *res = NULL;
    int              ret = -1;

    for (int i = 0; i <= RESOLVE_ATTEMPTS; i++) {

        ret = getaddrinfo(ctx->host, ctx->port, &hints, &res);
        if (ret == 0)
            break;

        LOG_WRN("Status getaddrinfo: %d, retrying...", ret);
        k_msleep(ADDRINFO_TIMEOUT_MS);
    }

    if (ret != 0) {
        LOG_ERR("Unable to resolve address");
        return HTTP_ERR_ADDR_RESOLVE;
    }

    dbg_print_addrinfo(res);

    if (ctx->is_tls) {
#ifdef CONFIG_NET_SOCKETS_SOCKOPT_TLS
        *sock = socket(res->ai_family, res->ai_socktype, IPPROTO_TLS_1_2);
#else
        LOG_ERR("TLS not supported");
        goto err_free_addrinfo;
#endif
    } else {
        *sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    }

    if (*sock < 0) {
        LOG_ERR("Failed to create socket (%d)", *sock);
        goto err_free_addrinfo;
    }

    LOG_DBG("Socket is %d", *sock);

#ifdef CONFIG_NET_SOCKETS_SOCKOPT_TLS
    if (ctx->is_tls) {
        sec_tag_t sec_tag_opt[ARRAY_SIZE(ca_certificates)];
        for (int i = 0; i < ARRAY_SIZE(ca_certificates); i++) {
            sec_tag_opt[i] = CA_CERTIFICATE_TAG + i;
        }

        ret = setsockopt(*sock,
                         SOL_TLS,
                         TLS_SEC_TAG_LIST,
                         sec_tag_opt,
                         sizeof(sec_tag_opt));
        if (ret < 0) {
            LOG_ERR("Failed to set TLS_SEC_TAG_LIST (%d)", ret);
            goto err_close_socket;
        }

        ret = setsockopt(
                *sock, SOL_TLS, TLS_HOSTNAME, ctx->host, strlen(ctx->host) + 1);
        if (ret < 0) {
            LOG_ERR("Failed to set TLS_HOSTNAME (%d)", ret);
            goto err_close_socket;
        }
    }
#endif

    ret = setsockopt(*sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    if (ret < 0) {
        LOG_ERR("Failed to set SO_RCVTIMEO (%d)", ret);
        goto err_close_socket;
    }

    ret = setsockopt(*sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    if (ret < 0) {
        LOG_ERR("Failed to set SO_SNDTIMEO (%d)", ret);
        goto err_close_socket;
    }

    ret = connect(*sock, res->ai_addr, res->ai_addrlen);
    if (ret < 0) {
        LOG_ERR("Cannot connect to remote (%d)", ret);
        goto err_close_socket;
    }

    freeaddrinfo(res);
    return HTTP_SOCK_OK;

err_close_socket:
    close(*sock);
err_free_addrinfo:
    freeaddrinfo(res);
    return HTTP_ERR_SOCK_CONNECT;
}

/**
 * @brief HTTP response callback function for handling incoming data fragments.
 * It handles three types of HTTP context: GET, POST, and DOWNLOAD.
 *
 * - For GET and POST: Appends received body fragment to the provided response buffer.
 * - For DOWNLOAD: Writes the received fragment directly to a file via `fs_write()`.
 * - For UPDATE: Writes data directly to the DFU (firmware upgrade) storage and updates progress.
 *
 * If hash calculation is enabled via `CONFIG_RPR_HASH_CALCULATION`, it updates the SHA-256 digest
 * incrementally for `DOWNLOAD` and `UPDATE` contexts.
 *
 * @param rsp         Pointer to the received HTTP response fragment.
 * @param final_data  Indicates if this is the final callback call for the request.
 * @param user_data   Pointer to the associated `http_context` used for request and buffer management.
 */
static void response_cb(struct http_response *rsp,
                        enum http_final_call  final_data,
                        void                 *user_data)
{
    if (!rsp || !user_data || !rsp->body_found)
        return;

    struct http_context *http_ctx = user_data;

    if (http_ctx->type == HTTP_CTX_GET || http_ctx->type == HTTP_CTX_POST) {

        struct get_context *rsp_ctx;

        if (http_ctx->type == HTTP_CTX_GET) {
            rsp_ctx = http_ctx->ctx.get;
        } else {
            rsp_ctx = &http_ctx->ctx.post->response;
        }
        size_t available = rsp_ctx->buffer_capacity - rsp_ctx->response_len;

        if (available >= rsp->body_frag_len) {
            memcpy(&rsp_ctx->response_buffer[rsp_ctx->response_len],
                   rsp->body_frag_start,
                   rsp->body_frag_len);
            rsp_ctx->response_len += rsp->body_frag_len;
        } else {
            LOG_WRN("Response buffer overflow, truncating response");
        }
    } else if (http_ctx->type == HTTP_CTX_DOWNLOAD) {
        struct download_context *ctx = http_ctx->ctx.download;

        int ret =
                fs_write(&ctx->file, rsp->body_frag_start, rsp->body_frag_len);

        if (ret >= 0) {
            ctx->filesize += ret;
        } else {
            LOG_ERR("fs_write failed: %d", ret);
        }
#ifdef CONFIG_RPR_HASH_CALCULATION
        mbedtls_md_update(&ctx->dwn_hash_ctx.hash_ctx,
                          rsp->body_frag_start,
                          rsp->body_frag_len);
#endif
    } else if (http_ctx->type == HTTP_CTX_UPDATE) {
        struct update_context *ctx = http_ctx->ctx.update;
        int                    ret = -1;

#ifdef CONFIG_RPR_MODULE_DFU
        ret = dfu_storage_write(&ctx->dfu_ctx,
                                rsp->body_frag_start,
                                rsp->body_frag_len,
                                final_data);
#endif

        if (ret == 0) {
            ctx->filesize += rsp->body_frag_len;
        } else {
            LOG_ERR("dfu_storage_write failed: %d", ret);
        }
#ifdef CONFIG_RPR_HASH_CALCULATION
        mbedtls_md_update(&ctx->dwn_hash_ctx.hash_ctx,
                          rsp->body_frag_start,
                          rsp->body_frag_len);
#endif
    } else
        LOG_ERR("Unknown context type in response_cb");

    *http_ctx->http_status_code = rsp->http_status_code;

    if (final_data == HTTP_DATA_FINAL && http_ctx->http_status_code) {

        *http_ctx->http_status_code = rsp->http_status_code;
        LOG_INF("HTTP status: %s (%d)",
                rsp->http_status,
                rsp->http_status_code);
    }
}

/**
 * @brief Initializes and prepares an HTTP client connection.
 *
 * Parses the provided URL and attempts to connect a socket.
 * This function supports both HTTP and HTTPS connections, based on the scheme in the URL.
 *
 * @param url        The full HTTP or HTTPS URL to connect to.
 * @param sock       Pointer to an integer where the connected socket file descriptor will be stored.
 * @param http_ctx   Pointer to the HTTP context structure holding URL components and state.
 *
 * @return HTTP_CLIENT_OK on success, or appropriate `http_status_t` error code on failure.
 */
static http_status_t
setup_http_client(const char *url, int *sock, struct http_context *http_ctx)
{
    if (!url || !sock || !http_ctx) {
        LOG_ERR("Invalid arguments for setup http client");
        return HTTP_ERR_INVALID_PARAM;
    }

    http_status_t ret_status;
    LOG_DBG("Setting up HTTP client for URL: %s", url);

    ret_status = parse_url(url, http_ctx);

    if (ret_status != HTTP_URL_OK) {
        LOG_ERR("URL parsing failed (code %d)", ret_status);
        return ret_status;
    }

    ret_status = connect_socket(sock, http_ctx);
    if (ret_status != HTTP_SOCK_OK) {
        LOG_ERR("Failed to connect socket (code %d)", ret_status);
        return ret_status;
    }
    LOG_DBG("HTTP client setup completed successfully");
    return HTTP_CLIENT_OK;
}

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
                                         uint16_t   *http_status_code)
{

    if (!url || !base_dir || !http_status_code) {
        LOG_ERR("Invalid arguments for DOWNLOAD request");
        return HTTP_ERR_INVALID_PARAM;
    }

    if (strlen(base_dir) >= CONFIG_RPR_FOLDER_PATH_MAX_LEN) {
        LOG_ERR("Folder path is too long");
        return HTTP_ERR_INVALID_PARAM;
    }

    http_status_t ret_status;
    int           sock = -1;

    struct download_context dl_ctx = {
        .filesize    = 0,
        .folder_path = base_dir,
    };

    struct http_context ctx = {
        .type             = HTTP_CTX_DOWNLOAD,
        .http_status_code = http_status_code,
        .ctx.download     = &dl_ctx,

    };

    ret_status = setup_http_client(url, &sock, &ctx);

    if (ret_status != HTTP_CLIENT_OK) {
        LOG_ERR("HTTP setup failed (code %d)", ret_status);
        return ret_status;
    }

#ifdef CONFIG_RPR_HASH_CALCULATION
    struct http_dwn_hash_context *dwn_hash_ctx = &dl_ctx.dwn_hash_ctx;
    dwn_hash_ctx->hash_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!dwn_hash_ctx->hash_info) {
        LOG_ERR("Unable to request hash type from mbedTLS");
    }

    mbedtls_md_init(&dwn_hash_ctx->hash_ctx);
    if (mbedtls_md_setup(&dwn_hash_ctx->hash_ctx, dwn_hash_ctx->hash_info, 0) <
        0) {
        LOG_ERR("Can't setup mbedTLS hash engine");
    }
#endif

    uint8_t recv_buf[CONFIG_RPR_HTTP_RECV_BUFFER_SIZE];

    struct http_request req = {
        .method       = HTTP_GET,
        .url          = ctx.url_context.path,
        .host         = ctx.url_context.host,
        .protocol     = "HTTP/1.1",
        .response     = response_cb,
        .recv_buf     = recv_buf,
        .recv_buf_len = sizeof(recv_buf),
    };

    fs_file_t_init(&dl_ctx.file);
    if (fs_open(&dl_ctx.file, dl_ctx.filepath, FS_O_CREATE | FS_O_WRITE) < 0) {
        LOG_ERR("Failed to open file for writing: %s", dl_ctx.filepath);
        return HTTP_ERR_FILE_OPEN;
    }

#ifdef CONFIG_RPR_HASH_CALCULATION
    mbedtls_md_starts(&dwn_hash_ctx->hash_ctx);
#endif

    LOG_INF("Starting file download...");

    *http_status_code = INTERNAL_SERVER_ERROR;

    int ret = http_client_req(sock, &req, HTTP_TIMEOUT_MS, &ctx);
    close(sock);
    fs_close(&dl_ctx.file);

#ifdef CONFIG_RPR_HASH_CALCULATION
    mbedtls_md_finish(&dwn_hash_ctx->hash_ctx, dwn_hash_ctx->response_hash);
    mbedtls_md_free(&dwn_hash_ctx->hash_ctx);
#endif

    if (ret < 0) {
        LOG_ERR("HTTP client request failed with code %d", ret);
        fs_unlink(dl_ctx.filepath);
        return HTTP_ERR_CLIENT_REQUEST;
    }

    if (*http_status_code != HTTP_STATUS_OK) {
        if (*http_status_code == INTERNAL_SERVER_ERROR)
            LOG_WRN("Unexpected internal server error: %d", *http_status_code);
        else
            LOG_WRN("Unexpected HTTP status: %d", *http_status_code);

        fs_unlink(dl_ctx.filepath);
        return HTTP_BAD_STATUS_CODE;
    }

    LOG_INF("Download complete. Size: %u Bytes (%u KiB)",
            dl_ctx.filesize,
            bytes2KiB(dl_ctx.filesize));

#ifdef CONFIG_RPR_HASH_CALCULATION
    int mbedtls_hash_len = mbedtls_md_get_size(dwn_hash_ctx->hash_info);
    print_hash(dwn_hash_ctx->response_hash, mbedtls_hash_len);
#endif

    return HTTP_CLIENT_OK;
}

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
                               uint16_t           *http_status_code)
{

    if (!url || !get_ctx || !get_ctx->response_buffer || !http_status_code ||
        get_ctx->buffer_capacity == 0) {
        LOG_ERR("Invalid arguments for GET request");
        return HTTP_ERR_INVALID_PARAM;
    }

    http_status_t ret_status;
    int           sock = -1;

    struct http_context ctx = {
        .type             = HTTP_CTX_GET,
        .http_status_code = http_status_code,
        .ctx.get          = get_ctx,
    };

    get_ctx->response_len = 0;

    ret_status = setup_http_client(url, &sock, &ctx);
    if (ret_status != HTTP_CLIENT_OK) {
        LOG_ERR("HTTP setup failed (code %d)", ret_status);
        return ret_status;
    }

    uint8_t recv_buf[CONFIG_RPR_HTTP_RECV_BUFFER_SIZE];

    struct http_request req = {
        .method        = HTTP_GET,
        .url           = ctx.url_context.path,
        .host          = ctx.url_context.host,
        .protocol      = "HTTP/1.1",
        .header_fields = get_ctx->headers,  /* Add custom headers support */
        .response      = response_cb,
        .recv_buf      = recv_buf,
        .recv_buf_len  = sizeof(recv_buf),
    };

    LOG_INF("Sending GET request...");
    *http_status_code = INTERNAL_SERVER_ERROR;
    int ret           = http_client_req(sock, &req, HTTP_TIMEOUT_MS, &ctx);
    close(sock);

    if (ret < 0) {
        LOG_ERR("HTTP GET failed: %d", ret);
        return HTTP_ERR_CLIENT_REQUEST;
    }

    if (*http_status_code != HTTP_STATUS_OK) {
        if (*http_status_code == INTERNAL_SERVER_ERROR)
            LOG_WRN("Unexpected internal server error: %d", *http_status_code);
        else
            LOG_WRN("Unexpected HTTP status: %d", *http_status_code);

        return HTTP_BAD_STATUS_CODE;
    }

    LOG_INF("GET request successful. Received %zu bytes.",
            ctx.ctx.get->response_len);

    return HTTP_CLIENT_OK;
}

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
                                uint16_t            *http_status_code)
{
    if (!url || !post_ctx || !post_ctx->response.response_buffer ||
        post_ctx->response.buffer_capacity == 0 || !post_ctx->payload ||
        !http_status_code) {
        LOG_ERR("Invalid arguments for POST request");
        return HTTP_ERR_INVALID_PARAM;
    }

    http_status_t ret_status;
    int           sock = -1;

    struct http_context ctx = {
        .type             = HTTP_CTX_POST,
        .http_status_code = http_status_code,
        .ctx.post         = post_ctx,
    };

    post_ctx->response.response_len = 0;

    ret_status = setup_http_client(url, &sock, &ctx);
    if (ret_status != HTTP_CLIENT_OK) {
        LOG_ERR("HTTP setup failed (code %d)", ret_status);
        return ret_status;
    }

    uint8_t recv_buf[CONFIG_RPR_HTTP_RECV_BUFFER_SIZE];

    struct http_request req = {
        .method        = HTTP_POST,
        .url           = ctx.url_context.path,
        .host          = ctx.url_context.host,
        .protocol      = "HTTP/1.1",
        .payload       = post_ctx->payload,
        .payload_len   = post_ctx->payload_len,
        .header_fields = post_ctx->headers,
        .response      = response_cb,
        .recv_buf      = recv_buf,
        .recv_buf_len  = sizeof(recv_buf),
    };

    LOG_INF("Sending POST request...");

    *http_status_code = INTERNAL_SERVER_ERROR;

    int ret = http_client_req(sock, &req, HTTP_TIMEOUT_MS, &ctx);
    close(sock);

    if (ret < 0) {
        LOG_ERR("HTTP POST failed: %d", ret);
        return HTTP_ERR_CLIENT_REQUEST;
    }

    if (*http_status_code != HTTP_STATUS_OK) {
        if (*http_status_code == INTERNAL_SERVER_ERROR)
            LOG_WRN("Unexpected internal server error: %d", *http_status_code);
        else
            LOG_WRN("Unexpected HTTP status: %d", *http_status_code);

        return HTTP_BAD_STATUS_CODE;
    }

    LOG_INF("POST request successful. Received %zu bytes.",
            post_ctx->response.response_len);

    return HTTP_CLIENT_OK;
}

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
                                           uint16_t   *http_status_code)
{

    if (!url || !http_status_code) {
        LOG_ERR("Invalid arguments for download UPDATE request");
        return HTTP_ERR_INVALID_PARAM;
    }

    http_status_t ret_status;
    int           sock = -1;

    struct update_context upd_ctx = {
        .filesize = 0,
    };

    struct http_context ctx = {
        .type             = HTTP_CTX_UPDATE,
        .http_status_code = http_status_code,
        .ctx.update       = &upd_ctx,

    };

    ret_status = setup_http_client(url, &sock, &ctx);

    if (ret_status != HTTP_CLIENT_OK) {
        LOG_ERR("HTTP setup failed (code %d)", ret_status);
        return ret_status;
    }

#ifdef CONFIG_RPR_HASH_CALCULATION
    struct http_dwn_hash_context *dwn_hash_ctx = &upd_ctx.dwn_hash_ctx;
    dwn_hash_ctx->hash_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!dwn_hash_ctx->hash_info) {
        LOG_ERR("Unable to request hash type from mbedTLS");
    }

    mbedtls_md_init(&dwn_hash_ctx->hash_ctx);
    if (mbedtls_md_setup(&dwn_hash_ctx->hash_ctx, dwn_hash_ctx->hash_info, 0) <
        0) {
        LOG_ERR("Can't setup mbedTLS hash engine");
    }
#endif

    uint8_t recv_buf[CONFIG_RPR_HTTP_RECV_BUFFER_SIZE];

    struct http_request req = {
        .method       = HTTP_GET,
        .url          = ctx.url_context.path,
        .host         = ctx.url_context.host,
        .protocol     = "HTTP/1.1",
        .response     = response_cb,
        .recv_buf     = recv_buf,
        .recv_buf_len = sizeof(recv_buf),
    };

    int ret = dfu_update_storage_init(&upd_ctx.dfu_ctx);

    if (ret) {
        LOG_ERR("Unable init flash storage: %d", ret);
        return HTTP_ERR_DFU_INIT;
    }

#ifdef CONFIG_RPR_HASH_CALCULATION
    mbedtls_md_starts(&dwn_hash_ctx->hash_ctx);
#endif

    LOG_INF("Starting update download...");

    *http_status_code = INTERNAL_SERVER_ERROR;

    ret = http_client_req(sock, &req, HTTP_TIMEOUT_MS, &ctx);

#ifdef CONFIG_RPR_HASH_CALCULATION
    mbedtls_md_finish(&dwn_hash_ctx->hash_ctx, dwn_hash_ctx->response_hash);
    mbedtls_md_free(&dwn_hash_ctx->hash_ctx);
#endif

    if (ret < 0) {
        LOG_ERR("HTTP client request failed with code %d", ret);
        return HTTP_ERR_CLIENT_REQUEST;
    }

    if (*http_status_code != HTTP_STATUS_OK) {
        if (*http_status_code == INTERNAL_SERVER_ERROR)
            LOG_WRN("Unexpected internal server error: %d", *http_status_code);
        else
            LOG_WRN("Unexpected HTTP status: %d", *http_status_code);

        return HTTP_BAD_STATUS_CODE;
    }

    LOG_INF("Download update complete. Size: %u Bytes (%u KiB)",
            upd_ctx.filesize,
            bytes2KiB(upd_ctx.filesize));

#ifdef CONFIG_RPR_HASH_CALCULATION
    int mbedtls_hash_len = mbedtls_md_get_size(dwn_hash_ctx->hash_info);
    print_hash(dwn_hash_ctx->response_hash, mbedtls_hash_len);

#ifdef CONFIG_RPR_IMAGE_INTEGRITY_CHECK

    ret = dfu_update_flash_img_check(
            &upd_ctx.dfu_ctx, dwn_hash_ctx->response_hash, upd_ctx.filesize);

    if (ret < 0) {
        LOG_ERR("The hash of the flashed and downloaded images do not match! %d",
                ret);
        return HTTP_ERR_DFU_HASH_NOT_MATCH;
    }

#endif // CONFIG_RPR_IMAGE_INTEGRITY_CHECK
#endif // CONFIG_RPR_HASH_CALCULATION

    return HTTP_CLIENT_OK;
}
#endif //CONFIG_RPR_MODULE_DFU
