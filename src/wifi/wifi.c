/**
 * @file wifi.с
 * @brief Wi-Fi management interface for connection, scanning, and status operations.
 *
 * This module provides a high-level API for managing Wi-Fi operations in Zephyr-based systems.
 * It includes functions to:
 * - Start Wi-Fi scan and retrieve scan results.
 * - Connect to specified networks or automatically connect to saved ones.
 * - Disconnect from networks and retrieve current connection status.
 * - Manage Wi-Fi interface and access connection parameters.
 *
 * The module also provides thread-safe access to Wi-Fi operations using a semaphore
 * and supports autoconnect logic using a user-provided SSID lookup callback.
 *
 * @author Eugene K.
 * @copyright (C) 2025 Alnicko Lab OU. All rights reserved.
 */

#include <zephyr/kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <zephyr/init.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_event.h>

#include "wifi.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(wifi_module, CONFIG_RPR_WIFI_LOG_LEVEL);

#define WIFI_REQUEST_TIMEOUT_MS       K_MSEC(2000)
#define WIFI_CONNECTION_CHECK_TIME_MS K_MSEC(5000)

static struct net_mgmt_event_callback wifi_mgmt_cb;
static struct net_mgmt_event_callback wifi_scan_cb;

struct wifi_autoconnect {
    bool                    autoconnecting;
    bool                    found_saved;
    wifi_config_lookup_cb_t config_lookup_cb;
};

struct wifi_context {
    uint32_t                scan_result_num;
    bool                    is_wifi_connected;
    struct net_if          *iface;
    struct k_sem            wifi_request_sem;
    struct wifi_par_context wifi_par_ctx;
    struct wifi_autoconnect auto_ctx;
    struct k_work_delayable connection_check;
#ifdef CONFIG_RPR_WIFI_GET_SCAN_RESULT
    bool                     req_scan;
    struct wifi_scan_result *ptr_scan_result;
#endif
};

static struct wifi_context wifi_ctx = {

    .scan_result_num        = 0,
    .is_wifi_connected      = false,
    .iface                  = NULL, 
    .wifi_par_ctx = {
        .ssid = "",
        .psk  = "",
        .band = 0,
    },
    .auto_ctx = {
        .autoconnecting     = false,
        .found_saved        = false,
        .config_lookup_cb   = NULL,
    },

#ifdef CONFIG_RPR_WIFI_GET_SCAN_RESULT
    .req_scan           = false,
    .ptr_scan_result    = NULL,
#endif
};

#ifdef CONFIG_RPR_WIFI_GET_SCAN_RESULT
static struct wifi_scan_result
        wifi_scan_result_buf[CONFIG_RPR_WIFI_SCAN_RESULT_MAX_SIZE];
#endif

#define WIFI_MGMT_EVENTS \
    (NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT)

#define WIFI_SCAN_EVENTS (NET_EVENT_WIFI_SCAN_RESULT | NET_EVENT_WIFI_SCAN_DONE)

/**
 * @brief Prepare Wi-Fi request by acquiring the semaphore.
 *
 * Checks if the Wi-Fi STA interface is available and attempts to take the
 * request semaphore within the specified timeout.
 * 
 * @param wait_for_request_ms Timeout to wait for semaphore acquisition.
 * @return WIFI_OK on success or an error otherwise 
 */
static wifi_status_t wifi_request_setup(k_timeout_t wait_for_request_ms)
{
    if (!wifi_ctx.iface) {
        LOG_ERR("No Wi-Fi STA interface found");
        return WIFI_ERR_IFACE_NOT_FOUND;
    }

    if (k_sem_take(&wifi_ctx.wifi_request_sem, wait_for_request_ms) != 0) {
        LOG_WRN("Failed to acquire Wi-Fi request semaphore (timeout)");
        return WIFI_ERR_REQUEST_TIMEOUT;
    }

    return WIFI_OK;
}

/**
 * @brief Release the Wi-Fi request semaphore.
 *
 * Unlocks the semaphore to allow other Wi-Fi operations to proceed.
 */
