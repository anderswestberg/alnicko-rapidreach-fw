/**
 * @file log_settings.h
 * @brief Persistent log level settings management
 */

#ifndef LOG_SETTINGS_H
#define LOG_SETTINGS_H

#include <stdint.h>

/**
 * @brief Initialize log settings module and restore saved settings
 * @return 0 on success, negative error code on failure
 */
int log_settings_init(void);

/**
 * @brief Save current log levels to persistent storage
 * @return 0 on success, negative error code on failure
 */
int log_settings_save(void);

/**
 * @brief Load and apply log levels from persistent storage
 * @return 0 on success, negative error code on failure
 */
int log_settings_load(void);

/**
 * @brief Clear all saved log settings
 * @return 0 on success, negative error code on failure
 */
int log_settings_clear(void);

#endif /* LOG_SETTINGS_H */
