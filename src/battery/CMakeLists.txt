if(DEFINED CONFIG_RPR_MODULE_BATTERY)
    target_sources(app PRIVATE battery.c )
endif()

target_include_directories(app PRIVATE .)

