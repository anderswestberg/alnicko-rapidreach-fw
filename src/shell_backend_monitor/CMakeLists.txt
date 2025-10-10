zephyr_library()

zephyr_library_sources_ifdef(CONFIG_SHELL_BACKEND_MQTT
    shell_mqtt_detector.c
)
