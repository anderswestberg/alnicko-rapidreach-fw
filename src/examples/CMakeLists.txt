if(DEFINED CONFIG_EXAMPLES_ENABLE_ALNICKO_SHELL)
    target_sources(app PRIVATE alnicko_server.c )
    target_sources(app PRIVATE alnicko_shell.c )
endif()

if(DEFINED CONFIG_EXAMPLES_ENABLE_MAIN_EXAMPLES)
    target_sources(app PRIVATE power_supervisor.c )
    target_sources(app PRIVATE domain_logic.c )
    target_sources(app PRIVATE alnicko_server.c )
endif()

target_sources_ifdef(CONFIG_RPR_MQTT_CLIENT_WRAPPER app PRIVATE
    test_mqtt_wrapper.c
)

target_include_directories(app PRIVATE .)
