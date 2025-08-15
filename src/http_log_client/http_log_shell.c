/**
 * @file http_log_shell.c
 * @brief Shell commands for HTTP log client
 */

#include <zephyr/shell/shell.h>
#include <zephyr/kernel.h>
#include <stdlib.h>
#include "http_log_client.h"

static int cmd_http_log_status(const struct shell *sh, size_t argc, char **argv)
{
    uint32_t sent, dropped, failed;
    uint16_t buffered;
    
    http_log_get_stats(&sent, &dropped, &failed);
    buffered = http_log_get_buffer_count();
    
    shell_print(sh, "HTTP Log Client Status:");
    shell_print(sh, "  Buffered: %d", buffered);
    shell_print(sh, "  Sent:     %d", sent);
    shell_print(sh, "  Dropped:  %d", dropped);
    shell_print(sh, "  Failed:   %d", failed);
    
    return 0;
}

static int cmd_http_log_flush(const struct shell *sh, size_t argc, char **argv)
{
    shell_print(sh, "Flushing log buffer...");
    int count = http_log_flush();
    
    if (count >= 0) {
        shell_print(sh, "Flushed %d logs", count);
    } else {
        shell_error(sh, "Flush failed: %d", count);
    }
    
    return 0;
}

static int cmd_http_log_enable(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2) {
        shell_error(sh, "Usage: http_log enable <on|off>");
        return -EINVAL;
    }
    
    bool enable = strcmp(argv[1], "on") == 0;
    http_log_enable(enable);
    shell_print(sh, "HTTP logging %s", enable ? "enabled" : "disabled");
    
    return 0;
}

static int cmd_http_log_test(const struct shell *sh, size_t argc, char **argv)
{
    const char *message = "Test log message from shell";
    
    if (argc > 1) {
        /* Join all arguments as the message */
        message = argv[1];
    }
    
    http_log_add(LOG_LEVEL_INF, "shell", "%s", message);
    shell_print(sh, "Added test log: %s", message);
    
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(http_log_cmds,
    SHELL_CMD(status, NULL, "Show HTTP log client status", cmd_http_log_status),
    SHELL_CMD(flush, NULL, "Force flush log buffer", cmd_http_log_flush),
    SHELL_CMD(enable, NULL, "Enable/disable HTTP logging", cmd_http_log_enable),
    SHELL_CMD(test, NULL, "Send a test log message", cmd_http_log_test),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(http_log, &http_log_cmds, "HTTP log client commands", NULL);
