if(DEFINED CONFIG_RPR_MODULE_HTTP)
    target_sources(app PRIVATE http_module.c )
endif()

target_include_directories(app PRIVATE .)
target_include_directories(app PRIVATE ./certificates/)

