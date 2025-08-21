target_sources_ifdef(CONFIG_RPR_MODULE_MQTT app PRIVATE 
    mqtt_module.c
    mqtt_message_parser.c
    mqtt_audio_handler.c
)
