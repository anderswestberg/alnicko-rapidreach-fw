/**
 * @file file_manager.c
 * @brief LittleFS filesystem initialization and setup module.
 *
 * This module sets up the LittleFS filesystem on a dedicated partition,
 * mounts it at a configurable mount point, and ensures that a default
 * subdirectory (e.g., for storing downloaded audio files) exists.
 *
 * It is initialized automatically at application startup via SYS_INIT.
 * 
 * @author Eugene K.
 * @copyright (C) 2025 Alnicko Lab OU. All rights reserved.
 */

#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/storage/flash_map.h>

#define HAS_FLASH_COMPAT 1
#if DT_HAS_COMPAT_STATUS_OKAY(jedec_spi_nor)
#define SPI_FLASH_COMPAT jedec_spi_nor
#elif DT_HAS_COMPAT_STATUS_OKAY(st_stm32_xspi_nor)
#define SPI_FLASH_COMPAT st_stm32_xspi_nor
#else
#define HAS_FLASH_COMPAT 0
#endif

LOG_MODULE_REGISTER(file_manager, CONFIG_RPR_FILE_MANAGER_LOG_LEVEL);

static struct fs_littlefs lfs_data;

static struct fs_mount_t lfs_mount_str = {
    .type        = FS_LITTLEFS,
    .mnt_point   = CONFIG_RPR_FS_MNT_POINT,
    .fs_data     = &lfs_data,
    .storage_dev = (void *)FIXED_PARTITION_ID(storage_partition),
};

/**
 * @brief Initialize and mount the LittleFS filesystem at the specified mount point.
 *
 * This function is called at system startup and:
 * - Mounts the LittleFS filesystem using the `work_partition`
 * - Ensures the default audio directory exists (CONFIG_RPR_AUDIO_DEFAULT_PATH)
 *
 * @retval 0 on success
 * @retval Negative error code on failure (e.g., mount or mkdir failure)
 */
static int fs_init(void)
{
#if HAS_FLASH_COMPAT
    const struct device *flash_dev = DEVICE_DT_GET_ONE(SPI_FLASH_COMPAT);

    if (!device_is_ready(flash_dev)) {
        LOG_ERR("External SPI flash device '%s' is not ready.",
                flash_dev->name);
        return -ENODEV;
    }

    LOG_DBG("External SPI flash device '%s' is ready.", flash_dev->name);
#else
    LOG_WRN("No compatible external SPI flash device defined in the devicetree.");
#endif

    int ret = fs_mount(&lfs_mount_str);
    if (ret != 0) {
        LOG_ERR("Failed to mount LittleFS at %s (err %d)",
                lfs_mount_str.mnt_point,
                ret);
    } else {
        LOG_INF("LittleFS mounted at %s", lfs_mount_str.mnt_point);
    }

    struct fs_dirent dirent;
    ret = fs_stat(CONFIG_RPR_AUDIO_DEFAULT_PATH, &dirent);
    if (ret == -ENOENT) {
        ret = fs_mkdir(CONFIG_RPR_AUDIO_DEFAULT_PATH);
        if (ret != 0) {
            LOG_ERR("Failed to create directory: %s (err %d)",
                    CONFIG_RPR_AUDIO_DEFAULT_PATH,
                    ret);
            return ret;
        }
        LOG_INF("Created directory: %s", CONFIG_RPR_AUDIO_DEFAULT_PATH);
    } else if (ret != 0) {
        LOG_ERR("Failed to stat path %s (err %d)",
                CONFIG_RPR_AUDIO_DEFAULT_PATH,
                ret);
        return ret;
    }

    ret = fs_stat(CONFIG_RPR_WIFI_CONFIG_DEFAULT_PATH, &dirent);
    if (ret == -ENOENT) {
        ret = fs_mkdir(CONFIG_RPR_WIFI_CONFIG_DEFAULT_PATH);
        if (ret != 0) {
            LOG_ERR("Failed to create directory: %s (err %d)",
                    CONFIG_RPR_WIFI_CONFIG_DEFAULT_PATH,
                    ret);
            return ret;
        }
        LOG_INF("Created directory: %s", CONFIG_RPR_WIFI_CONFIG_DEFAULT_PATH);
    } else if (ret != 0) {
        LOG_ERR("Failed to stat path %s (err %d)",
                CONFIG_RPR_WIFI_CONFIG_DEFAULT_PATH,
                ret);
        return ret;
    }

    return ret;
}

SYS_INIT(fs_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
