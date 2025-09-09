/**
 * @file device_registry.c
 * @brief GitHub-based device registry implementation
 * 
 * This module uses GitHub API to maintain a collision-free device registry.
 * Each device gets a unique ID prefix by checking existing registrations.
 */

#include "device_registry.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_if.h>
#include <string.h>
#include <stdio.h>
#include "../http_module/http_module.h"

LOG_MODULE_REGISTER(device_registry, CONFIG_RPR_MODULE_DEVICE_REGISTRY_LOG_LEVEL);

#define GITHUB_API_BASE "https://api.github.com"
#define MAX_HTTP_RESPONSE_SIZE 4096
#define MIN_ID_LENGTH 4
#define MAX_ID_LENGTH 24

/* Global state for deferred registration */
static struct {
    bool pending;
    bool completed;
    char full_device_id[MAX_ID_LENGTH + 1];
    uint8_t preferred_length;
    device_registry_result_t result;
    struct k_work_delayable work;
} registry_state;

/* Network event callback structure */
static struct net_mgmt_event_callback net_mgmt_cb;

/**
 * @brief Check if a file exists in the GitHub repository
 */
static bool check_file_exists(
    const char *github_token,
    const char *repo_owner,
    const char *repo_name,
    const char *file_path)
{
    char url[256];
    char auth_header[128];
    char response[MAX_HTTP_RESPONSE_SIZE];
    const char *headers[3];  /* Auth header, Accept header, NULL terminator */
    
    struct get_context get_ctx = {
        .response_buffer = response,
        .buffer_capacity = sizeof(response),
        .response_len = 0,
        .headers = headers
    };
    uint16_t http_status_code = 0;
    http_status_t ret;

    /* Construct URL */
    snprintf(url, sizeof(url), "%s/repos/%s/%s/contents/%s",
             GITHUB_API_BASE, repo_owner, repo_name, file_path);

    /* Prepare headers */
    snprintf(auth_header, sizeof(auth_header), "Authorization: token %s", github_token);
    headers[0] = auth_header;
    headers[1] = "Accept: application/vnd.github.v3+json";
    headers[2] = NULL;  /* NULL terminator */
    
    LOG_DBG("Checking if file exists: %s", url);
    
    ret = http_get_request(url, &get_ctx, &http_status_code);
    
    if (ret == HTTP_CLIENT_OK) {
        /* 200 = exists, 404 = not found */
        return (http_status_code == 200);
    }
    
    LOG_DBG("HTTP request failed with code %d, status %d", ret, http_status_code);
    return false;
}

/**
 * @brief Create a device registration file in GitHub
 */
