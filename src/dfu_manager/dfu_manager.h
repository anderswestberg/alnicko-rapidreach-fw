/**
 * @file dfu_manager.h
 * @brief Device Firmware Update (DFU) manager interface for handling firmware upgrades.
 *
 * This module provides a high-level interface for performing firmware updates using MCUboot.
 * It supports the following functionalities:
 *
 * - Initializing flash storage for image updates.
 * - Writing data chunks to the secondary image slot.
 * - Reading secondary firmware version.
 * - Confirming or checking the currently running image.
 * - Triggering firmware upgrades on the next boot (temporary or permanent).
 * - Optionally verifying image integrity using SHA-256 hash comparison.
 * 
 * @author Eugene K.
 * @copyright (C) 2025 Alnicko Lab OU. All rights reserved.
 */

#ifndef __DFU_MANAGER_H__
#define __DFU_MANAGER_H__
#include <zephyr/dfu/flash_img.h>

#define UPD_VERSION_STRING_MAX_LEN 16

struct dfu_storage_context {
    struct flash_img_context flash_ctx;
};

struct dfu_fw_update_version {
    int major;
    int minor;
    int patch;
};

#ifdef CONFIG_IMG_ENABLE_IMAGE_CHECK
typedef enum {
    UPD_IMG_INTEGRITY_UNVERIFIED = 0, // Integrity check has not been performed.
    UPD_IMG_INTEGRITY_VALID,          // Image hash matches the expected value.
    UPD_IMG_INTEGRITY_INVALID // Image hash does not match the expected value.
} dfu_upd_img_integrity_t;
#endif // CONFIG_IMG_ENABLE_IMAGE_CHECK

/**
 * @brief Get the firmware version string from the secondary slot.
 *
 * @param version Pointer to the output buffer to store the version string.
 * @param version_len Length of the output buffer.
 *
 * @return true if the version was successfully read and formatted, false otherwise.
 */
bool dfu_get_fw_update_version_str(char *version, int version_len);

/**
 * @brief Get the firmware version numbers from the secondary slot.
 *
 * @param version Pointer to a dfu_fw_update_version structure to be filled.
 *
 * @return true if the version was successfully read, false otherwise.
 */
bool dfu_get_fw_update_version_num(struct dfu_fw_update_version *version);

/**
 * @brief Marks the currently running image as confirmed. 
 *
 * @return 0 on success, negative errno code on fail. 
 */
int dfu_mark_current_img_as_confirmed(void);

/**
 * @brief Check if the currently running image is confirmed.
 *
 * @return 0 on success, negative errno code on fail. 
 */
int dfu_is_current_img_confirmed(void);

/**
 * @brief Initialize the storage for a DFU update.
 *
 * @param ctx Pointer to the DFU storage context to initialize.
 *
 * @return 0 on success, negative errno code on fail.
 */
int dfu_update_storage_init(struct dfu_storage_context *ctx);

/**
 * @brief Process input buffers to be written to the update image slot. 
 *
 * @param ctx   Pointer to the DFU storage context.
 * @param data  Pointer to the data buffer to write.
 * @param size  Size of the data in bytes.
 * @param flush When true this forces any buffered data to be written to flash.
 *
 * @return 0 on success, negative errno code on fail 
 */
int dfu_storage_write(struct dfu_storage_context *ctx,
                      const uint8_t              *data,
                      const size_t                size,
                      const bool                  flush);

#ifdef CONFIG_IMG_ENABLE_IMAGE_CHECK

/**
 * @brief Check the integrity of the firmware image in the update slot.
 *
 * @param ctx  Pointer to the DFU storage context.
 * @param hash Pointer to the expected hash value.
 * @param size Size of the file in bytes.
 *
 * @return 0 on success, negative errno code on fail 
 */
int dfu_update_flash_img_check(struct dfu_storage_context *ctx,
                               const uint8_t              *hash,
                               const size_t                size);

/**
 * @brief Get the result of the last firmware image integrity check.
 *
 * @return Current update image integrity status.
 */
dfu_upd_img_integrity_t dfu_get_upd_img_integrity(void);

#endif // CONFIG_IMG_ENABLE_IMAGE_CHECK

/**
 * @brief Request a test firmware upgrade on next reboot.
 *
 * This function sets the upgrade flag for the secondary image.
 * Run image once, then confirm or revert.
 *
 * @return 0 on success, negative errno code on fail. 
 */
int dfu_request_upgrade(void);

/**
 * @brief Request a permanent firmware upgrade on next reboot.
 *
 * This function sets the upgrade flag for the secondary image
 * and marks it as confirmed, so no manual confirmation is needed after reboot.
 *
 * @return 0 on success, negative errno code on fail. 
 */
int dfu_request_upgrade_with_confirmed_img(void);

#endif //__DFU_MANAGER_H__