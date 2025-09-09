# Device Registry Module CMakeLists.txt

zephyr_library_sources_ifdef(CONFIG_RPR_MODULE_DEVICE_REGISTRY
    device_registry.c
)

zephyr_library_include_directories_ifdef(CONFIG_RPR_MODULE_DEVICE_REGISTRY
    ${CMAKE_CURRENT_SOURCE_DIR}
)
