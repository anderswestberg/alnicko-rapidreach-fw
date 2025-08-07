if(DEFINED CONFIG_RPR_WIFI)   
    target_sources(app PRIVATE wifi.c )
endif()

target_include_directories(app PRIVATE .)
    
