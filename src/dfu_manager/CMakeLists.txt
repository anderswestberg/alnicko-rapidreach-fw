if(DEFINED CONFIG_RPR_MODULE_DFU)
    target_sources(app PRIVATE dfu_manager.c )
endif()

target_include_directories(app PRIVATE .)

