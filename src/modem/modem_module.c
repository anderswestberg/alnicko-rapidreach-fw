/**
 * @file modem_module.c
 * @brief Modem initialization and connection module for Zephyr-based systems.
 *
 * This module provides an interface to power on, initialize, and establish a network
 * connection via a cellular modem using Zephyr's networking and management 
 * APIs for C16QS modem.
 *
 * @author Eugene K.
 * @copyright (C) 2025 Alnicko Lab OU. All rights reserved.
 */

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <string.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/drivers/cellular.h>

#include "modem_module.h"

LOG_MODULE_REGISTER(modem_module, CONFIG_RPR_MODEM_LOG_LEVEL);

#define EVENT_MASK (NET_EVENT_IPV4_ADDR_ADD | NET_EVENT_IPV4_ADDR_DEL)

#define MODEM_ENABLE_SPEC GPIO_DT_SPEC_GET(DT_ALIAS(modemen), gpios)

#define MODEM_DEVICE_GET DEVICE_DT_GET(DT_ALIAS(modem))

#define MODEM_ENABLE_VALUE     1
#define MODEM_DISABLE_VALUE    0
#define MODEM_STARTUP_DELAY_MS 500

/**
 * @brief Structure holding modem device and interface
 */
struct modem_c16qs_t {
    const struct device           *modem;
    struct net_if                 *iface;
    struct net_mgmt_event_callback mgmt_cb;
    bool                           is_modem_init;
    bool                           is_modem_connected;
    struct gpio_dt_spec            modem_en;
    bool                           is_device_ready;
};

static struct modem_c16qs_t c16sq = {
    .modem              = MODEM_DEVICE_GET,
    .is_modem_init      = false,
    .is_modem_connected = false,
    .modem_en           = MODEM_ENABLE_SPEC,
    .is_device_ready    = false,
};

/**
 * @brief Network management event handler for C16QS modem.
 *
 * This function handles relevant network management events (L4_CONNECTED and
 * L4_DISCONNECTED) for the C16QS LTE modem. It updates the modem connection
 * state and logs the corresponding events.
 *
 * Only events from the associated modem interface are processed.
 */
static void c16qs_mgmt_event_handler(struct net_mgmt_event_callback *cb,
                                     uint32_t                        mgmt_event,
                                     struct net_if                  *iface)
{
    if ((mgmt_event & EVENT_MASK) != mgmt_event) {
        return;
    }

    if (iface != c16sq.iface) {
        return;
    }

    switch (mgmt_event) {
    case NET_EVENT_IPV4_ADDR_ADD:
        c16sq.is_modem_connected = true;
        LOG_INF("Cellular modem connected");
        break;

    case NET_EVENT_IPV4_ADDR_DEL:
        c16sq.is_modem_connected = false;
        LOG_INF("Cellular modem disconnected");
        break;

    default:
        break;
    }
}

/**
 * @brief Get the current RSSI (signal strength) from the modem.
 *
 * Retrieves RSSI and writes the value to the provided pointer.
 *
 * @param rssi Pointer to int16_t variable to store RSSI.
 * @return c16qs_modem_status_t Error code or MODEM_SUCCESS on success.
 */
c16qs_modem_status_t c16qs_get_signal_rssi(int16_t *rssi)
{
    if (!c16sq.is_device_ready) {
        LOG_ERR("Modem module is not ready");
        return MODEM_ERR_NO_DEV;
    }

    if (!c16sq.is_modem_init) {
        LOG_ERR("Modem is not initialized");
        return MODEM_ERR_NOT_INIT;
    }
    if (cellular_get_signal(c16sq.modem, CELLULAR_SIGNAL_RSSI, rssi)) {
        LOG_ERR("Failed to retrieve RSSI modem info");
        return MODEM_ERR_INFO_RETRIEVAL;
    }
    return MODEM_SUCCESS;
}

/**
 * @brief Retrieve basic cellular information from the modem.
 *
 * Fills the provided modem_info_t structure with IMEI, model ID,
 * manufacturer, SIM IMSI, ICCID, and firmware version.
 *
 * @param info Pointer to modem_info_t structure to populate.
 * @return c16qs_modem_status_t Error code or MODEM_SUCCESS on success.
 */
