if(DEFINED CONFIG_RPR_MODULE_POWEROFF)    
    target_sources(app PRIVATE poweroff.c )
endif()

target_include_directories(app PRIVATE .)