static void wifi_request_unlock(void)
{
    k_sem_give(&wifi_ctx.wifi_request_sem);
}

/**
 * @brief Check if Wi-Fi is connected after a connection request.
 *
 * This function verifies whether the device successfully connected to a Wi-Fi
 * access point after a connection attempt. Since net_mgmt may not report connection
 * failures explicitly, this check is necessary to handle cases where the connection
 * silently fails. If not connected, it unlocks the Wi-Fi request.
 *
 * @param work Pointer to the work item (unused).
 */
static void wifi_connection_check(struct k_work *work)
{

    if (wifi_ctx.is_wifi_connected) {
        return;
    }

    LOG_WRN("Still unable to connect to the network \"%s\"",
            wifi_ctx.wifi_par_ctx.ssid);

    wifi_request_unlock();
}

/**
 * @brief Initiates a Wi-Fi scan request.
 *
 * Sends a scan request via the net_mgmt API to detect available Wi-Fi networks.
 * Adds the scan event callback and resets the internal scan result counter.
 *
 * @return WIFI_OK on success, or WIFI_ERR_REQUEST_FAIL if the scan request fails.
 */
static wifi_status_t wifi_request_scan(void)
{
    struct wifi_scan_params params = { 0 };

    net_mgmt_add_event_callback(&wifi_scan_cb);
    wifi_ctx.scan_result_num = 0;

    int status = net_mgmt(
            NET_REQUEST_WIFI_SCAN, wifi_ctx.iface, &params, sizeof(params));

    if (status) {
        LOG_WRN("Scan request failed with error: %d", status);
        return WIFI_ERR_REQUEST_FAIL;
    } else {
        LOG_DBG("Scan requested");
    }
    return WIFI_OK;
}

/**
 * @brief Starts the Wi-Fi scan operation with request synchronization.
 *
 * @return WIFI_OK on success, or an appropriate error code on failure.
 */
wifi_status_t wifi_start_scan(void)
{
    wifi_status_t ret = wifi_request_setup(WIFI_REQUEST_TIMEOUT_MS);

    if (ret != WIFI_OK) {
        return ret;
    }

#ifdef CONFIG_RPR_WIFI_GET_SCAN_RESULT
    wifi_ctx.req_scan        = true;
    wifi_ctx.ptr_scan_result = wifi_scan_result_buf;
#endif

    ret = wifi_request_scan();

    if (ret != WIFI_OK) {
        wifi_request_unlock();
    }
    return ret;
}

#ifdef CONFIG_RPR_WIFI_GET_SCAN_RESULT

/**
 * @brief Copies the most recent Wi-Fi scan results to the provided buffer.
 *
 * @param[out] copy_result Pointer to the buffer where results will be copied.
 * @param[in]  copy_result_size Maximum number of entries that can be copied.
 * @param[out] count Pointer to variable where the number of copied results is stored.
 *
 * @return WIFI_OK on success, or an appropriate error code on failure.
 */
wifi_status_t wifi_get_last_scan_result(struct wifi_scan_result *copy_result,
                                        size_t  copy_result_size,
                                        size_t *count)
{
    if (!copy_result) {
        LOG_ERR("Input context pointer is NULL");
        return WIFI_ERR_INVALID_VALUE;
    }
    if (!count) {
        LOG_ERR("Count pointer is NULL");
        return WIFI_ERR_INVALID_VALUE;
    }

    if (copy_result_size == 0 ||
        copy_result_size > CONFIG_RPR_WIFI_SCAN_RESULT_MAX_SIZE) {
        LOG_ERR("Invalid buffer size: %zu", copy_result_size);
        return WIFI_ERR_INVALID_VALUE;
    }

    wifi_status_t ret = wifi_request_setup(WIFI_REQUEST_TIMEOUT_MS);

    if (ret != WIFI_OK) {
        return ret;
    }
    if (!wifi_ctx.ptr_scan_result) {
        LOG_ERR("Scan result buffer is NULL");
        wifi_request_unlock();
        return WIFI_ERR_GET_RES;
    }

    *count = MIN(wifi_ctx.scan_result_num, copy_result_size);

    memcpy(copy_result,
           wifi_ctx.ptr_scan_result,
           *count * sizeof(struct wifi_scan_result));

    LOG_DBG("Successfully copied %zu scan result(s)", *count);

    wifi_request_unlock();
    return WIFI_OK;
}