c16qs_modem_status_t c16qs_get_cellular_info(modem_info_t *info)
{
    int                  rc;
    const struct device *modem = c16sq.modem;

    if (!c16sq.is_device_ready) {
        LOG_ERR("Modem module is not ready");
        return MODEM_ERR_NO_DEV;
    }

    if (!c16sq.is_modem_init) {
        LOG_ERR("Modem is not initialized");
        return MODEM_ERR_NOT_INIT;
    }

    rc = cellular_get_modem_info(
            modem, CELLULAR_MODEM_INFO_IMEI, info->imei, sizeof(info->imei));
    if (rc) {
        LOG_ERR("Failed to retrieve IMEI modem info");
        return MODEM_ERR_INFO_RETRIEVAL;
    }
    rc = cellular_get_modem_info(modem,
                                 CELLULAR_MODEM_INFO_MODEL_ID,
                                 info->model_id,
                                 sizeof(info->model_id));
    if (rc) {
        LOG_ERR("Failed to retrieve MODEL_ID modem info");
        return MODEM_ERR_INFO_RETRIEVAL;
    }
    rc = cellular_get_modem_info(modem,
                                 CELLULAR_MODEM_INFO_MANUFACTURER,
                                 info->manufacturer,
                                 sizeof(info->manufacturer));
    if (rc) {
        LOG_ERR("Failed to retrieve MANUFACTURER modem info");
        return MODEM_ERR_INFO_RETRIEVAL;
    }
    rc = cellular_get_modem_info(modem,
                                 CELLULAR_MODEM_INFO_SIM_IMSI,
                                 info->sim_imsi,
                                 sizeof(info->sim_imsi));
    if (rc) {
        LOG_ERR("Failed to retrieve SIM_IMSI modem info");
        return MODEM_ERR_INFO_RETRIEVAL;
    }
    rc = cellular_get_modem_info(modem,
                                 CELLULAR_MODEM_INFO_SIM_ICCID,
                                 info->sim_iccid,
                                 sizeof(info->sim_iccid));
    if (rc) {
        LOG_ERR("Failed to retrieve SIM_ICCID modem info");
        return MODEM_ERR_INFO_RETRIEVAL;
    }
    rc = cellular_get_modem_info(modem,
                                 CELLULAR_MODEM_INFO_FW_VERSION,
                                 info->fw_version,
                                 sizeof(info->fw_version));
    if (rc) {
        LOG_ERR("Failed to retrieve FW_VERSION modem info");
        return MODEM_ERR_INFO_RETRIEVAL;
    }
    return MODEM_SUCCESS;
}

/**
 * @brief Initialize and connect the C16QS modem.
 *
 * Powers on the modem and brings up the PPP network interface.
 *
 * @return c16qs_modem_status_t Error code or MODEM_SUCCESS on success.
 */
c16qs_modem_status_t modem_init_and_connect(void)
{
    int ret;

    if (!c16sq.is_device_ready) {
        LOG_ERR("Modem module is not ready");
        return MODEM_ERR_NO_DEV;
    }

    ret = gpio_pin_set_dt(&c16sq.modem_en, MODEM_ENABLE_VALUE);
    if (ret < 0) {
        LOG_ERR("Failed to enable modem (err %d)", ret);
        return MODEM_ERR_INIT_FAILED;
    }
    k_msleep(MODEM_STARTUP_DELAY_MS);

    if (!c16sq.is_modem_init) {
        LOG_DBG("Powering on modem");
        ret = pm_device_action_run(c16sq.modem, PM_DEVICE_ACTION_RESUME);
        if (ret < 0) {
            LOG_ERR("Failed to start up modem");
            modem_shutdown();
            return MODEM_ERR_INIT_FAILED;
        }
        c16sq.is_modem_init = true;
    }

    if (!c16sq.is_modem_connected) {
        LOG_DBG("Bring up network interface");
        ret = net_if_up(c16sq.iface);
        if (ret == -EALREADY) {
            LOG_WRN("Network interface already up");
        } else if (ret < 0) {
            LOG_ERR("Failed to bring up network interface %d", ret);
            modem_shutdown();
            return MODEM_ERR_IFACE_UP;
        }
    }

    return MODEM_SUCCESS;
}

/**
 * @brief Restart the C16QS modem.
 *
 * Suspends and resumes the modem.
 *
 * @return c16qs_modem_status_t Error code or MODEM_SUCCESS on success.
 */
c16qs_modem_status_t modem_reset(void)
{
    int ret;

    if (!c16sq.is_device_ready) {
        LOG_ERR("Modem module is not ready");
        return MODEM_ERR_NO_DEV;
    }

    LOG_DBG("Restart modem");
    if (c16sq.is_modem_init) {
        ret = pm_device_action_run(c16sq.modem, PM_DEVICE_ACTION_SUSPEND);
        if (ret != 0) {
            LOG_ERR("Failed to power down modem");
        }
        c16sq.is_modem_init = false;
    } else {
        ret = gpio_pin_set_dt(&c16sq.modem_en, MODEM_ENABLE_VALUE);
        if (ret < 0) {
            LOG_ERR("Failed to enable modem (err %d)", ret);
            return MODEM_ERR_INIT_FAILED;
        }
        k_msleep(MODEM_STARTUP_DELAY_MS);
    }

    ret = pm_device_action_run(c16sq.modem, PM_DEVICE_ACTION_RESUME);
    if (ret < 0) {
        LOG_ERR("Failed to start up modem");
        return MODEM_ERR_INIT_FAILED;
    }
    c16sq.is_modem_init = true;

    if (!c16sq.is_modem_connected) {

        LOG_DBG("Bring up network interface");
        ret = net_if_up(c16sq.iface);

        if (ret == -EALREADY) {
            LOG_WRN("Network interface already up");
        } else if (ret < 0) {
            LOG_ERR("Failed to bring up network interface %d", ret);
            return MODEM_ERR_IFACE_UP;
        }
    }

    return MODEM_SUCCESS;
}

