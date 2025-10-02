target_sources_ifdef(CONFIG_RPR_MODULE_MQTT app PRIVATE 
    mqtt_module.c
    mqtt_message_parser.c
    mqtt_audio_handler.c
    mqtt_audio_queue.c
)

target_sources_ifdef(CONFIG_RPR_MQTT_CLIENT_WRAPPER app PRIVATE
    mqtt_client_wrapper.c
    mqtt_audio_handler_v2.c
)
