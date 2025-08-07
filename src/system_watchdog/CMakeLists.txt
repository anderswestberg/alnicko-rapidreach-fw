if(DEFINED CONFIG_RPR_MODULE_SYSTEM_WATCHDOG)
    target_sources(app PRIVATE system_watchdog.c )
endif()

target_include_directories(app PRIVATE .)

