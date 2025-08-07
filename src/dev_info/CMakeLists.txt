if(DEFINED CONFIG_RPR_MODULE_DEV_INFO)
    target_sources(app PRIVATE dev_info.c )
endif()

target_include_directories(app PRIVATE .)

