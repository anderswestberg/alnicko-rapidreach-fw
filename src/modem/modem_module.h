/**
 * @file modem_module.h
 * @brief Modem initialization and connection module for Zephyr-based systems.
 *
 * This module provides an interface to power on, initialize, and establish a network
 * connection via a cellular modem using Zephyr's networking and management 
 * APIs for C16QS modem.
 *
 * @author Eugene K.
 * @copyright (C) 2025 Alnicko Lab OU. All rights reserved.
 */
#ifndef MODEM_MODULE_H
#define MODEM_MODULE_H
#include <zephyr/drivers/gpio.h>
#include <zephyr/net/net_if.h>

#define INFO_BUFFER_SIZE 64

/**
 * @brief Error codes for modem c16qs
 */
typedef enum {
    MODEM_SUCCESS = 0,
    MODEM_ERR_NO_DEV,
    MODEM_ERR_IFACE_UP,
    MODEM_ERR_IFACE_DOWN,
    MODEM_ERR_POWER_DOWN,
    MODEM_ERR_INFO_RETRIEVAL,
    MODEM_ERR_NOT_INIT     = -1,
    MODEM_ERR_INIT_FAILED  = -2,
    MODEM_ERR_NO_INTERFACE = -3
} c16qs_modem_status_t;

/**
 * @brief Structure holding modem information
 */
typedef struct {
    char imei[INFO_BUFFER_SIZE];
    char model_id[INFO_BUFFER_SIZE];
    char manufacturer[INFO_BUFFER_SIZE];
    char sim_imsi[INFO_BUFFER_SIZE];
    char sim_iccid[INFO_BUFFER_SIZE];
    char fw_version[INFO_BUFFER_SIZE];
} modem_info_t;

/**
 * @brief Initialize and connect the C16QS modem.
 *
 * Powers on the modem, brings up the PPP network interface.
 *
 * @return c16qs_modem_status_t Error code or MODEM_SUCCESS on success.
 */
c16qs_modem_status_t modem_init_and_connect(void);

/**
 * @brief Restart the C16QS modem.
 *
 * Suspends and resumes the modem.
 *
 * @return c16qs_modem_status_t Error code or MODEM_SUCCESS on success.
 */
c16qs_modem_status_t modem_reset(void);

/**
 * @brief Shutdown the C16QS modem and network interface.
 *
 * Brings down the network interface and powers off the modem.
 *
 * @return c16qs_modem_status_t Error code or MODEM_SUCCESS on success.
 */
c16qs_modem_status_t modem_shutdown(void);

/**
 * @brief Retrieve basic cellular information from the modem.
 *
 * Fills the provided modem_info_t structure with IMEI, model ID,
 * manufacturer, SIM IMSI, ICCID, and firmware version.
 *
 * @param info Pointer to modem_info_t structure to populate.
 * @return c16qs_modem_status_t Error code or MODEM_SUCCESS on success.
 */
c16qs_modem_status_t c16qs_get_cellular_info(modem_info_t *info);

/**
 * @brief Get the current RSSI (signal strength) from the modem.
 *
 * Retrieves RSSI and writes the value to the provided pointer.
 *
 * @param rssi Pointer to int16_t variable to store RSSI.
 * @return c16qs_modem_status_t Error code or MODEM_SUCCESS on success.
 */
c16qs_modem_status_t c16qs_get_signal_rssi(int16_t *rssi);

/**
 * @brief Check whether the modem has been initialized.
 *
 * @return true if the modem is initialized, false otherwise.
 */
bool is_modem_initialized(void);

/**
 * @brief Check whether the modem has been L4 connected.
 *
 * @return true if the modem is L4 connected, false otherwise.
 */
bool is_modem_connected(void);

/**
 * @brief Set the first PPP modem interface as the default network interface.
 *
 * @retval MODEM_SUCCESS         If the modem interface was successfully set as default.
 * @retval MODEM_ERR_NO_INTERFACE If no PPP interface was found (modem not initialized or not connected).
 */
c16qs_modem_status_t modem_set_iface_default(void);

/**
 * @brief Check whether the modem PPP interface is currently the default network interface.
 * 
 * @retval true  If the modem PPP interface is the default interface.
 * @retval false If it's not, or if no PPP interface is available.
 */
bool is_modem_iface_default(void);

/**
 * @brief Check if the given interface is a modem (PPP) interface.
 *
 * @param iface Pointer to the network interface to check.
 * @return true if the interface is a modem (PPP), false otherwise.
 */
bool is_modem_iface(struct net_if *iface);

#endif // MODEM_MODULE_H
