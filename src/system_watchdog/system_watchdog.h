/**
 * @file system_watchdog.h
 * @brief Internal IWDG watchdog handler for STM32-based systems.
 *
 * This module provides initialization and feeding logic for the STM32 Independent Watchdog Timer (IWDG).
 * It was introduced to solve a rare but critical issue encountered after moving the DFU slot
 * to external flash memory due to internal flash limitations. In some cases, MCUBoot fails to boot the main
 * application image and hangs the device during startup.
 *
 * Although the system includes an external watchdog, its short timeout (1.6 seconds) is not suitable for
 * long-running operations such as firmware updates. The IWDG timer, however, can be
 * activated earlier during the bootloader phase and maintained throughout the firmware update and runtime.
 *
 * This internal watchdog acts as a second-level safety mechanism. It is enabled in MCUBoot, where it is fed
 * during firmware updates, and continues to operate in the main firmware via `system_watchdog_feed()`.
 *
 * This module works in parallel with the external watchdog and provides additional protection against
 * rare startup hangs.
 *
 * @author Eugene K.
 * @copyright (C) 2025 Alnicko Lab OU. All rights reserved.
 */

#ifndef SYSTEM_WATCHDOG_H
#define SYSTEM_WATCHDOG_H

/**
 * @brief Feed the internal STM32 independent watchdog (IWDG).
 *
 * This function resets the watchdog timer to prevent system reset.
 * Must be called periodically from the application to keep the system alive.
 *
 * @return 0 on success, negative error code on failure.
 */
int system_watchdog_feed(void);

#endif /* SYSTEM_WATCHDOG_H */
