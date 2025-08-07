if(DEFINED CONFIG_RPR_MODULE_CHARGER)   
    target_sources(app PRIVATE charger.c )
endif()

target_include_directories(app PRIVATE .)