/**
 * @brief Copy a single Wi-Fi scan result into the scan result buffer.
 *
 * @param entry Pointer to the scan result entry to be copied.
 */
static void wifi_copy_result(const struct wifi_scan_result *entry)
{
    if (!wifi_ctx.req_scan || !entry)
        return;
    if (!wifi_ctx.ptr_scan_result) {
        LOG_ERR("Scan result buffer not initialized");
    } else if (wifi_ctx.scan_result_num <
               CONFIG_RPR_WIFI_SCAN_RESULT_MAX_SIZE) {
        memcpy(&wifi_ctx.ptr_scan_result[wifi_ctx.scan_result_num],
               entry,
               sizeof(struct wifi_scan_result));
    } else {
        LOG_DBG("Scan result buffer full (max = %d)",
                CONFIG_RPR_WIFI_SCAN_RESULT_MAX_SIZE);
    }
}

#endif

/**
 * @brief Initiate a Wi-Fi connection request using stored connection parameters.
 *
 * This function prepares the Wi-Fi connection parameters from the internal context
 * and sends a connection request via the net_mgmt API. It resets the internal 
 * connection status and logs the result of the request.
 *
 * @return WIFI_OK on success, or WIFI_ERR_REQUEST_FAIL on failure.
 */
static wifi_status_t wifi_request_connect(void)
{
    struct wifi_connect_req_params wifi_params = { 0 };

    wifi_ctx.is_wifi_connected = false;

    wifi_params.ssid        = wifi_ctx.wifi_par_ctx.ssid;
    wifi_params.psk         = wifi_ctx.wifi_par_ctx.psk;
    wifi_params.ssid_length = strlen(wifi_ctx.wifi_par_ctx.ssid);
    wifi_params.psk_length  = strlen(wifi_ctx.wifi_par_ctx.psk);
    wifi_params.channel     = WIFI_CHANNEL_ANY;
    wifi_params.security    = (strlen(wifi_ctx.wifi_par_ctx.psk) == 0) ?
                                      WIFI_SECURITY_TYPE_NONE :
                                      WIFI_SECURITY_TYPE_PSK;

    wifi_params.band = wifi_ctx.wifi_par_ctx.band;
    wifi_params.mfp  = WIFI_MFP_OPTIONAL;

    int status = net_mgmt(NET_REQUEST_WIFI_CONNECT,
                          wifi_ctx.iface,
                          &wifi_params,
                          sizeof(struct wifi_connect_req_params));

    if (status) {
        LOG_WRN("Connection request failed with error: %d", status);
        return WIFI_ERR_REQUEST_FAIL;
    } else {
        LOG_DBG("Connection requested");
        k_work_reschedule(&wifi_ctx.connection_check,
                          WIFI_CONNECTION_CHECK_TIME_MS);
    }
    return WIFI_OK;
}

/**
 * @brief Initiate a Wi-Fi disconnection request.
 *
 * Sends a disconnection request to the Wi-Fi interface using the net_mgmt API.
 *
 * @return WIFI_OK on success, or WIFI_ERR_REQUEST_FAIL if the request failed.
 */
static wifi_status_t wifi_request_disconnect(void)
{
    int status = net_mgmt(NET_REQUEST_WIFI_DISCONNECT, wifi_ctx.iface, NULL, 0);

    if (status) {
        LOG_WRN("Disconnect request failed with error: %d", status);
        return WIFI_ERR_REQUEST_FAIL;
    } else {
        LOG_DBG("Disconnect requested");
    }
    return WIFI_OK;
}

