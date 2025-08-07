/**
 * @file dev_info.c
 * @brief Board and firmware identification module.
 *
 * This module reads board revision via GPIO pins and exposes functions
 * to access hardware revision and firmware version in string or numeric form.
 *
 * @author Eugene K.
 * @copyright (C) 2025 Alnicko Lab OU. All rights reserved.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/hwinfo.h>

#include "app_version.h"
#include "dev_info.h"

#define VERSION_STRING_MAX_LEN 16
#define HARDWARE_REVISION_MAX_LEN \
    5 ///< Maximum length of the HW revision string (Rxx\0)

#define DEVICE_ID_HEX_MAX_SIZE ((CONFIG_RPR_DEVICE_ID_BIN_MAX_SIZE * 2) + 1)

LOG_MODULE_REGISTER(dev_info, CONFIG_RPR_MODULE_DEV_INFO_LOG_LEVEL);

#define REV_GPIO_0_NODE DT_ALIAS(rev0)
#define REV_GPIO_1_NODE DT_ALIAS(rev1)
#define REV_GPIO_2_NODE DT_ALIAS(rev2)
#define REV_GPIO_3_NODE DT_ALIAS(rev3)

#define REV_GPIO_0_SPEC GPIO_DT_SPEC_GET(REV_GPIO_0_NODE, gpios)
#define REV_GPIO_1_SPEC GPIO_DT_SPEC_GET(REV_GPIO_1_NODE, gpios)
#define REV_GPIO_2_SPEC GPIO_DT_SPEC_GET(REV_GPIO_2_NODE, gpios)
#define REV_GPIO_3_SPEC GPIO_DT_SPEC_GET(REV_GPIO_3_NODE, gpios)

struct dev_info_config {
    struct gpio_dt_spec rev_gpios[4];
    bool                is_ready;
    uint8_t             hw_revision_code;
};

static struct dev_info_config info_cfg = {
    .rev_gpios = {
        REV_GPIO_0_SPEC,
        REV_GPIO_1_SPEC,
        REV_GPIO_2_SPEC,
        REV_GPIO_3_SPEC,
    },
    .is_ready = false,
    .hw_revision_code = 0,
};

/**
 * @brief Get device identity in hex format
 *
 * @param id          Output buffer for the hex string.
 * @param id_max_len  Size of the output buffer (must be enough to hold 2 * binary size + 1).
 *
 * @return true on success, false if failed to get or convert ID.
 */
static bool get_device_identity(char *id, uint8_t id_max_len)
{
    if (!id || id_max_len == 0)
        return false;

    uint8_t hwinfo_id[CONFIG_RPR_DEVICE_ID_BIN_MAX_SIZE];
    ssize_t length;

    length = hwinfo_get_device_id(hwinfo_id, CONFIG_RPR_DEVICE_ID_BIN_MAX_SIZE);
    if (length <= 0) {
        return false;
    }

    memset(id, 0, id_max_len);
    length = bin2hex(hwinfo_id, (size_t)length, id, id_max_len);

    return length > 0;
}

/**
 * @brief Initialize GPIOs and detect board revision and fw version.
 *
 * @return 0 on success, negative error code otherwise.
 */
static int dev_info_init(void)
{
    int     ret;
    uint8_t code = 0;

    for (int i = 0; i < 4; i++) {
        if (!device_is_ready(info_cfg.rev_gpios[i].port)) {
            LOG_ERR("Revision GPIO %d not ready", i);
            return -ENODEV;
        }

        ret = gpio_pin_configure_dt(&info_cfg.rev_gpios[i], GPIO_INPUT);
        if (ret < 0) {
            LOG_ERR("Failed to configure revision GPIO %d", i);
            return ret;
        }

        int value = gpio_pin_get_dt(&info_cfg.rev_gpios[i]);
        if (value < 0) {
            LOG_ERR("Failed to read revision GPIO %d", i);
            return value;
        }

        code |= (value & 0x01) << i;
    }

    info_cfg.hw_revision_code = code;
    LOG_INF("Board revision detected: R%02d", code);
    LOG_INF("Firmware version detected: %d.%d.%d",
            APP_VERSION_MAJOR,
            APP_VERSION_MINOR,
            APP_PATCHLEVEL);

#if defined(CONFIG_BOARD_SPEAKER)
    LOG_INF("Board name detected: Speaker Board");
#elif defined(CONFIG_BOARD_IO_MODULE)
    LOG_INF("Board name detected: I/O Module Board");
#else
    LOG_INF("Board name detected: Development Board");
#endif

    char device_id[DEVICE_ID_HEX_MAX_SIZE];

    bool get_dev = get_device_identity(device_id, DEVICE_ID_HEX_MAX_SIZE);

    if (get_dev) {
        LOG_INF("Device ID detected: %s", device_id);
    } else {
        LOG_ERR("Failed to retrieve device ID");
        return -EIO;
    }

    info_cfg.is_ready = true;

    return 0;
}

