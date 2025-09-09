# Initialization State Machine module

zephyr_library_sources_ifdef(
    CONFIG_RPR_MODULE_INIT_SM
    init_state_machine.c
)

zephyr_library_include_directories_ifdef(
    CONFIG_RPR_MODULE_INIT_SM
    .
)
