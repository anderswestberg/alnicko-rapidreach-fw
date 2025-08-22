/**
 * @file file_manager.c
 * @brief Simple file manager wrapper implementation
 */

#include "file_manager.h"
#include <zephyr/fs/fs.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(file_manager, LOG_LEVEL_INF);

int file_manager_write(const char *filepath, const void *data, size_t len)
{
    struct fs_file_t file;
    int ret;

    if (!filepath || !data || len == 0) {
        return -EINVAL;
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