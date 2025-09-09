/**
 * @file device_registry.h
 * @brief GitHub-based device registry for collision-free ID assignment
 */

#ifndef DEVICE_REGISTRY_H
#define DEVICE_REGISTRY_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Device registration result
 */
typedef struct {
    bool success;
    char assigned_id[16];     /* Assigned device ID (may be longer than requested) */
    uint8_t id_length;        /* Actual length of assigned ID */
    char error_message[128];  /* Error message if failed */
} device_registry_result_t;

/**
 * @brief Register device with GitHub registry
 * 
 * @param full_device_id Full hardware device ID in hex
 * @param preferred_length Preferred ID length (minimum 4)
 * @param github_token GitHub API token
 * @param repo_owner GitHub repository owner
 * @param repo_name GitHub repository name
 * @param result Output registration result
 * @return 0 on success, negative error code on failure
 */
int device_registry_register(
    const char *full_device_id,
    uint8_t preferred_length,
    const char *github_token,
    const char *repo_owner,
    const char *repo_name,
    device_registry_result_t *result
);

/**
 * @brief Check if a device ID prefix is already registered
 * 
 * @param id_prefix Device ID prefix to check
 * @param github_token GitHub API token
 * @param repo_owner GitHub repository owner
 * @param repo_name GitHub repository name
 * @return true if prefix exists, false if available
 */
bool device_registry_prefix_exists(
    const char *id_prefix,
    const char *github_token,
    const char *repo_owner,
    const char *repo_name
);

/**
 * @brief Initialize the device registry module
 * 
 * Sets up network event monitoring for deferred registration.
 * Should be called during system initialization.
 * 
 * @return 0 on success, negative error code on failure
 */
int device_registry_init(void);

/**
 * @brief Get the current device registry status
 * 
 * @param result Output structure to receive the registration result
 * @return true if registration is complete, false otherwise
 */
bool device_registry_get_status(device_registry_result_t *result);

#endif /* DEVICE_REGISTRY_H */
