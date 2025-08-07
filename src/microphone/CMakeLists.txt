if(DEFINED CONFIG_RPR_MODULE_MICROPHONE)    
    target_sources(app PRIVATE microphone.c)
endif()

target_include_directories(app PRIVATE .)

