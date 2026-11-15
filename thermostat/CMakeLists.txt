# SPDX-License-Identifier: MIT
# Build of the user-space thermostat code: the pure control library (hvac_core), its unit tests,
# and the room simulator (sil). Our code uses strict warnings with -Werror; vendored Unity is a
# separate target with default flags. Options: HVAC_SANITIZE (ASan + UBSan), HVAC_COVERAGE (gcov).
cmake_minimum_required(VERSION 3.16)
project(hvac_thermostat C)

option(HVAC_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(HVAC_COVERAGE "Build with gcov coverage instrumentation" OFF)
option(HVAC_BUILD_TESTS "Build unit tests and the simulator" ON)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
# POSIX functions (open, select, clock_gettime, mkstemp, ...) on top of plain C11.
add_compile_definitions(_POSIX_C_SOURCE=200809L)

set(HVAC_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Wconversion
	-Wsign-conversion -Werror)

if(HVAC_SANITIZE)
	add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all
		-fno-omit-frame-pointer)
	add_link_options(-fsanitize=address,undefined)
endif()
if(HVAC_COVERAGE)
	add_compile_options(--coverage -O0)
	add_link_options(--coverage)
endif()

# Pure control library: no I/O in hvac_core.c; config.c reads one file in config_load_file().
add_library(hvac_core STATIC src/hvac_core.c src/config.c)
target_include_directories(hvac_core PUBLIC include)
target_compile_options(hvac_core PRIVATE ${HVAC_WARNINGS})

if(HVAC_BUILD_TESTS)
	enable_testing()

	# Vendored test framework: its own flags, no -Werror (ENGINEERING_RULES.md 4.1).
	add_library(unity STATIC third_party/unity/unity.c)
	target_include_directories(unity PUBLIC third_party/unity)

	foreach(t test_hvac_core test_config)
		add_executable(${t} tests/${t}.c)
		target_link_libraries(${t} PRIVATE hvac_core unity)
		target_compile_options(${t} PRIVATE ${HVAC_WARNINGS})
		add_test(NAME ${t} COMMAND ${t})
	endforeach()

	# Software-in-the-loop room simulator.
	add_executable(sil sim/sil_main.c sim/room_model.c sim/baseline_controller.c
		sim/sil_metrics.c)
	target_include_directories(sil PRIVATE sim)
	target_link_libraries(sil PRIVATE hvac_core)
	target_compile_options(sil PRIVATE ${HVAC_WARNINGS})
endif()
