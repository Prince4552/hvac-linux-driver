# SPDX-License-Identifier: MIT
# FreeRTOS port of the thermostat (Ext 4): the FreeRTOS kernel with its POSIX simulator port
# (fetched by scripts/fetch_freertos.sh into .cache/, never copied), plus our three tasks. The
# control core and the room model are compiled straight from thermostat/ - one copy of each.
cmake_minimum_required(VERSION 3.16)
project(hvac_rtos C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(REPO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(FREERTOS_DIR "${REPO_DIR}/.cache/FreeRTOS-Kernel-V11.3.3" CACHE PATH "FreeRTOS-Kernel checkout")
set(PORT_DIR "${FREERTOS_DIR}/portable/ThirdParty/GCC/Posix")
if(NOT EXISTS "${FREERTOS_DIR}/tasks.c")
	message(FATAL_ERROR "FreeRTOS not found at ${FREERTOS_DIR}; run scripts/fetch_freertos.sh")
endif()

set(HVAC_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Wconversion
	-Wsign-conversion -Werror)
find_package(Threads REQUIRED)

# Third-party kernel and port: default flags, no -Werror (like vendored Unity), GNU C for pthreads.
add_library(freertos_kernel STATIC
	"${FREERTOS_DIR}/tasks.c" "${FREERTOS_DIR}/queue.c" "${FREERTOS_DIR}/list.c"
	"${FREERTOS_DIR}/portable/MemMang/heap_4.c"
	"${PORT_DIR}/port.c" "${PORT_DIR}/utils/wait_for_event.c")
set_target_properties(freertos_kernel PROPERTIES C_EXTENSIONS ON)
target_include_directories(freertos_kernel SYSTEM PUBLIC
	"${FREERTOS_DIR}/include" "${PORT_DIR}" "${PORT_DIR}/utils" "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(freertos_kernel PUBLIC Threads::Threads)

# Our shared code, compiled from thermostat/ unchanged.
add_library(hvac_shared STATIC "${REPO_DIR}/thermostat/src/hvac_core.c"
	"${REPO_DIR}/thermostat/sim/room_model.c" "${REPO_DIR}/thermostat/sim/sil_trace.c")
target_include_directories(hvac_shared PUBLIC "${REPO_DIR}/thermostat/include"
	"${REPO_DIR}/thermostat/sim")
target_compile_options(hvac_shared PRIVATE ${HVAC_WARNINGS})

add_executable(hvac_rtos main.c plant_task.c control_task.c logger_task.c)
target_link_libraries(hvac_rtos PRIVATE hvac_shared freertos_kernel)
target_compile_options(hvac_rtos PRIVATE ${HVAC_WARNINGS})
