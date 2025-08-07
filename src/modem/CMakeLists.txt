if(DEFINED CONFIG_RPR_MODEM)   
    target_sources(app PRIVATE modem_module.c )
endif()

target_include_directories(app PRIVATE .)




