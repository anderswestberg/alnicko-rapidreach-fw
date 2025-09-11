# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

list(APPEND ZEPHYR_EXTRA_MODULES
  ${CMAKE_CURRENT_SOURCE_DIR}/drivers
)

# Set the board root for custom boards
set(BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(rapidreach)

add_subdirectory(src)

target_sources(app PRIVATE src/main/main.c)
target_sources(app PRIVATE src/main/shell_keepalive.c)

# Link HTTP log client if enabled
if(CONFIG_HTTP_LOG_CLIENT)
    target_link_libraries(app PUBLIC http_log_client)
endif()

