/**
 * @file ethernet.h
 * @brief Ethernet interface utility module.
 *
 * This module provides helper functions for working with Ethernet interfaces
 * in Zephyr. It allows setting the default interface to Ethernet, checking
 * whether a given interface is Ethernet, and verifying whether the current
 * default interface is Ethernet.
 *
 * @author Eugene K.
 * @copyright (C) 2025 Alnicko Lab OU. All rights reserved.
 */

#ifndef ETHERNET_UTILS_H
#define ETHERNET_UTILS_H

#include <zephyr/net/net_if.h>

typedef enum {
    ETHERNET_OK               = 0,
    ETHERNET_ERR_NO_INTERFACE = -1,
} ethernet_status_t;

/**
 * @brief Set the first Ethernet interface as the default one.
 *
 * @retval ETHERNET_OK If successfully set or already set.
 * @retval ETHERNET_ERR_NO_INTERFACE If no Ethernet interface found.
 */
ethernet_status_t ethernet_set_iface_default(void);

/**
 * @brief Check if the Ethernet interface is currently the default one.
 *
 * @retval true If Ethernet interface is default
 * @retval false If not or not found
 */
bool is_ethernet_iface_default(void);

/**
 * @brief Check if the given interface is an Ethernet interface.
 *
 * @param iface Pointer to the network interface to check.
 * @return true if the interface is Ethernet, false otherwise.
 */
bool is_ethernet_iface(struct net_if *iface);

#endif // ETHERNET_UTILS_H
