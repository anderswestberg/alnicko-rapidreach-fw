target_sources_ifdef(CONFIG_RPR_MQTT_LOG_CLIENT app PRIVATE
    mqtt_log_client.c
)

zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR} ..)


