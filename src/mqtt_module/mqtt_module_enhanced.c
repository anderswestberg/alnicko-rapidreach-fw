/**
 * @file mqtt_module_enhanced.c
 * @brief Enhanced MQTT module with GitHub registry integration
 * 
 * This shows how to integrate the device registry with MQTT client ID generation
 */

#include "mqtt_module.h"
#include "../device_registry/device_registry.h"
#include "../dev_info/dev_info.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(mqtt_enhanced, CONFIG_RPR_MODULE_MQTT_LOG_LEVEL);

/**
 * @brief Generate MQTT client ID with collision detection
 */
static int generate_unique_client_id(char *client_id_buffer, size_t buffer_size)
{
    size_t device_id_len;
    const char *full_device_id;
    device_registry_result_t reg_result;
    int ret;

    /* Get full hardware device ID */
    full_device_id = dev_info_get_device_id_str(&device_id_len);
    if (!full_device_id || device_id_len == 0) {
        LOG_ERR("Failed to get device ID");
        return -ENODEV;
    }

    LOG_INF("Full device ID: %s", full_device_id);

#ifdef CONFIG_RPR_MODULE_DEVICE_REGISTRY
    /* Try to register with GitHub registry */
    ret = device_registry_register(
        full_device_id,
        CONFIG_RPR_DEVICE_REGISTRY_PREFERRED_ID_LENGTH,
        CONFIG_RPR_DEVICE_REGISTRY_GITHUB_TOKEN,
        CONFIG_RPR_DEVICE_REGISTRY_REPO_OWNER,
        CONFIG_RPR_DEVICE_REGISTRY_REPO_NAME,
        &reg_result
    );

    if (ret == 0 && reg_result.success) {
        /* Use the assigned ID from registry */
        snprintf(client_id_buffer, buffer_size, "%s-speaker", reg_result.assigned_id);
        LOG_INF("Registered with unique ID: %s (length: %d)", 
                reg_result.assigned_id, reg_result.id_length);
        
        if (reg_result.id_length > CONFIG_RPR_DEVICE_REGISTRY_PREFERRED_ID_LENGTH) {
            LOG_WRN("Had to extend ID length from %d to %d due to collisions",
                    CONFIG_RPR_DEVICE_REGISTRY_PREFERRED_ID_LENGTH,
                    reg_result.id_length);
        }
    } else {
        /* Registry failed, fall back to default behavior */
        LOG_WRN("Device registry failed: %s", reg_result.error_message);
        LOG_WRN("Falling back to default 6-character prefix");
        snprintf(client_id_buffer, buffer_size, "%.6s-speaker", full_device_id);
    }
#else
    /* No registry configured, use default 6-character prefix */
    snprintf(client_id_buffer, buffer_size, "%.6s-speaker", full_device_id);
    LOG_WRN("Device registry not enabled - using potentially non-unique ID");
#endif

    LOG_INF("MQTT client ID: %s", client_id_buffer);
    return 0;
}

/**
 * @brief Example of how to check for ID collisions before connecting
 */
static bool check_id_collision(const char *device_id_prefix)
{
#ifdef CONFIG_RPR_MODULE_DEVICE_REGISTRY
    bool exists = device_registry_prefix_exists(
        device_id_prefix,
        CONFIG_RPR_DEVICE_REGISTRY_GITHUB_TOKEN,
        CONFIG_RPR_DEVICE_REGISTRY_REPO_OWNER,
        CONFIG_RPR_DEVICE_REGISTRY_REPO_NAME
    );

    if (exists) {
        LOG_ERR("Device ID prefix %s is already registered!", device_id_prefix);
        return true;
    }
#endif
    return false;
}
