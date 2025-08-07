/**
 * @file dev_info.h
 * @brief Board and firmware identification module.
 *
 * This module reads board revision via GPIO pins and exposes functions
 * to access hardware revision and firmware version in string or numeric form.
 *
 * @author Eugene K.
 * @copyright (C) 2025 Alnicko Lab OU. All rights reserved.
 */

#ifndef DEV_INFO_H
#define DEV_INFO_H
#include <stdlib.h>

struct dev_info_fw_version {
    int major;
    int minor;
    int patch;
};

typedef enum {
    DEV_INFO_BOARD_DEV = 0,
    DEV_INFO_BOARD_SPEAKER,
    DEV_INFO_BOARD_IO_MODULE,
} dev_info_board_name_t;

/**
 * @brief Get the detected board revision code.
 *
 * @return Revision number (1-15), or 0 if not initialized.
 */
uint8_t dev_info_get_hw_revision_num(void);

/**
 * @brief Check if board revision was successfully detected.
 *
 * @retval true if ready
 * @retval false if not initialized properly
 */
bool dev_info_hw_revision_is_ready(void);

/**
 * @brief Get a hardware revision string
 * The hardware revision is compile time resolved, depends on a board the
 * firmware is compiled for.
 * 
 * @param [out] len Length of the hardware revision string
 * @return Hardware revision string in with a format Rxx, where xx is a number
 */
const char *dev_info_get_hw_revision_str(size_t *len);

/**
 * @brief Get a firmware version string
 * 
 * @param [out] len Length of the firmware version string
 * @return Firmware version string in the semantic versioning format
 */
const char *dev_info_get_fw_version_str(size_t *len);

/**
 * @brief Get a firmware version in a numeric format
 * 
 * @return Firmware version 
 */
const struct dev_info_fw_version *dev_info_get_fw_version_num(void);

/**
 * @brief Get the board name as an enum value.
 *
 * @return Board name enum value.
 */
dev_info_board_name_t dev_info_get_board_name_num(void);

/**
 * @brief Get the board name as a human-readable string.
 *
 * @return Pointer to string with board name.
 */
const char *dev_info_get_board_name_str(void);

/**
 * @brief Retrieve the device ID as a hexadecimal string.
 *
 * @param [out] len Pointer to a size_t variable to store the length of the ID string.
 * @return Pointer to a static string containing the device ID.
 */
const char *dev_info_get_device_id_str(size_t *len);

#endif // DEV_INFO_H