SYS_INIT(dev_info_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

/**
 * @brief Get the detected board revision code.
 *
 * @return Revision number (1-15), or 0 if not initialized.
 */
uint8_t dev_info_get_hw_revision_num(void)
{
    return info_cfg.is_ready ? info_cfg.hw_revision_code : 0;
}

/**
 * @brief Check if board revision was successfully detected.
 *
 * @retval true if ready
 * @retval false if not initialized properly
 */
bool dev_info_hw_revision_is_ready(void)
{
    return info_cfg.is_ready;
}

/**
 * @brief Get a hardware revision string
 * The hardware revision is compile time resolved, depends on a board the
 * firmware is compiled for.
 * 
 * @param [out] len Length of the hardware revision string
 * @return Hardware revision string in with a format Rxx, where xx is a number
 */
const char *dev_info_get_hw_revision_str(size_t *len)
{
    static char formatted_revision[HARDWARE_REVISION_MAX_LEN];

    int ret = snprintf(formatted_revision,
                       sizeof(formatted_revision),
                       "R%02d",
                       info_cfg.hw_revision_code);

    if (len) {
        *len = ret > 0 ? ret : 0;
    }

    return formatted_revision;
}

/**
 * @brief Get a firmware version string
 * 
 * @param [out] len Length of the firmware version string
 * @return Firmware version string in the semantic versioning format
 */
const char *dev_info_get_fw_version_str(size_t *len)
{
    static char version_str[VERSION_STRING_MAX_LEN];

    int ret = snprintf(version_str,
                       sizeof(version_str),
                       "%d.%d.%d",
                       APP_VERSION_MAJOR,
                       APP_VERSION_MINOR,
                       APP_PATCHLEVEL);

    if (len) {
        *len = ret > 0 ? ret : 0;
    }

    return version_str;
}

/**
 * @brief Get a firmware version in a numeric format
 * 
 * @return Firmware version 
 */
const struct dev_info_fw_version *dev_info_get_fw_version_num(void)
{
    static const struct dev_info_fw_version version = {
        .major = APP_VERSION_MAJOR,
        .minor = APP_VERSION_MINOR,
        .patch = APP_PATCHLEVEL
    };

    return &version;
}

/**
 * @brief Get the board name as an enum value.
 *
 * @return Board name enum value.
 */
dev_info_board_name_t dev_info_get_board_name_num(void)
{
#if defined(CONFIG_BOARD_SPEAKER)
    return DEV_INFO_BOARD_SPEAKER;
#elif defined(CONFIG_BOARD_IO_MODULE)
    return DEV_INFO_BOARD_IO_MODULE;
#else
    return DEV_INFO_BOARD_DEV;
#endif
}

/**
 * @brief Get the board name as a human-readable string.
 *
 * @return Pointer to string with board name.
 */
const char *dev_info_get_board_name_str(void)
{
#if defined(CONFIG_BOARD_SPEAKER)
    static char board_name_str[] = "Speaker Board";
#elif defined(CONFIG_BOARD_IO_MODULE)
    static char board_name_str[] = "I/O Module Board";
#else
    static char board_name_str[] = "Development Board";
#endif
    return board_name_str;
}

/**
 * @brief Retrieve the device ID as a hexadecimal string.
 *
 * @param [out] len Pointer to a size_t variable to store the length of the ID string.
 * @return Pointer to a static string containing the device ID.
 */
const char *dev_info_get_device_id_str(size_t *len)
{
    static char device_id[DEVICE_ID_HEX_MAX_SIZE];

    bool get_dev = get_device_identity(device_id, DEVICE_ID_HEX_MAX_SIZE);

    *len = get_dev ? DEVICE_ID_HEX_MAX_SIZE : 0;

    return device_id;
}