/**
 * @brief Attempt to connect to a previously saved Wi-Fi network.
 *
 * Checks if a saved network configuration was found during scanning.
 * If found, initiates a connection using the saved parameters.
 * If the connection attempt fails or no saved network is found,
 * the Wi-Fi request semaphore is released to allow further operations.
 */
static void wifi_request_autoconnect(void)
{
    if (wifi_ctx.auto_ctx.found_saved) {

        wifi_ctx.auto_ctx.found_saved = false;

        LOG_INF("Saved Wi-Fi network found: %s", wifi_ctx.wifi_par_ctx.ssid);

        LOG_DBG("Saved password: %s", wifi_ctx.wifi_par_ctx.psk);

        LOG_DBG("Saved band: %s", wifi_band_txt(wifi_ctx.wifi_par_ctx.band));

        wifi_status_t ret = wifi_request_connect();

        if (ret == WIFI_OK) {
            LOG_INF("Connecting to saved Wi-Fi network...");
            return; // Semaphore will be released by the connection handler
        } else {
            LOG_WRN("Failed to connect to saved network (code %d)", ret);
        }
    } else {
        LOG_WRN("No saved Wi-Fi network found");
    }

    wifi_request_unlock();
}

/**
 * @brief Start connection to a specified Wi-Fi network.
 *
 * Validates the input connection context (SSID, password, band), checks for
 * existing connection, and if not connected, initiates the connection process.
 *
 * @param ctx Pointer to the Wi-Fi connection parameters (SSID, PSK, band).
 * @return WIFI_OK on success, or an appropriate error code on failure.
 */
wifi_status_t wifi_start_connect(const struct wifi_par_context *ctx)
{
    if (!ctx) {
        LOG_ERR("Input context pointer is NULL");
        return WIFI_ERR_INVALID_VALUE;
    }

    if (strlen(ctx->ssid) > WIFI_SSID_MAX_LEN) {
        LOG_ERR("SSID too long (max %d characters)", WIFI_SSID_MAX_LEN);
        return WIFI_ERR_INVALID_VALUE;
    }

    if (strlen(ctx->psk) > WIFI_PSK_MAX_LEN) {
        LOG_ERR("PASSWORD too long (max %d characters)", WIFI_PSK_MAX_LEN);
        return WIFI_ERR_INVALID_VALUE;
    }

    wifi_status_t ret = wifi_request_setup(WIFI_REQUEST_TIMEOUT_MS);

    if (ret != WIFI_OK) {
        return ret;
    }

    if (wifi_ctx.is_wifi_connected) {
        LOG_WRN("Wi-Fi is already connected to the network \"%s\"",
                wifi_ctx.wifi_par_ctx.ssid);
        wifi_request_unlock();
        return WIFI_WRN_ALREADY_CONNECTED;
    }

    strncpy(wifi_ctx.wifi_par_ctx.ssid, ctx->ssid, WIFI_SSID_MAX_LEN);
    wifi_ctx.wifi_par_ctx.ssid[WIFI_SSID_MAX_LEN] = '\0';

    strncpy(wifi_ctx.wifi_par_ctx.psk, ctx->psk, WIFI_PSK_MAX_LEN);
    wifi_ctx.wifi_par_ctx.psk[WIFI_PSK_MAX_LEN] = '\0';

    wifi_ctx.wifi_par_ctx.band = ctx->band;

    ret = wifi_request_connect();

    if (ret != WIFI_OK) {
        wifi_request_unlock();
    }
    return ret;
}

/**
 * @brief Starts the autoconnect process by scanning for available networks
 *        and trying to connect to a known one using the provided lookup callback.
 *
 * Acquires the Wi-Fi request semaphore for thread safety, checks if the device
 * is already connected, and if not — sets the autoconnect context with the
 * specified callback function. Then initiates a Wi-Fi scan to search for
 * known networks.
 *
 * @param lookup_cb Callback function used to match scanned SSIDs with saved configurations.
 *
 * @return WIFI_OK on success, or an appropriate error code on failure.
 */
