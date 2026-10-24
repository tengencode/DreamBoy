file(GLOB_RECURSE GBACORE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)
add_library(gbacore STATIC ${GBACORE_SOURCES})
target_include_directories(gbacore PUBLIC include PRIVATE src)
target_compile_options(gbacore PRIVATE -Wall -Wextra -Wno-unused-parameter)

# The warning log (milestone 13): a record of anything a game uses that the emulator skips. Only in the PC builds
# tools/dev.sh makes for tests and the game check (build/host, build/check) - never in the app or build/release.
option(GBA_NOTES "Build the core with the warning log" OFF)
if(GBA_NOTES)
    target_compile_definitions(gbacore PUBLIC GBA_NOTES=1) # PUBLIC: the unit tests see the same struct gba
endif()

# ARM64 fast paths (milestone 7b): assembly and NEON in arch/arm64, each with its plain-C twin in src/.
# GBA_FORCE_GENERIC builds an arm64 core with only the twins, so the two can be compared on a phone.
# The assembly is ELF-only (Android, Linux: not macOS or Windows on ARM) and skips the warning log's notes.
option(GBA_FORCE_GENERIC "On arm64, build the core with only the plain-C twins of the fast paths" OFF)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$" AND (ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
   AND NOT GBA_FORCE_GENERIC AND NOT GBA_NOTES)
    enable_language(ASM)
    file(GLOB GBACORE_ARM64 CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/arch/arm64/*.c ${CMAKE_CURRENT_SOURCE_DIR}/arch/arm64/*.S)
    target_sources(gbacore PRIVATE ${GBACORE_ARM64})
    target_include_directories(gbacore PRIVATE arch)
    target_compile_definitions(gbacore PUBLIC GBA_ARM64=1) # PUBLIC: the tests compare each fast path with its twin
endif()

