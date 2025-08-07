if(DEFINED CONFIG_RPR_MODULE_SWITCH)
    target_sources(app PRIVATE switch_module.c )
endif()

target_include_directories(app PRIVATE .)