wifi_status_t wifi_start_autoconnect(wifi_config_lookup_cb_t lookup_cb)
{

    if (!lookup_cb) {
        LOG_ERR("Input cb pointer is NULL");
        return WIFI_ERR_INVALID_VALUE;
    }

    wifi_status_t ret = wifi_request_setup(WIFI_REQUEST_TIMEOUT_MS);

    if (ret != WIFI_OK) {
        return ret;
    }

    if (wifi_ctx.is_wifi_connected) {
        LOG_WRN("Wi-Fi is already connected to the network \"%s\"",
                wifi_ctx.wifi_par_ctx.ssid);
        wifi_request_unlock();
        return WIFI_WRN_ALREADY_CONNECTED;
    }

    wifi_ctx.auto_ctx.autoconnecting   = true;
    wifi_ctx.auto_ctx.found_saved      = false;
    wifi_ctx.auto_ctx.config_lookup_cb = lookup_cb;

    LOG_INF("Search for saved networks...");

    ret = wifi_request_scan();

    if (ret != WIFI_OK) {
        wifi_request_unlock();
    }
    return ret;
}

/**
 * @brief Initiates disconnection from the currently connected Wi-Fi network.
 *
 * Acquires the Wi-Fi request semaphore to ensure thread-safe access,
 * then sends a disconnect request to the Wi-Fi interface. 
 *
 * @return WIFI_OK on success, or an appropriate error code on failure.
 */
wifi_status_t wifi_start_disconnect(void)
{

    if (!wifi_ctx.is_wifi_connected) {
        LOG_WRN("Wi-Fi is not connected");
        return WIFI_WRN_NOT_CONNECTED;
    }

    wifi_status_t ret = wifi_request_setup(WIFI_REQUEST_TIMEOUT_MS);

    if (ret != WIFI_OK) {
        return ret;
    }

    LOG_INF("Disconnecting from the network \"%s\"...",
            wifi_ctx.wifi_par_ctx.ssid);

    ret = wifi_request_disconnect();

    if (ret != WIFI_OK) {
        wifi_request_unlock();
    }
    return ret;
}

/**
 * @brief Retrieves the current Wi-Fi interface status.
 *
 * This function fills the provided structure with the current status
 * of the Wi-Fi connection (SSID, BSSID, RSSI, etc.), if the device is connected.
 * Ensures thread-safe access using the Wi-Fi request semaphore.
 *
 * @param status Pointer to a structure where the Wi-Fi status will be stored.
 *
 * @return WIFI_OK on success, or an appropriate error code on failure.
 */
wifi_status_t wifi_get_info(struct wifi_iface_status *status)
{

    if (!status) {
        LOG_ERR("Invalid pointer to status structure");
        return WIFI_ERR_INVALID_VALUE;
    }

    if (!wifi_ctx.is_wifi_connected) {
        LOG_WRN("Wi-Fi is not connected");
        return WIFI_WRN_NOT_CONNECTED;
    }

    wifi_status_t ret = wifi_request_setup(WIFI_REQUEST_TIMEOUT_MS);

    if (ret != WIFI_OK) {
        return ret;
    }

    int mgmt_status = net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS,
                               wifi_ctx.iface,
                               status,
                               sizeof(struct wifi_iface_status));

    if (mgmt_status) {
        LOG_ERR("WiFi status request failed: %d", mgmt_status);
        wifi_request_unlock();
        return WIFI_ERR_REQUEST_FAIL;
    }

    LOG_INF("Wi-Fi status retrieved for \"%s\", %d dBm",
            status->ssid,
            status->rssi);

    wifi_request_unlock();
    return WIFI_OK;
}

/**
 * @brief Retrieves the current Wi-Fi connection parameters.
 *
 * This function copies the current SSID, password (PSK), and band
 * used in the last successful connection to the provided context structure.
 *
 * @param ctx Pointer to a structure where the Wi-Fi parameters will be stored.
 *
 * @return WIFI_OK on success,
 *         WIFI_ERR_INVALID_VALUE if the input pointer is NULL,
 *         WIFI_WRN_NOT_CONNECTED if Wi-Fi is not currently connected.
 */
