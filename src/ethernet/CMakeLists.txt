if(DEFINED CONFIG_RPR_ETHERNET)   
    target_sources(app PRIVATE ethernet.c )
endif()

target_include_directories(app PRIVATE .)



