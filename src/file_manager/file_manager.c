/**
 * @file file_manager.c
 * @brief Simple file manager wrapper implementation
 */

#include "file_manager.h"
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <string.h>

LOG_MODULE_REGISTER(file_manager, CONFIG_RPR_FILE_MANAGER_LOG_LEVEL);

/* File system mount configuration */
FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(storage);
static struct fs_mount_t lfs_storage_mnt = {
    .type = FS_LITTLEFS,
    .fs_data = &storage,
    .storage_dev = (void *)FIXED_PARTITION_ID(storage_partition),
    .mnt_point = CONFIG_RPR_FS_MNT_POINT,
};

static bool fs_mounted = false;

int file_manager_init(void)
{
    int ret;
    struct fs_statvfs stats;
    
    if (fs_mounted) {
        return 0;
    }
    
    LOG_INF("Mounting LittleFS at %s", lfs_storage_mnt.mnt_point);
    
    ret = fs_mount(&lfs_storage_mnt);
    if (ret < 0) {
        LOG_ERR("Failed to mount LittleFS: %d", ret);
        return ret;
    }
    
    /* Get file system statistics */
    ret = fs_statvfs(lfs_storage_mnt.mnt_point, &stats);
    if (ret < 0) {
        LOG_ERR("Failed to get file system stats: %d", ret);
        fs_unmount(&lfs_storage_mnt);
        return ret;
    }
    
    LOG_INF("LittleFS mounted successfully:");
    LOG_INF("  Block size: %lu", stats.f_bsize);
    LOG_INF("  Total blocks: %lu", stats.f_blocks);
    LOG_INF("  Free blocks: %lu", stats.f_bfree);
    
    fs_mounted = true;
    return 0;
}

/* Ensure file system is mounted before operations */
static int ensure_fs_mounted(void)
{
    if (!fs_mounted) {
        /* Try to mount, but if already mounted, just set flag */
        int ret = file_manager_init();
        if (ret == -EBUSY) {
            /* Already mounted, just set flag */
            fs_mounted = true;
            return 0;
        }
        return ret;
    }
    return 0;
}

int file_manager_write(const char *filepath, const void *data, size_t len)
{
    struct fs_file_t file;
    int ret;

    if (!filepath || !data || len == 0) {
        return -EINVAL;
    }
    
    /* Ensure file system is mounted */
    ret = ensure_fs_mounted();
    if (ret < 0) {
        return ret;
    }

    fs_file_t_init(&file);

    ret = fs_open(&file, filepath, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
    if (ret < 0) {
        LOG_ERR("Failed to open file %s: %d", filepath, ret);
        return ret;
    }

    ret = fs_write(&file, data, len);
    if (ret < 0) {
        LOG_ERR("Failed to write to file %s: %d", filepath, ret);
        fs_close(&file);
        return ret;
    }

    if (ret != len) {
        LOG_WRN("Partial write to %s: wrote %d of %zu bytes", filepath, ret, len);
    }

    ret = fs_close(&file);
    if (ret < 0) {
        LOG_ERR("Failed to close file %s: %d", filepath, ret);
        return ret;
    }

    LOG_DBG("Successfully wrote %zu bytes to %s", len, filepath);
    return 0;
}

int file_manager_delete(const char *filepath)
{
    int ret;

    if (!filepath) {
        return -EINVAL;
    }
    
    /* Ensure file system is mounted */
    ret = ensure_fs_mounted();
    if (ret < 0) {
        return ret;
    }

    ret = fs_unlink(filepath);
    if (ret < 0 && ret != -ENOENT) {
        LOG_ERR("Failed to delete file %s: %d", filepath, ret);
        return ret;
    }

    LOG_DBG("Deleted file %s", filepath);
    return 0;
}

int file_manager_exists(const char *filepath)
{
    struct fs_dirent entry;
    int ret;

    if (!filepath) {
        return -EINVAL;
    }
    
    /* Ensure file system is mounted */
    ret = ensure_fs_mounted();
    if (ret < 0) {
        return ret;
    }

    ret = fs_stat(filepath, &entry);
    if (ret == 0) {
        return 1;  /* File exists */
    } else if (ret == -ENOENT) {
        return 0;  /* File does not exist */
    } else {
        LOG_ERR("Failed to stat file %s: %d", filepath, ret);
        return ret;
    }
}