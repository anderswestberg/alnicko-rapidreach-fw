/**
 * @file file_manager.h
 * @brief Simple file manager wrapper for Zephyr file system
 */

#ifndef FILE_MANAGER_H
#define FILE_MANAGER_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Initialize the file manager and mount the file system
 * 
 * This function mounts the LittleFS file system at the configured mount point.
 * It is automatically called on first file operation if not already initialized.
 * 
 * @return 0 on success, negative error code on failure
 */
int file_manager_init(void);

/**
 * @brief Write data to a file
 * 
 * @param filepath Path to the file
 * @param data Data to write
 * @param len Length of data
 * @return 0 on success, negative error code on failure
 */
int file_manager_write(const char *filepath, const void *data, size_t len);

/**
 * @brief Delete a file
 * 
 * @param filepath Path to the file to delete
 * @return 0 on success, negative error code on failure
 */
int file_manager_delete(const char *filepath);

/**
 * @brief Check if a file exists
 * 
 * @param filepath Path to the file
 * @return 1 if exists, 0 if not exists, negative on error
 */
int file_manager_exists(const char *filepath);

#endif /* FILE_MANAGER_H */
