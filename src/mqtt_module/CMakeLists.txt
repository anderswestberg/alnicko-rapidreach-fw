target_sources_ifdef(CONFIG_RPR_MODULE_MQTT app PRIVATE 
    mqtt_module.c
    mqtt_cli_bridge.c
)
