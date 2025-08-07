if(DEFINED CONFIG_RPR_MODULE_WATCHDOG)
    target_sources(app PRIVATE watchdog.c )
endif()

target_include_directories(app PRIVATE .)