wifi_status_t wifi_get_current_param(struct wifi_par_context *ctx)
{
    if (!ctx) {
        LOG_ERR("Input context pointer is NULL");
        return WIFI_ERR_INVALID_VALUE;
    }

    if (!wifi_ctx.is_wifi_connected) {
        LOG_WRN("Wi-Fi is not connected");
        return WIFI_WRN_NOT_CONNECTED;
    }

    strncpy(ctx->ssid, wifi_ctx.wifi_par_ctx.ssid, WIFI_SSID_MAX_LEN);
    ctx->ssid[WIFI_SSID_MAX_LEN] = '\0';

    strncpy(ctx->psk, wifi_ctx.wifi_par_ctx.psk, WIFI_PSK_MAX_LEN);
    ctx->psk[WIFI_PSK_MAX_LEN] = '\0';

    ctx->band = wifi_ctx.wifi_par_ctx.band;

    return WIFI_OK;
}

/**
 * @brief Set the first Wi-Fi STA interface as the default one.
 *
 * @retval WIFI_OK If successfully set or already set.
 * @retval WIFI_ERR_IFACE_NOT_FOUND If no Wi-Fi STA interface found.
 */
wifi_status_t wifi_set_iface_default(void)
{
    if (!wifi_ctx.iface) {
        LOG_ERR("No Wi-Fi STA interface found");
        return WIFI_ERR_IFACE_NOT_FOUND;
    }

    net_if_set_default(wifi_ctx.iface);
    LOG_DBG("Wi-Fi interface set as default");
    return WIFI_OK;
}

/**
 * @brief Check if the Wi-Fi STA interface is currently the default one.
 *
 * @retval true If Wi-Fi STA interface is default
 * @retval false If not or not found
 */
bool is_wifi_iface_default(void)
{
    struct net_if *iface_def = net_if_get_default();

    return wifi_ctx.iface && (wifi_ctx.iface == iface_def);
}

/**
 * @brief Check if the given interface is the active Wi-Fi STA interface.
 *
 * @param iface Pointer to the network interface to check.
 * @return true if the interface is Wi-Fi, false otherwise.
 */
bool is_wifi_iface(struct net_if *iface)
{
    return iface && (iface == net_if_get_wifi_sta());
}

/**
 * @brief Handles individual Wi-Fi scan results from the network management event.
 *
 * This function is called for each scan result during a Wi-Fi scan operation.
 * It copies the scan result into the internal buffer (if enabled), increments
 * the scan result counter, and optionally prints information about the network.
 * If autoconnect mode is active, it attempts to match the scanned SSID with
 * saved configuration via a user-provided callback.
 *
 * @param cb Pointer to the network management event callback structure containing the scan result.
 */
static void handle_wifi_scan_result(struct net_mgmt_event_callback *cb)
{
    const struct wifi_scan_result *entry =
            (const struct wifi_scan_result *)cb->info;

    char ssid_print[WIFI_SSID_MAX_LEN + 1];

#ifdef CONFIG_RPR_WIFI_GET_SCAN_RESULT
    wifi_copy_result(entry);
#endif

    wifi_ctx.scan_result_num++;

    strncpy(ssid_print, entry->ssid, sizeof(ssid_print) - 1);
    ssid_print[sizeof(ssid_print) - 1] = '\0';

#ifndef CONFIG_NET_L2_WIFI_SHELL
    printk("%d) SSID: %s, Band: %s, RSSI: %d, Security type: %s\n",
           wifi_ctx.scan_result_num,
           (entry->ssid_length == 0) ? "-" : ssid_print,
           wifi_band_txt(entry->band),
           entry->rssi,
           wifi_security_txt(entry->security));
#endif

    if (wifi_ctx.auto_ctx.autoconnecting && !wifi_ctx.auto_ctx.found_saved &&
        wifi_ctx.auto_ctx.config_lookup_cb) {

        bool ret = wifi_ctx.auto_ctx.config_lookup_cb(ssid_print,
                                                      &wifi_ctx.wifi_par_ctx);

        if (ret) {
            wifi_ctx.auto_ctx.found_saved = true;
        }
    }
}