/**
 * @brief Shutdown the C16QS modem and network interface.
 *
 * Brings down the network interface and powers off the modem.
 *
 * @return c16qs_modem_status_t Error code or MODEM_SUCCESS on success.
 */
c16qs_modem_status_t modem_shutdown(void)
{
    c16qs_modem_status_t status = MODEM_SUCCESS;
    int                  ret    = -1;

    if (!c16sq.is_device_ready) {
        LOG_ERR("Modem module is not ready");
        return MODEM_ERR_NO_DEV;
    }

    ret = net_if_down(c16sq.iface);

    if (ret == -EALREADY) {
        LOG_WRN("Network interface already down");
    } else if (ret < 0) {
        LOG_ERR("Failed to bring down network interface");
        status = MODEM_ERR_IFACE_DOWN;
    }

    if (c16sq.is_modem_init) {
        LOG_DBG("Powering down modem");
        ret = pm_device_action_run(c16sq.modem, PM_DEVICE_ACTION_SUSPEND);
        if (ret != 0) {
            LOG_ERR("Failed to power down modem");
            status = MODEM_ERR_POWER_DOWN;
        }
        c16sq.is_modem_init = false;
    }

    ret = gpio_pin_set_dt(&c16sq.modem_en, MODEM_DISABLE_VALUE);

    c16sq.is_modem_connected = false;

    if (ret < 0) {
        LOG_ERR("Failed to disable modem (err %d)", ret);
        status = MODEM_ERR_POWER_DOWN;
    }
    return status;
}

/**
 * @brief Check whether the modem has been initialized.
 *
 * @return true if the modem is initialized, false otherwise.
 */
bool is_modem_initialized(void)
{
    return c16sq.is_modem_init;
}

/**
 * @brief Check whether the modem has been L4 connected.
 *
 * @return true if the modem is L4 connected, false otherwise.
 */
bool is_modem_connected(void)
{
    return c16sq.is_modem_connected;
}

/**
 * @brief Set the first PPP modem interface as the default network interface.
 *
 * @retval MODEM_SUCCESS         If the modem interface was successfully set as default.
 * @retval MODEM_ERR_NO_INTERFACE If no PPP interface was found (modem not initialized or not connected).
 */
c16qs_modem_status_t modem_set_iface_default(void)
{
    struct net_if *iface = net_if_get_first_by_type(&NET_L2_GET_NAME(PPP));
    if (!iface) {
        LOG_ERR("Failed to find any Modem PPP interface");
        return MODEM_ERR_NO_INTERFACE;
    }

    net_if_set_default(iface);
    LOG_DBG("Modem PPP interface set as default");
    return MODEM_SUCCESS;
}

/**
 * @brief Check whether the modem PPP interface is currently the default network interface.
 * 
 * @retval true  If the modem PPP interface is the default interface.
 * @retval false If it's not, or if no PPP interface is available.
 */
bool is_modem_iface_default(void)
{
    struct net_if *iface     = net_if_get_first_by_type(&NET_L2_GET_NAME(PPP));
    struct net_if *iface_def = net_if_get_default();

    return iface && (iface == iface_def);
}

/**
 * @brief Check if the given interface is a modem (PPP) interface.
 *
 * @param iface Pointer to the network interface to check.
 * @return true if the interface is a modem (PPP), false otherwise.
 */
bool is_modem_iface(struct net_if *iface)
{
    return iface && (iface == net_if_get_first_by_type(&NET_L2_GET_NAME(PPP)));
}

/**
  * @brief Initialize modem enable GPIO and modem device check.
  *
  * @return 0 on success, negative error code otherwise.
  */
static int modem_init(void)
{
    if (!device_is_ready(c16sq.modem)) {
        LOG_ERR("Modem device '%s' is not ready.", c16sq.modem->name);
        return -ENODEV;
    }

    if (!device_is_ready(c16sq.modem_en.port)) {
        LOG_ERR("GPIO device not ready");
        return -ENODEV;
    }

    int ret = gpio_pin_configure_dt(&c16sq.modem_en, GPIO_OUTPUT_LOW);

    if (ret < 0) {
        LOG_ERR("Failed to configure modem enable GPIO (err %d)", ret);
        return ret;
    }

    c16sq.iface = net_if_get_first_by_type(&NET_L2_GET_NAME(PPP));

    if (!c16sq.iface) {
        LOG_ERR("iface network interface is not initialized");
        return -EIO;
    }

    net_mgmt_init_event_callback(
            &c16sq.mgmt_cb, c16qs_mgmt_event_handler, EVENT_MASK);
    net_mgmt_add_event_callback(&c16sq.mgmt_cb);

    c16sq.is_device_ready = true;
    LOG_DBG("Modem module '%s' is ready.", c16sq.modem->name);

    return 0;
}

SYS_INIT(modem_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
