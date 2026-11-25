# SPDX-License-Identifier: BSD-3-Clause-Clear
# Ping Pong on FreeRTOS (STM32F411 Black Pill)
#
#   cmake -S examples/main_examples/ping_pong_freertos_example -B build_rtos -G Ninja \
#         -DBOARD=BLACKPILL_F411 -DRAC_RADIO=lr2021
#   ninja -C build_rtos            # -> build_rtos/ping_pong_freertos.{elf,bin,hex}
#   ninja -C build_rtos flash_ping_pong_freertos   # OpenOCD + ST-Link

cmake_minimum_required(VERSION 3.25)

set(DESCRIPTION "Ping-pong on FreeRTOS (STM32F411 Black Pill)")

################################################################################
# Toolchain and Project

include(${CMAKE_CURRENT_SOURCE_DIR}/../../toolchain.cmake)
project(ping_pong_freertos LANGUAGES C ASM DESCRIPTION "${DESCRIPTION}")

# When built as part of main_examples ("all examples"), the modem HAL has already been configured
# for bare metal: this example needs its own build directory (USE_FREERTOS changes the modem HAL).
if(COMMON_CMAKE_INCLUDED AND NOT USE_FREERTOS)
    message(STATUS "ping_pong_freertos: build it on its own (cmake -S .../ping_pong_freertos_example), skipped")
    return()
endif()

# The modem HAL RTOS hooks come from rtos_hooks.c
set(USE_FREERTOS ON CACHE BOOL "" FORCE)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../common.cmake)

if(NOT BOARD STREQUAL "BLACKPILL_F411")
    message(STATUS "ping_pong_freertos: only for BOARD=BLACKPILL_F411, skipped")
    return()
endif()

################################################################################
# FreeRTOS kernel (vendored in examples/rtos/FreeRTOS-Kernel, V11.1.0)

set(FREERTOS_DIR ${EXAMPLES_DIR}/rtos/FreeRTOS-Kernel)

add_library(freertos_kernel OBJECT
    ${FREERTOS_DIR}/tasks.c
    ${FREERTOS_DIR}/queue.c
    ${FREERTOS_DIR}/list.c
    ${FREERTOS_DIR}/timers.c
    ${FREERTOS_DIR}/event_groups.c
    ${FREERTOS_DIR}/stream_buffer.c
    ${FREERTOS_DIR}/portable/GCC/ARM_CM4F/port.c
    ${FREERTOS_DIR}/portable/MemMang/heap_4.c
)
target_include_directories(freertos_kernel PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}              # FreeRTOSConfig.h
    ${FREERTOS_DIR}/include
    ${FREERTOS_DIR}/portable/GCC/ARM_CM4F
)
target_compile_options(freertos_kernel PRIVATE -Wno-pedantic -Wno-unused-parameter)

################################################################################
# Create target

set(PING_PONG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../ping_pong_example)

add_executable(ping_pong_freertos
    ${CMAKE_CURRENT_SOURCE_DIR}/main_ping_pong_freertos.c
    ${CMAKE_CURRENT_SOURCE_DIR}/rtos_hooks.c
    ${PING_PONG_DIR}/app_ping_pong.c          # reused unchanged
)
target_include_directories(ping_pong_freertos PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PING_PONG_DIR}
)
target_sources(ping_pong_freertos PRIVATE $<TARGET_OBJECTS:freertos_kernel>)
target_include_directories(ping_pong_freertos PRIVATE $<TARGET_PROPERTY:freertos_kernel,INTERFACE_INCLUDE_DIRECTORIES>)
# STM32 HAL headers (stm32f4xx_hal.h) for rtos_hooks.c
target_include_directories(ping_pong_freertos PRIVATE $<TARGET_PROPERTY:st_hal,INTERFACE_INCLUDE_DIRECTORIES>)
target_compile_definitions(ping_pong_freertos PRIVATE USE_HAL_DRIVER)

setup_example_target(ping_pong_freertos)
register_example(ping_pong_freertos "${DESCRIPTION}")