/**
 * @brief Handles the completion of a Wi-Fi scan operation.
 *
 * This function is called when the Wi-Fi scan is finished. It checks the scan result
 * status, logs appropriate messages, and handles autoconnect logic if enabled.
 *
 * @param cb Pointer to the network management event callback structure containing the scan status.
 */
static void handle_wifi_scan_done(struct net_mgmt_event_callback *cb)
{
    const struct wifi_status *status = (const struct wifi_status *)cb->info;

    net_mgmt_del_event_callback(&wifi_scan_cb);

    if (!status) {
        LOG_ERR("Received NULL wifi_status in scan result");
        wifi_request_unlock();
        return;
    }

    if (status->status) {
        LOG_WRN("Wi-Fi scan failed with error: %d", status->status);

    } else {
        LOG_INF("Wi-Fi scan completed successfully");

        if (wifi_ctx.auto_ctx.autoconnecting) {
            wifi_request_autoconnect();
            return;
        }
    }
    wifi_request_unlock();
}

/**
 * @brief Handles the result of a Wi-Fi connection attempt.
 *
 * This function is triggered by a network management event after a Wi-Fi connection
 * request. It checks the connection status, logs the result, updates internal connection
 * state.
 *
 * @param cb Pointer to the net management event callback containing connection status info.
 */
static void handle_wifi_connect_result(struct net_mgmt_event_callback *cb)
{
    const struct wifi_status *status = (const struct wifi_status *)cb->info;

    if (!status) {
        LOG_ERR("Received NULL wifi_status in connect result");
        wifi_request_unlock();
        return;
    }

    wifi_ctx.auto_ctx.autoconnecting = false;

    if (status->status) {
        LOG_WRN("Connection request failed. Wrong password or invalid connection parameters");
        wifi_ctx.is_wifi_connected = false;
        wifi_request_disconnect();
    } else {
        LOG_INF("Wi-Fi connected to SSID: %s", wifi_ctx.wifi_par_ctx.ssid);
        wifi_ctx.is_wifi_connected = true;
    }
    wifi_request_unlock();
}

/**
 * @brief Handles the result of a Wi-Fi disconnection event.
 *
 * This function is called in response to a network management event indicating the
 * outcome of a Wi-Fi disconnect request. 
 *
 * @param cb Pointer to the net management event callback containing disconnection status info.
 */
static void handle_wifi_disconnect_result(struct net_mgmt_event_callback *cb)
{
    const struct wifi_status *status = (const struct wifi_status *)cb->info;

    if (!status) {
        LOG_ERR("Received NULL wifi_status in disconnect result");
        wifi_request_unlock();
        return;
    }

    if (status->status) {
        LOG_WRN("Disconnection request failed (%d)", status->status);
    } else {
        LOG_INF("Wi-Fi disconnected");

        if (wifi_ctx.is_wifi_connected) {
            wifi_ctx.is_wifi_connected = false;
        } else {
            LOG_DBG("Already disconnected");
        }
    }
    wifi_request_unlock();
}

#ifdef CONFIG_WIFI_NM_WPA_SUPPLICANT_ROAMING
/**
 * @brief Handles a Wi-Fi signal strength change event.
 *
 * Triggers a roaming attempt by sending a NET_REQUEST_WIFI_START_ROAMING request
 * to the network interface.
 *
 * @param cb     Pointer to the network management event callback.
 * @param iface  Pointer to the network interface where the signal change occurred.
 */
static void handle_wifi_signal_change(struct net_mgmt_event_callback *cb,
                                      struct net_if                  *iface)
{
    int ret;

    ret = net_mgmt(NET_REQUEST_WIFI_START_ROAMING, iface, NULL, 0);
    if (ret) {
        LOG_WRN("Start roaming failed");
        return;
    }

    LOG_DBG("Start roaming requested");
}

