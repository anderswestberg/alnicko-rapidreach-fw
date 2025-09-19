zephyr_library_sources_ifdef(CONFIG_LOG_SETTINGS
    log_settings.c
)

zephyr_library_sources_ifdef(CONFIG_LOG_SETTINGS_SHELL
    log_settings_shell.c
)
