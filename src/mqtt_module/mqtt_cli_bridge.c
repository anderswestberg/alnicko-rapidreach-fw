/**
 * @file mqtt_cli_bridge.c
 * @brief MQTT CLI Bridge - Remote command execution via MQTT
 * 
 * This module provides a bridge between MQTT and the Zephyr shell system,
 * allowing remote command execution and response retrieval via MQTT topics.
 * 
 * @author RapidReach Development Team
 * @copyright (C) 2025 RapidReach. All rights reserved.
 */

#include <zephyr/kernel.h>
#include <zephyr/version.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_uart.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/mqtt.h>
#include <string.h>
#include <stdio.h>
#include "mqtt_module.h"

LOG_MODULE_REGISTER(mqtt_cli_bridge, LOG_LEVEL_INF);

/* Configuration */
#define MQTT_CLI_CMD_TOPIC_PREFIX "rapidreach/"
#define MQTT_CLI_CMD_TOPIC_SUFFIX "/cli/command"
#define MQTT_CLI_RSP_TOPIC_SUFFIX "/cli/response"
#define MQTT_CLI_MAX_CMD_LEN 256
#define MQTT_CLI_MAX_RSP_LEN 1024

/* State management */
static bool mqtt_cli_enabled = false;
static char device_id[32] = {0};
static char cmd_topic[128] = {0};
static char rsp_topic[128] = {0};

/* Command and response buffers */
static char cmd_buffer[MQTT_CLI_MAX_CMD_LEN];
static char rsp_buffer[MQTT_CLI_MAX_RSP_LEN];

/* Work queue for command execution */
static struct k_work_q mqtt_cli_work_q;
static K_THREAD_STACK_DEFINE(mqtt_cli_work_q_stack, 2048);

/* Work item for command execution */
static struct k_work cmd_work;

/**
 * @brief Custom shell transport backend for capturing output
 */
struct mqtt_shell_transport {
    struct shell_transport transport;
    char *buffer;
    size_t buffer_size;
    size_t buffer_pos;
};

static struct mqtt_shell_transport mqtt_transport;

/**
 * @brief Shell transport write function for capturing output
 */
static int mqtt_shell_write(const struct shell_transport *transport,
                           const void *data, size_t length, size_t *cnt)
{
    struct mqtt_shell_transport *mqtt_trans = CONTAINER_OF(transport, 
                                                          struct mqtt_shell_transport, 
                                                          transport);
    
    size_t space_left = mqtt_trans->buffer_size - mqtt_trans->buffer_pos;
    size_t to_copy = MIN(length, space_left);
    
    if (to_copy > 0) {
        memcpy(&mqtt_trans->buffer[mqtt_trans->buffer_pos], data, to_copy);
        mqtt_trans->buffer_pos += to_copy;
    }
    
    *cnt = to_copy;
    return 0;
}

/**
 * @brief Shell transport read function (not used for output capture)
 */
static int mqtt_shell_read(const struct shell_transport *transport,
                          void *data, size_t length, size_t *cnt)
{
    ARG_UNUSED(transport);
    ARG_UNUSED(data);
    ARG_UNUSED(length);
    *cnt = 0;
    return 0;
}

/**
 * @brief Shell transport API
 */
static const struct shell_transport_api mqtt_shell_transport_api = {
    .write = mqtt_shell_write,
    .read = mqtt_shell_read,
};

/**
 * @brief Custom shell fprintf context for capturing output
 */
static void mqtt_shell_print(const struct shell *sh, const char *fmt, ...)
{
    va_list args;
    char temp[256];
    
    va_start(args, fmt);
    int len = vsnprintf(temp, sizeof(temp), fmt, args);
    va_end(args);
    
    if (len > 0) {
        size_t space_left = MQTT_CLI_MAX_RSP_LEN - mqtt_transport.buffer_pos - 1;
        size_t to_copy = MIN(len, space_left);
        
        if (to_copy > 0) {
            memcpy(&rsp_buffer[mqtt_transport.buffer_pos], temp, to_copy);
            mqtt_transport.buffer_pos += to_copy;
            rsp_buffer[mqtt_transport.buffer_pos] = '\0';
        }
    }
}

