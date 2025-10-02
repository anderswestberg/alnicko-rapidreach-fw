/**
 * @file cli_utils.c
 * @brief Implementation of CLI utility functions
 * 
 * @author Assistant
 * @copyright (C) 2025 Alnicko Lab OU. All rights reserved.
 */

#include "cli_utils.h"
#include <stdlib.h>
#include <errno.h>
#include <limits.h>

bool cli_parse_int(const char *str, int *value, int min, int max)
{
    if (!str || !value) {
        return false;
    }
    
    char *endptr;
    errno = 0;
    
    long parsed = strtol(str, &endptr, 10);
    
    /* Check for conversion errors */
    if (errno != 0 || endptr == str || *endptr != '\0') {
        return false;
    }
    
    /* Check if value fits in int and is within specified range */
    if (parsed < INT_MIN || parsed > INT_MAX || parsed < min || parsed > max) {
        return false;
    }
    
    *value = (int)parsed;
    return true;
}

bool cli_parse_uint(const char *str, unsigned int *value, unsigned int min, unsigned int max)
{
    if (!str || !value) {
        return false;
    }
    
    /* Check for negative sign */
    if (*str == '-') {
        return false;
    }
    
    char *endptr;
    errno = 0;
    
    unsigned long parsed = strtoul(str, &endptr, 10);
    
    /* Check for conversion errors */
    if (errno != 0 || endptr == str || *endptr != '\0') {
        return false;
    }
    
    /* Check if value fits in unsigned int and is within specified range */
    if (parsed > UINT_MAX || parsed < min || parsed > max) {
        return false;
    }
    
    *value = (unsigned int)parsed;
    return true;
}

bool cli_parse_long(const char *str, long *value, long min, long max)
{
    if (!str || !value) {
        return false;
    }
    
    char *endptr;
    errno = 0;
    
    long parsed = strtol(str, &endptr, 10);
    
    /* Check for conversion errors */
    if (errno != 0 || endptr == str || *endptr != '\0') {
        return false;
    }
    
    /* Check if value is within specified range */
    if (parsed < min || parsed > max) {
        return false;
    }
    
    *value = parsed;
    return true;
}
