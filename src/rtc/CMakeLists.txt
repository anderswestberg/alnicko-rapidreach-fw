if(DEFINED CONFIG_RPR_MODULE_RTC)    
    target_sources(app PRIVATE rtc.c )
endif()

target_include_directories(app PRIVATE .)

