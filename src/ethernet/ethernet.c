/**
 * @file ethernet.c
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

#include <zephyr/net/net_if.h>
#include <zephyr/logging/log.h>

#include "ethernet.h"

LOG_MODULE_REGISTER(ethernet_utils, CONFIG_RPR_ETHERNET_LOG_LEVEL);

/**
 * @brief Set the first Ethernet interface as the default one.
 *
 * @retval ETHERNET_OK If successfully set or already set.
 * @retval ETHERNET_ERR_NO_INTERFACE If no Ethernet interface found.
 */
ethernet_status_t ethernet_set_iface_default(void)
{
    struct net_if *iface = net_if_get_first_by_type(&NET_L2_GET_NAME(ETHERNET));
    if (!iface) {
        LOG_ERR("Failed to find any Ethernet interface");
        return ETHERNET_ERR_NO_INTERFACE;
    }

    net_if_set_default(iface);
    LOG_DBG("Ethernet interface set as default");
    return ETHERNET_OK;
}

/**
 * @brief Check if the Ethernet interface is currently the default one.
 *
 * @retval true If Ethernet interface is default
 * @retval false If not or not found
 */
bool is_ethernet_iface_default(void)
{
    struct net_if *iface = net_if_get_first_by_type(&NET_L2_GET_NAME(ETHERNET));
    struct net_if *iface_def = net_if_get_default();

    return iface && (iface == iface_def);
}

/**
 * @brief Check if the given interface is an Ethernet interface.
 *
 * @param iface Pointer to the network interface to check.
 * @return true if the interface is Ethernet, false otherwise.
 */
bool is_ethernet_iface(struct net_if *iface)
{
    return iface &&
           (iface == net_if_get_first_by_type(&NET_L2_GET_NAME(ETHERNET)));
}