/* Macro replacements for shell output functions - only for execute_known_command */
#define SHELL_PRINT_CAPTURE(sh, ...) mqtt_shell_print(sh, __VA_ARGS__)
#define SHELL_INFO_CAPTURE(sh, ...) mqtt_shell_print(sh, __VA_ARGS__)
#define SHELL_WARN_CAPTURE(sh, ...) mqtt_shell_print(sh, __VA_ARGS__)
#define SHELL_ERROR_CAPTURE(sh, ...) mqtt_shell_print(sh, __VA_ARGS__)

/**
 * @brief Execute specific known commands with output capture
 */
static int execute_known_command(const struct shell *sh, const char *cmd)
{
    /* Parse command */
    if (strcmp(cmd, "kernel uptime") == 0) {
        int64_t uptime = k_uptime_get();
        SHELL_PRINT_CAPTURE(sh, "Uptime: %lld ms (%lld seconds)", uptime, uptime/1000);
        return 0;
    }
    else if (strcmp(cmd, "kernel version") == 0) {
        SHELL_PRINT_CAPTURE(sh, "Zephyr version %d.%d.%d", 
                    KERNEL_VERSION_MAJOR, KERNEL_VERSION_MINOR, KERNEL_PATCHLEVEL);
        return 0;
    }
    else if (strcmp(cmd, "rapidreach test") == 0) {
        SHELL_PRINT_CAPTURE(sh, "Hello");
        return 0;
    }
    else if (strcmp(cmd, "rapidreach mqtt status") == 0) {
        bool connected = mqtt_is_connected();
        if (connected) {
            SHELL_PRINT_CAPTURE(sh, "MQTT Connected");
            SHELL_PRINT_CAPTURE(sh, "Broker: %s:%d", CONFIG_RPR_MQTT_BROKER_HOST, CONFIG_RPR_MQTT_BROKER_PORT);
            SHELL_PRINT_CAPTURE(sh, "Client ID: %s", CONFIG_RPR_MQTT_CLIENT_ID);
        } else {
            SHELL_PRINT_CAPTURE(sh, "MQTT Disconnected");
        }
        return 0;
    }
    else if (strncmp(cmd, "rapidreach mqtt heartbeat", 25) == 0) {
        if (strstr(cmd, "start")) {
            mqtt_start_heartbeat();
            SHELL_PRINT_CAPTURE(sh, "Heartbeat started");
            return 0;
        } else if (strstr(cmd, "stop")) {
            mqtt_stop_heartbeat();
            SHELL_PRINT_CAPTURE(sh, "Heartbeat stopped");
            return 0;
        } else if (strstr(cmd, "status")) {
            /* For now, just show that heartbeat functions exist */
            SHELL_PRINT_CAPTURE(sh, "Heartbeat functions available");
            SHELL_PRINT_CAPTURE(sh, "Interval: %d seconds", CONFIG_RPR_MQTT_HEARTBEAT_INTERVAL_SEC);
            return 0;
        }
    }
    
    return -ENOENT;
}

/**
 * @brief Execute command and send response
 */
