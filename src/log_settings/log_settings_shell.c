/**
 * @file log_settings_shell.c
 * @brief Shell commands for persistent log settings
 */

#include <zephyr/shell/shell.h>
#include <zephyr/logging/log.h>
#include "log_settings.h"

LOG_MODULE_REGISTER(log_settings_shell, LOG_LEVEL_INF);

static int cmd_log_save(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    
    int ret = log_settings_save();
    if (ret < 0) {
        shell_error(sh, "Failed to save log settings: %d", ret);
        return ret;
    }
    
    shell_print(sh, "Log settings saved successfully");
    return 0;
}

static int cmd_log_load(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    
    int ret = log_settings_load();
    if (ret < 0) {
        shell_error(sh, "Failed to load log settings: %d", ret);
        return ret;
    }
    
    shell_print(sh, "Log settings loaded successfully");
    return 0;
}

static int cmd_log_clear(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    
    int ret = log_settings_clear();
    if (ret < 0) {
        shell_error(sh, "Failed to clear log settings: %d", ret);
        return ret;
    }
    
    shell_print(sh, "Log settings cleared");
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(log_settings_cmds,
    SHELL_CMD(save, NULL, "Save current log levels to persistent storage", cmd_log_save),
    SHELL_CMD(load, NULL, "Load and apply saved log levels", cmd_log_load),
    SHELL_CMD(clear, NULL, "Clear saved log settings", cmd_log_clear),
    SHELL_SUBCMD_SET_END
);

/* Add as a subcommand to the existing 'log' command */
static int cmd_log_settings(const struct shell *sh, size_t argc, char **argv)
{
    shell_help(sh);
    return 0;
}

SHELL_CMD_REGISTER(log_settings, &log_settings_cmds, "Persistent log settings commands", cmd_log_settings);
