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

#endif /* _CLI_UTILS_H_ */
