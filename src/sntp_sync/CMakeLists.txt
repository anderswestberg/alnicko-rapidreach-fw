zephyr_library()
zephyr_library_sources(sntp_sync.c)
zephyr_library_sources_ifdef(CONFIG_SHELL sntp_sync_shell.c)

