/**
 * @file dfu_manager.c
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

#include <zephyr/kernel.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/logging/log.h>

#include "dfu_manager.h"

LOG_MODULE_REGISTER(dfu_manager, CONFIG_RPR_MODULE_DFU_LOG_LEVEL);

#define DFU_SLOT_PARTITION_0 FIXED_PARTITION_ID(slot0_partition)
#define DFU_SLOT_PARTITION_1 FIXED_PARTITION_ID(slot1_partition)

#ifdef CONFIG_IMG_ENABLE_IMAGE_CHECK
struct dfu_upd_img_integrity_info {
    dfu_upd_img_integrity_t status;
};

static struct dfu_upd_img_integrity_info upd_img_integrity_info = {
    .status = UPD_IMG_INTEGRITY_UNVERIFIED,
};
#endif

/**
 * @brief Get the firmware version string from the secondary slot.
 *
 * @param version Pointer to the output buffer to store the version string.
 * @param version_len Length of the output buffer.
 *
 * @return true if the version was successfully read and formatted, false otherwise.
 */
bool dfu_get_fw_update_version_str(char *version, int version_len)
{
    if (!version) {
        LOG_ERR("Version pointer is NULL");
        return false;
    }

    struct mcuboot_img_header header;

    int ret = boot_read_bank_header(
            DFU_SLOT_PARTITION_1, &header, sizeof(struct mcuboot_img_header));

    if (ret != 0) {
        LOG_DBG("Failed to read update bank header (code: %d)", ret);
        return false;
    }

    if (header.mcuboot_version != 1) {
        LOG_DBG("MCUboot header version not supported!");
        return false;
    }

    snprintf(version,
             version_len,
             "%d.%d.%d",
             header.h.v1.sem_ver.major,
             header.h.v1.sem_ver.minor,
             header.h.v1.sem_ver.revision);

    return true;
}

/**
 * @brief Get the firmware version numbers from the secondary slot.
 *
 * @param version Pointer to a dfu_fw_update_version structure to be filled.
 *
 * @return true if the version was successfully read, false otherwise.
 */
bool dfu_get_fw_update_version_num(struct dfu_fw_update_version *version)
{
    if (!version) {
        LOG_ERR("Version pointer is NULL");
        return false;
    }
    struct mcuboot_img_header header;

    int ret = boot_read_bank_header(
            DFU_SLOT_PARTITION_1, &header, sizeof(struct mcuboot_img_header));

    if (ret != 0) {
        LOG_DBG("Failed to read update bank header (code: %d)", ret);

        return false;
    }

    if (header.mcuboot_version != 1) {
        LOG_DBG("MCUboot header version not supported!");
        return false;
    }

    version->major = header.h.v1.sem_ver.major;
    version->minor = header.h.v1.sem_ver.minor;
    version->patch = header.h.v1.sem_ver.revision;

    return true;
}

/**
 * @brief Marks the currently running image as confirmed. 
 *
 * @return 0 on success, negative errno code on fail. 
 */
int dfu_mark_current_img_as_confirmed(void)
{

    int ret = boot_write_img_confirmed();
    if (ret < 0) {

        LOG_ERR("Failed to confirm the current image (code: %d)", ret);
    }

    return ret;
}

/**
 * @brief Check if the currently running image is confirmed.
 *
 * @return 0 on success, negative errno code on fail. 
 */
int dfu_is_current_img_confirmed(void)
{

    int ret = boot_is_img_confirmed();
    if (ret < 0) {

        LOG_ERR("Failed to check if the current image is confirmed (code: %d)",
                ret);
    }

    return ret;
}

/**
 * @brief Initialize the storage for a DFU update.
 *
 * @param ctx Pointer to the DFU storage context to initialize.
 *
 * @return 0 on success, negative errno code on fail.
 */
int dfu_update_storage_init(struct dfu_storage_context *ctx)
{
    if (ctx == NULL) {
        LOG_ERR("DFU context is NULL");
        return -EINVAL;
    }

    int ret = boot_erase_img_bank(DFU_SLOT_PARTITION_1);
    if (ret != 0) {
        LOG_ERR("Failed to erase image bank (code: %d)", ret);
        return ret;
    }

    ret = flash_img_init(&ctx->flash_ctx);
    if (ret < 0) {
        LOG_ERR("Failed to init flash image context (code: %d)", ret);
    }

    return ret;
}

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
                      const bool                  flush)
{
    if (ctx == NULL || (size > 0 && data == NULL)) {
        LOG_ERR("Invalid arguments: ctx=%p, data=%p, size=%zu", ctx, data, size);
        return -EINVAL;
    }

    int ret = flash_img_buffered_write(&ctx->flash_ctx, data, size, flush);
    if (ret < 0) {
        LOG_ERR("Failed to write data to flash (code: %d)", ret);
    }

    return ret;
}

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
                               const size_t                size)
{
    if (ctx == NULL || hash == NULL || size == 0) {
        LOG_ERR("Invalid parameters passed to image check");
        return -EINVAL;
    }

    const struct flash_img_check fic = {
        .match = hash,
        .clen  = size,
    };

    int ret = flash_img_check(&ctx->flash_ctx, &fic, DFU_SLOT_PARTITION_1);

    if (ret == 0) {
        LOG_INF("Image integrity check passed");
        upd_img_integrity_info.status = UPD_IMG_INTEGRITY_VALID;
    } else {
        LOG_ERR("Image integrity check failed (code: %d)", ret);
        upd_img_integrity_info.status = UPD_IMG_INTEGRITY_INVALID;
    }

    return ret;
}

/**
 * @brief Get the result of the last firmware image integrity check.
 *
 * @return Current update image integrity status.
 */
dfu_upd_img_integrity_t dfu_get_upd_img_integrity(void)
{
    return upd_img_integrity_info.status;
}

#endif

/**
 * @brief Request firmware upgrade on next reboot.
 *
 * This function sets the upgrade flag for the secondary image.
 * Run image once, then confirm or revert.
 *
 * @return 0 on success, negative errno code on fail. 
 */
int dfu_request_upgrade(void)
{
    int ret = boot_request_upgrade(BOOT_UPGRADE_TEST);

    if (ret < 0) {
        LOG_ERR("Failed to request image upgrade (code: %d)", ret);
    }

    return ret;
}

/**
 * @brief Request a permanent firmware upgrade on next reboot.
 *
 * This function sets the upgrade flag for the secondary image
 * and marks it as confirmed, so no manual confirmation is needed after reboot.
 *
 * @return 0 on success, negative errno code on fail. 
 */
int dfu_request_upgrade_with_confirmed_img(void)
{
    int ret = boot_request_upgrade(BOOT_UPGRADE_PERMANENT);

    if (ret < 0) {
        LOG_ERR("Failed to request permanent image upgrade (code: %d)", ret);
    }

    return ret;
}