static int create_registration_file(
    const char *github_token,
    const char *repo_owner,
    const char *repo_name,
    const char *device_id,
    const char *full_device_id)
{
    char url[256];
    char file_path[64];
    char auth_header[128];
    char response_buf[1024];
    const char *headers[4];
    
    /* Create file path */
    snprintf(file_path, sizeof(file_path), "devices/%s.json", device_id);

    /* Create JSON content for the file */
    char file_content[512];
    snprintf(file_content, sizeof(file_content),
             "{\n"
             "  \"deviceId\": \"%s\",\n"
             "  \"fullHardwareId\": \"%s\",\n"
             "  \"type\": \"speaker\",\n"
             "  \"registeredAt\": \"2025-01-15T10:30:00Z\",\n"
             "  \"firmwareVersion\": \"%s\"\n"
             "}",
             device_id, full_device_id, CONFIG_RPR_FIRMWARE_VERSION);

    /* TODO: Base64 encode file_content for GitHub API */
    /* For now, we'll note this as a limitation */
    
    /* Create the PUT request body */
    char request_body[1024];
    snprintf(request_body, sizeof(request_body),
             "{"
             "\"message\":\"Register device %s\","
             "\"content\":\"BASE64_ENCODED_CONTENT_HERE\","
             "\"branch\":\"main\""
             "}",
             device_id);

    /* Construct URL */
    snprintf(url, sizeof(url), "%s/repos/%s/%s/contents/%s",
             GITHUB_API_BASE, repo_owner, repo_name, file_path);

    /* Prepare headers */
    snprintf(auth_header, sizeof(auth_header), "Authorization: token %s", github_token);
    headers[0] = auth_header;
    headers[1] = "Accept: application/vnd.github.v3+json";
    headers[2] = "Content-Type: application/json";
    headers[3] = NULL;

    struct post_context post_ctx = {
        .response = {
            .response_buffer = response_buf,
            .buffer_capacity = sizeof(response_buf),
            .response_len = 0,
            .headers = NULL  /* Response doesn't need custom headers */
        },
        .payload = request_body,
        .payload_len = strlen(request_body),
        .headers = headers
    };

    uint16_t http_status_code = 0;
    
    LOG_INF("Creating registration file: %s", file_path);
    LOG_DBG("URL: %s", url);

    /* Note: GitHub API uses PUT for creating files, but we're using POST here
     * as the http_module doesn't have PUT support yet. This would need to be
     * enhanced for full functionality. */
    http_status_t ret = http_post_request(url, &post_ctx, &http_status_code);
    
    if (ret != HTTP_CLIENT_OK || http_status_code != 201) {
        LOG_ERR("Failed to create file: HTTP %d, status %d", ret, http_status_code);
        return -EIO;
    }

    LOG_INF("Successfully created device registration file");
    return 0;
}

/**
 * @brief Work handler for deferred device registration
 */
static void registry_work_handler(struct k_work *work)
{
    int ret;
    
    LOG_INF("Attempting deferred device registration...");
    
    ret = device_registry_register(
        registry_state.full_device_id,
        registry_state.preferred_length,
        CONFIG_RPR_DEVICE_REGISTRY_GITHUB_TOKEN,
        CONFIG_RPR_DEVICE_REGISTRY_REPO_OWNER,
        CONFIG_RPR_DEVICE_REGISTRY_REPO_NAME,
        &registry_state.result
    );
    
    if (ret == 0) {
        LOG_INF("Deferred registration successful: %s (length: %d)", 
                registry_state.result.assigned_id, 
                registry_state.result.id_length);
        registry_state.completed = true;
        registry_state.pending = false;
    } else if (ret == -ENOTCONN || ret == -EAGAIN) {
        /* Still no network, retry later */
        LOG_WRN("Network still not ready, will retry in 30 seconds");
        k_work_schedule(&registry_state.work, K_SECONDS(30));
    } else {
        LOG_ERR("Deferred registration failed: %d", ret);
        registry_state.pending = false;
    }
}

/**
 * @brief Network management event handler
 */
static void net_mgmt_event_handler(struct net_mgmt_event_callback *cb,
                                   uint32_t mgmt_event,
                                   struct net_if *iface)
{
    if (mgmt_event == NET_EVENT_L4_CONNECTED) {
        LOG_INF("Network connected - checking device registry status");
        
        if (registry_state.pending && !registry_state.completed) {
            /* Schedule registration attempt after a short delay to ensure
             * network is fully ready */
            k_work_schedule(&registry_state.work, K_SECONDS(2));
        }
    }
}

