/**
 * @file config_types.h
 * @brief Ogg integer type definitions for Zephyr environment
 *
 * This header defines fixed-size integer types used by the Ogg library,
 * adapted for Zephyr-based projects. The types map directly to standard
 * C99 types via <stdint.h>.
 * 
 * @author Eugene K.
 * @copyright (C) 2025 Alnicko Lab OU. All rights reserved.
 */

 #ifndef __CONFIG_TYPES_H__
 #define __CONFIG_TYPES_H__
 
 #include <stdint.h>
 
 /* Zephyr-compatible typedefs for Ogg library */
 typedef int16_t  ogg_int16_t;
 typedef uint16_t ogg_uint16_t;
 typedef int32_t  ogg_int32_t;
 typedef uint32_t ogg_uint32_t;
 typedef int64_t  ogg_int64_t;
 typedef uint64_t ogg_uint64_t;
 
 #endif /* __CONFIG_TYPES_H__ */
 