/**
 * @brief Handles completion of the Wi-Fi neighbor report.
 *
 * Sends a NET_REQUEST_WIFI_NEIGHBOR_REP_COMPLETE request to the specified network
 * interface to notify that the neighbor report process has completed.
 *
 * @param cb     Pointer to the network management event callback.
 * @param iface  Pointer to the network interface associated with the event.
 */
static void
handle_wifi_neighbor_rep_complete(struct net_mgmt_event_callback *cb,
                                  struct net_if                  *iface)
{
    int ret;

    ret = net_mgmt(NET_REQUEST_WIFI_NEIGHBOR_REP_COMPLETE, iface, NULL, 0);
    if (ret) {
        LOG_WRN("Neighbor report complete failed");
        return;
    }

    LOG_DBG("Neighbor report complete requested");
}
#endif

/**
 * @brief Main handler for Wi-Fi management events.
 *
 * Dispatches incoming Wi-Fi-related network management events to their corresponding
 * handlers based on the event type. 
 *
 * @param cb          Pointer to the network management event callback structure.
 * @param mgmt_event  Event type identifier.
 * @param iface       Pointer to the network interface that triggered the event.
 */
static void wifi_mgmt_event_handler(struct net_mgmt_event_callback *cb,
                                    uint32_t                        mgmt_event,
                                    struct net_if                  *iface)
{
    switch (mgmt_event) {
    case NET_EVENT_WIFI_CONNECT_RESULT:
        handle_wifi_connect_result(cb);
        break;
    case NET_EVENT_WIFI_DISCONNECT_RESULT:
        handle_wifi_disconnect_result(cb);
        break;
#ifdef CONFIG_WIFI_NM_WPA_SUPPLICANT_ROAMING
    case NET_EVENT_WIFI_SIGNAL_CHANGE:
        handle_wifi_signal_change(cb, iface);
        break;
    case NET_EVENT_WIFI_NEIGHBOR_REP_COMP:
        handle_wifi_neighbor_rep_complete(cb, iface);
        break;
#endif
    default:
        break;
    }
}

/**
 * @brief Handles Wi-Fi scan-related management events.
 *
 * Processes network management events related to Wi-Fi scanning.
 *
 * @param cb          Pointer to the network management event callback structure.
 * @param mgmt_event  Event type identifier.
 * @param iface       Pointer to the network interface that triggered the event.
 */
static void wifi_mgmt_scan_event_handler(struct net_mgmt_event_callback *cb,
                                         uint32_t       mgmt_event,
                                         struct net_if *iface)
{
    switch (mgmt_event) {
    case NET_EVENT_WIFI_SCAN_RESULT:
        handle_wifi_scan_result(cb);
        break;
    case NET_EVENT_WIFI_SCAN_DONE:
        handle_wifi_scan_done(cb);
        break;
    default:
        break;
    }
}

/**
 * @brief Initializes the Wi-Fi module and registers event callbacks.
 *
 * Sets up the Wi-Fi context, initializes the semaphore used for request
 * synchronization, retrieves the Wi-Fi STA interface, and registers
 * management event callbacks for both general Wi-Fi events and scan events.
 *
 * @return 0 on success, or a negative error code if the interface is not found.
 */
static int wifi_init(void)
{
    k_sem_init(&wifi_ctx.wifi_request_sem, 1, 1);

    wifi_ctx.iface = net_if_get_wifi_sta();

    if (!wifi_ctx.iface) {
        LOG_ERR("Failed to get Wi-Fi STA interface");
        return -ENODEV;
    }

    net_mgmt_init_event_callback(
            &wifi_mgmt_cb, wifi_mgmt_event_handler, WIFI_MGMT_EVENTS);

    net_mgmt_add_event_callback(&wifi_mgmt_cb);

    net_mgmt_init_event_callback(
            &wifi_scan_cb, wifi_mgmt_scan_event_handler, WIFI_SCAN_EVENTS);

    k_work_init_delayable(&wifi_ctx.connection_check, wifi_connection_check);

    LOG_INF("Wi-Fi module initialized");

    return 0;
}

SYS_INIT(wifi_init, APPLICATION, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