int device_registry_register(
    const char *full_device_id,
    uint8_t preferred_length,
    const char *github_token,
    const char *repo_owner,
    const char *repo_name,
    device_registry_result_t *result)
{
    char test_prefix[MAX_ID_LENGTH + 1];
    uint8_t current_length;
    bool found_unique = false;
    
    /* Check if we have network connectivity */
    struct net_if *iface = net_if_get_default();
    if (!iface || !net_if_is_up(iface)) {
        LOG_WRN("No network interface available, deferring registration");
        
        /* Store parameters for deferred registration */
        strncpy(registry_state.full_device_id, full_device_id, MAX_ID_LENGTH);
        registry_state.full_device_id[MAX_ID_LENGTH] = '\0';
        registry_state.preferred_length = preferred_length;
        registry_state.pending = true;
        registry_state.completed = false;
        
        /* Return a temporary result */
        strncpy(result->assigned_id, full_device_id, preferred_length);
        result->assigned_id[preferred_length] = '\0';
        result->id_length = preferred_length;
        result->success = false;
        
        return -ENOTCONN;
    }

    LOG_INF("Device registry registration started");
    LOG_DBG("Full device ID: %s, preferred length: %d", full_device_id, preferred_length);
    LOG_DBG("Repository: %s/%s", repo_owner, repo_name);

    if (!full_device_id || !github_token || !repo_owner || !repo_name || !result) {
        LOG_ERR("Invalid parameters passed to device_registry_register");
        return -EINVAL;
    }

    /* Initialize result */
    memset(result, 0, sizeof(*result));
    result->success = false;

    /* Validate preferred length */
    if (preferred_length < MIN_ID_LENGTH) {
        preferred_length = MIN_ID_LENGTH;
    }

    /* Try progressively longer prefixes until we find a unique one */
    for (current_length = preferred_length; current_length <= strlen(full_device_id) && current_length <= MAX_ID_LENGTH; current_length++) {
        /* Extract prefix */
        memset(test_prefix, 0, sizeof(test_prefix));
        strncpy(test_prefix, full_device_id, current_length);

        LOG_DBG("Testing prefix: %s (length %d)", test_prefix, current_length);

        /* Check if this prefix already exists */
        if (!device_registry_prefix_exists(test_prefix, github_token, repo_owner, repo_name)) {
            found_unique = true;
            break;
        }

        LOG_WRN("Prefix %s already exists, trying longer prefix", test_prefix);
    }

    if (!found_unique) {
        snprintf(result->error_message, sizeof(result->error_message),
                 "Could not find unique prefix up to length %d", current_length - 1);
        return -EEXIST;
    }

    /* Register the device with the unique prefix */
    int ret = create_registration_file(github_token, repo_owner, repo_name, test_prefix, full_device_id);
    if (ret != 0) {
        snprintf(result->error_message, sizeof(result->error_message),
                 "Failed to create registration file: %d", ret);
        return ret;
    }

    /* Success */
    result->success = true;
    strncpy(result->assigned_id, test_prefix, sizeof(result->assigned_id) - 1);
    result->id_length = current_length;

    LOG_INF("Device registered with ID: %s (length %d)", result->assigned_id, result->id_length);

    return 0;
}

bool device_registry_prefix_exists(
    const char *id_prefix,
    const char *github_token,
    const char *repo_owner,
    const char *repo_name)
{
    char file_path[64];

    if (!id_prefix || !github_token || !repo_owner || !repo_name) {
        return false;
    }

    /* Check if a file with this prefix exists */
    snprintf(file_path, sizeof(file_path), "devices/%s.json", id_prefix);

    return check_file_exists(github_token, repo_owner, repo_name, file_path);
}

/**
 * @brief Initialize the device registry module
 * 
 * Sets up network event monitoring for deferred registration.
 * Should be called during system initialization.
 */
int device_registry_init(void)
{
    LOG_INF("Initializing device registry module");
    
    /* Initialize the work item */
    k_work_init_delayable(&registry_state.work, registry_work_handler);
    
    /* Register for network events */
    net_mgmt_init_event_callback(&net_mgmt_cb,
                                 net_mgmt_event_handler,
                                 NET_EVENT_L4_CONNECTED);
    net_mgmt_add_event_callback(&net_mgmt_cb);
    
    LOG_DBG("Device registry module initialized");
    return 0;
}

/**
 * @brief Get the current device registry status
 */
bool device_registry_get_status(device_registry_result_t *result)
{
    if (!result) {
        return false;
    }
    
    if (registry_state.completed) {
        memcpy(result, &registry_state.result, sizeof(*result));
        return true;
    }
    
    return false;
}
