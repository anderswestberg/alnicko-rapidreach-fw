/**
 * @file cli_utils.h
 * @brief Utility functions for CLI argument parsing with proper error handling
 * 
 * @author Assistant
 * @copyright (C) 2025 Alnicko Lab OU. All rights reserved.
 */

#ifndef _CLI_UTILS_H_
#define _CLI_UTILS_H_

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/**
 * @brief Parse a string to a signed integer with error handling
 * 
 * @param str Input string to parse
 * @param value Pointer to store the parsed value (only modified on success)
 * @param min Minimum allowed value (inclusive)
 * @param max Maximum allowed value (inclusive)
 * @return true on successful parse within range, false otherwise
 */
bool cli_parse_int(const char *str, int *value, int min, int max);

/**
 * @brief Parse a string to an unsigned integer with error handling
 * 
 * @param str Input string to parse
 * @param value Pointer to store the parsed value (only modified on success)
 * @param min Minimum allowed value (inclusive)
 * @param max Maximum allowed value (inclusive)
 * @return true on successful parse within range, false otherwise
 */
bool cli_parse_uint(const char *str, unsigned int *value, unsigned int min, unsigned int max);

/**
 * @brief Parse a string to a long integer with error handling
 * 
 * @param str Input string to parse
 * @param value Pointer to store the parsed value (only modified on success)
 * @param min Minimum allowed value (inclusive)
 * @param max Maximum allowed value (inclusive)
 * @return true on successful parse within range, false otherwise
 */
bool cli_parse_long(const char *str, long *value, long min, long max);

/**
 * @brief Get the current working directory
 * 
 * @return Pointer to current working directory string (statically allocated)
 */
const char *cli_get_cwd(void);

/**
 * @brief Set the current working directory
 * 
 * @param path Path to set as current directory
 * @return true on success, false on failure
 */
bool cli_set_cwd(const char *path);

/**
 * @brief Resolve a path relative to current working directory
 * 
 * Converts relative or absolute paths to absolute paths:
 * - If path starts with "/", use as-is (absolute)
 * - Otherwise, resolve relative to current working directory
 * 
 * @param path Input path (can be relative or absolute)
 * @param out_path Buffer to store resolved path
 * @param max_len Maximum length of out_path buffer
 * @return true on success, false on failure
 */
bool cli_resolve_path(const char *path, char *out_path, size_t max_len);

#endif /* _CLI_UTILS_H_ */
