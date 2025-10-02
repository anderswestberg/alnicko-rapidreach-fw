if(DEFINED CONFIG_RPR_MODULE_CLI_SHELL)
    target_sources(app PRIVATE cli_module.c cli_utils.c)
endif()


