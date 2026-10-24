# Native library for the Android app (built by Gradle's externalNativeBuild from app/build.gradle.kts).
cmake_minimum_required(VERSION 3.22)
project(gbaemu LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# The emulator must run at full speed in the debug APK too: optimise, keep debug symbols.
set(CMAKE_C_FLAGS_DEBUG "-O2 -g")
# Link-time optimisation: the CPU loop, the memory bus and the instruction handlers live in different files;
# letting the compiler inline across them made games 15-24 % faster on a phone's small core (milestone 7a).
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)

set(REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
add_subdirectory(${REPO_ROOT}/core ${CMAKE_BINARY_DIR}/core)

file(GLOB PLATFORM_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/*.c)
add_library(gbaemu SHARED ${PLATFORM_SOURCES})
target_include_directories(gbaemu PRIVATE ${CMAKE_CURRENT_LIST_DIR} ${REPO_ROOT}/tools/common)
target_compile_options(gbaemu PRIVATE -Wall -Wextra -Wno-unused-parameter -Werror=unguarded-availability)
# Newer-API calls (e.g. AChoreographer_postFrameCallback64, Android 10+) link weakly and must sit
# behind __builtin_available checks; minSdk is 26.
target_compile_definitions(gbaemu PRIVATE __ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__)
target_link_libraries(gbaemu PRIVATE gbacore android log EGL GLESv3 aaudio)

# RetroAchievements (milestone 14): rcheevos and the glue (platform/ra).
include(${REPO_ROOT}/third_party/rcheevos.cmake)
target_link_libraries(gbaemu PRIVATE rcheevos)
target_sources(gbaemu PRIVATE ${REPO_ROOT}/platform/ra/ra.c)
target_include_directories(gbaemu PRIVATE ${REPO_ROOT}/platform/ra)
