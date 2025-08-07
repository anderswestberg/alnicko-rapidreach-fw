if(DEFINED CONFIG_RPR_MODULE_LED)
    target_sources(app PRIVATE led_control.c )
endif()

target_include_directories(app PRIVATE .)

