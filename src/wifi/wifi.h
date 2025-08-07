/**
 * @file wifi.h
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
#ifndef WIFI_MODULE_H
#define WIFI_MODULE_H
#include <zephyr/net/wifi.h>
#include <zephyr/net/wifi_mgmt.h>

typedef enum {
    WIFI_OK = 0,
    WIFI_WRN_ALREADY_CONNECTED,
    WIFI_WRN_NOT_CONNECTED,
    WIFI_ERR_INVALID_VALUE,
    WIFI_ERR_IFACE_NOT_FOUND,
    WIFI_ERR_REQUEST_FAIL,
    WIFI_WRN_REQ_ALREADY_DONE,
    WIFI_ERR_REQUEST_TIMEOUT,
    WIFI_ERR_GET_RES,
} wifi_status_t;

struct wifi_par_context {
    char                      ssid[WIFI_SSID_MAX_LEN + 1];
    char                      psk[WIFI_PSK_MAX_LEN + 1];
    enum wifi_frequency_bands band;
};

/**
 * @brief Callback type used to look up saved Wi-Fi configuration by SSID.
 *
 * This function is used during autoconnect to determine whether the scanned SSID
 * matches a saved configuration. If a match is found, the corresponding configuration
 * is written to the output parameter.
 *
 * @param ssid      SSID of the scanned Wi-Fi network.
 * @param out_cfg   Pointer to the structure where the matched configuration
 *                  should be written, if found.
 *
 * @return true if a saved configuration is found for the given SSID, false otherwise.
 */
typedef bool (*wifi_config_lookup_cb_t)(const char              *ssid,
                                        struct wifi_par_context *out_cfg);

/**
 * @brief Starts the Wi-Fi scan operation with request synchronization.
 *
 * @return WIFI_OK on success, or an appropriate error code on failure.
 */
wifi_status_t wifi_start_scan(void);

/**
 * @brief Start connection to a specified Wi-Fi network.
 *
 * Validates the input connection context (SSID, password, band), checks for
 * existing connection, and if not connected, initiates the connection process.
 *
 * @param ctx Pointer to the Wi-Fi connection parameters (SSID, PSK, band).
 * @return WIFI_OK on success, or an appropriate error code on failure.
 */
wifi_status_t wifi_start_connect(const struct wifi_par_context *ctx);

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
wifi_status_t wifi_start_autoconnect(wifi_config_lookup_cb_t lookup_cb);

/**
 * @brief Initiates disconnection from the currently connected Wi-Fi network.
 *
 * Acquires the Wi-Fi request semaphore to ensure thread-safe access,
 * then sends a disconnect request to the Wi-Fi interface. 
 *
 * @return WIFI_OK on success, or an appropriate error code on failure.
 */
wifi_status_t wifi_start_disconnect(void);

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
wifi_status_t wifi_get_info(struct wifi_iface_status *status);

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
wifi_status_t wifi_get_current_param(struct wifi_par_context *ctx);

/**
 * @brief Set the first Wi-Fi STA interface as the default one.
 *
 * @retval WIFI_OK If successfully set or already set.
 * @retval WIFI_ERR_IFACE_NOT_FOUND If no Wi-Fi STA interface found.
 */
wifi_status_t wifi_set_iface_default(void);

/**
 * @brief Check if the Wi-Fi STA interface is currently the default one.
 *
 * @retval true If Wi-Fi STA interface is default
 * @retval false If not or not found
 */
bool is_wifi_iface_default(void);

/**
 * @brief Check if the given interface is the active Wi-Fi STA interface.
 *
 * @param iface Pointer to the network interface to check.
 * @return true if the interface is Wi-Fi, false otherwise.
 */
bool is_wifi_iface(struct net_if *iface);

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
                                        size_t *count);

#endif // CONFIG_RPR_WIFI_GET_SCAN_RESULT

#endif // WIFI_MODULE_H