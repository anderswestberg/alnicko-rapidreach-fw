# HTTP Log Client Module

zephyr_library_named(http_log_client)

zephyr_library_sources(
    http_log_client.c
)

zephyr_library_sources_ifdef(CONFIG_SHELL
    http_log_shell.c
)

zephyr_library_include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Add module to the include path
zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR})