static void execute_command_work(struct k_work *work)
{
    ARG_UNUSED(work);
    
    LOG_INF("Executing CLI command: %s", cmd_buffer);
    
    /* Reset output buffer */
    mqtt_transport.buffer_pos = 0;
    memset(rsp_buffer, 0, MQTT_CLI_MAX_RSP_LEN);
    
    /* Special handling for help command */
    if (strcmp(cmd_buffer, "help") == 0) {
        /* Build help output */
        strcpy(rsp_buffer, "Available commands:\n");
        strcat(rsp_buffer, "  alnicko      : Alnicko server command\n");
        strcat(rsp_buffer, "  clear        : Clear screen.\n");
        strcat(rsp_buffer, "  date         : Date commands\n");
        strcat(rsp_buffer, "  device       : Device commands\n");
        strcat(rsp_buffer, "  devmem       : Read/write physical memory\n");
        strcat(rsp_buffer, "  fs           : File system commands\n");
        strcat(rsp_buffer, "  help         : Prints the help message.\n");
        strcat(rsp_buffer, "  history      : Command history.\n");
        strcat(rsp_buffer, "  kernel       : Kernel commands\n");
        strcat(rsp_buffer, "  mcuboot      : MCUboot commands\n");
        strcat(rsp_buffer, "  modem        : Modem commands\n");
        strcat(rsp_buffer, "  modem_stats  : Modem statistics commands\n");
        strcat(rsp_buffer, "  net          : Networking commands\n");
        strcat(rsp_buffer, "  pm           : PM commands\n");
        strcat(rsp_buffer, "  rapidreach   : RapidReach commands\n");
        strcat(rsp_buffer, "  rem          : Ignore lines beginning with 'rem '\n");
        strcat(rsp_buffer, "  resize       : Console gets terminal screen size\n");
        strcat(rsp_buffer, "  retval       : Print return value of most recent command\n");
        strcat(rsp_buffer, "  shell        : Useful, not Unix-like shell commands.\n");
        strcat(rsp_buffer, "  wifi         : Wi-Fi commands\n");
        
        mqtt_module_publish(rsp_topic, rsp_buffer, strlen(rsp_buffer));
        return;
    }
    
    /* Get shell instance for context */
    const struct shell *sh = shell_backend_uart_get_ptr();
    if (!sh) {
        LOG_ERR("Failed to get shell instance");
        strcpy(rsp_buffer, "Error: Shell not available");
        mqtt_module_publish(rsp_topic, rsp_buffer, strlen(rsp_buffer));
        return;
    }
    
    /* Try to execute known commands with output capture */
    int ret = execute_known_command(sh, cmd_buffer);
    
    /* If not a known command, fall back to shell_execute_cmd */
    if (ret == -ENOENT) {
        /* For now, we can't capture output from shell_execute_cmd */
        /* So we'll just report success/failure */
        ret = shell_execute_cmd(sh, cmd_buffer);
        
        if (ret == 0) {
            /* For commands we can't capture output, provide informative message */
            snprintf(rsp_buffer, sizeof(rsp_buffer), 
                    "Command '%s' executed successfully. Check serial console for output.", 
                    cmd_buffer);
        }
    }
    
    /* Prepare response */
    if (ret == 0) {
        /* Command succeeded - if no output, send success message */
        if (mqtt_transport.buffer_pos == 0 && strlen(rsp_buffer) == 0) {
            strcpy(rsp_buffer, "OK");
        } else {
            /* Output already in rsp_buffer */
        }
    } else {
        /* Command failed */
        if (mqtt_transport.buffer_pos > 0) {
            /* We have some output */
        } else if (strlen(rsp_buffer) == 0) {
            snprintf(rsp_buffer, sizeof(rsp_buffer), "Error: Command failed (%d)", ret);
        }
    }
    
    /* Publish response */
    mqtt_module_publish(rsp_topic, rsp_buffer, strlen(rsp_buffer));
}



/**
 * @brief Process incoming MQTT command
 * 
 * This function would be called from mqtt_module.c when a message
 * is received on the command topic.
 */
void mqtt_cli_process_command(const char *topic, const uint8_t *payload, size_t len)
{
    if (!mqtt_cli_enabled) {
        LOG_WRN("MQTT CLI bridge not enabled");
        return;
    }
    
    /* Copy command to buffer */
    size_t cmd_len = MIN(len, MQTT_CLI_MAX_CMD_LEN - 1);
    memcpy(cmd_buffer, payload, cmd_len);
    cmd_buffer[cmd_len] = '\0';
    
    LOG_INF("Received CLI command via MQTT: %s", cmd_buffer);
    
    /* Submit work to execute command */
    k_work_submit_to_queue(&mqtt_cli_work_q, &cmd_work);
}

/**
 * @brief Initialize MQTT CLI bridge
 */
int mqtt_cli_bridge_init(const char *dev_id)
{
    if (mqtt_cli_enabled) {
        LOG_WRN("MQTT CLI bridge already initialized");
        return -EALREADY;
    }
    
    /* Store device ID */
    strncpy(device_id, dev_id, sizeof(device_id) - 1);
    
    /* Build topic names */
    snprintf(cmd_topic, sizeof(cmd_topic), "%s%s%s", 
             MQTT_CLI_CMD_TOPIC_PREFIX, device_id, MQTT_CLI_CMD_TOPIC_SUFFIX);
    snprintf(rsp_topic, sizeof(rsp_topic), "%s%s%s",
             MQTT_CLI_CMD_TOPIC_PREFIX, device_id, MQTT_CLI_RSP_TOPIC_SUFFIX);
    
    /* Initialize transport */
    mqtt_transport.transport.api = &mqtt_shell_transport_api;
    mqtt_transport.buffer = rsp_buffer;
    mqtt_transport.buffer_size = MQTT_CLI_MAX_RSP_LEN;
    mqtt_transport.buffer_pos = 0;
    
    /* Initialize work queue */
    k_work_queue_init(&mqtt_cli_work_q);
    k_work_queue_start(&mqtt_cli_work_q,
                       mqtt_cli_work_q_stack,
                       K_THREAD_STACK_SIZEOF(mqtt_cli_work_q_stack),
                       K_PRIO_PREEMPT(10),
                       NULL);
    k_thread_name_set(&mqtt_cli_work_q.thread, "mqtt_cli");
    
    /* Initialize work item */
    k_work_init(&cmd_work, execute_command_work);
    
    /* Note: Don't set mqtt_cli_enabled here, let mqtt_cli_bridge_enable do it */
    
    LOG_INF("MQTT CLI bridge initialized");
    LOG_INF("Command topic: %s", cmd_topic);
    LOG_INF("Response topic: %s", rsp_topic);
    
    return 0;
}

/**
 * @brief Enable/disable MQTT CLI bridge
 */
int mqtt_cli_bridge_enable(bool enable)
{
    if (enable && !mqtt_cli_enabled) {
        /* Subscribe to command topic */
        mqtt_status_t status = mqtt_module_subscribe(cmd_topic, 1, mqtt_cli_process_command);
        if (status != MQTT_SUCCESS) {
            LOG_ERR("Failed to subscribe to command topic: %d", status);
            return -EIO;
        }
        mqtt_cli_enabled = true;
        LOG_INF("MQTT CLI bridge enabled and subscribed to %s", cmd_topic);
    } else if (!enable && mqtt_cli_enabled) {
        /* Unsubscribe from command topic */
        mqtt_module_unsubscribe(cmd_topic);
        mqtt_cli_enabled = false;
        LOG_INF("MQTT CLI bridge disabled");
    }
    
    return 0;
}

/**
 * @brief Get MQTT CLI bridge status
 */
bool mqtt_cli_bridge_is_enabled(void)
{
    return mqtt_cli_enabled;
}

/**
 * @brief Get command topic for subscription
 */
const char *mqtt_cli_get_command_topic(void)
{
    return cmd_topic;
}

/**
 * @brief Shell command to control MQTT CLI bridge
 */
int cmd_mqtt_cli_bridge(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2) {
        shell_print(sh, "Usage: mqtt cli <enable|disable|status>");
        return -EINVAL;
    }
    
    if (strcmp(argv[1], "enable") == 0) {
        if (!mqtt_cli_enabled) {
            /* Initialize if not already done */
            int ret = mqtt_cli_bridge_init(CONFIG_RPR_MQTT_CLIENT_ID);
            if (ret < 0) {
                shell_error(sh, "Failed to initialize MQTT CLI bridge: %d", ret);
                return ret;
            }
        }
        mqtt_cli_bridge_enable(true);
        shell_print(sh, "MQTT CLI bridge enabled");
        shell_print(sh, "Command topic: %s", cmd_topic);
        shell_print(sh, "Response topic: %s", rsp_topic);
    } else if (strcmp(argv[1], "disable") == 0) {
        mqtt_cli_bridge_enable(false);
        shell_print(sh, "MQTT CLI bridge disabled");
    } else if (strcmp(argv[1], "status") == 0) {
        shell_print(sh, "MQTT CLI bridge: %s", mqtt_cli_enabled ? "enabled" : "disabled");
        if (mqtt_cli_enabled) {
            shell_print(sh, "Command topic: %s", cmd_topic);
            shell_print(sh, "Response topic: %s", rsp_topic);
        }
    } else {
        shell_error(sh, "Unknown command: %s", argv[1]);
        return -EINVAL;
    }
    
    return 0;
}

