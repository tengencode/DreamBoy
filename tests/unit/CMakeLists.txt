file(GLOB UNIT_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.c)
add_executable(unit_tests ${UNIT_SOURCES})
target_link_libraries(unit_tests PRIVATE gbacore gbcore machine)
target_include_directories(unit_tests PRIVATE ${PROJECT_SOURCE_DIR}/core/src ${PROJECT_SOURCE_DIR}/core_gb/src ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(unit_tests PRIVATE -Wall -Wextra -Wno-unused-parameter)
add_test(NAME unit_tests COMMAND unit_tests)
# The user's own BIOS for the twin tests against the real BIOS (gitignored; those tests skip without it).
target_compile_definitions(unit_tests PRIVATE GBA_TEST_BIOS="${PROJECT_SOURCE_DIR}/tests/bios/gba_bios.bin")

# The Android platform layer's plain-C pieces are tested here too (they use no Android API).
set(PLATFORM_DIR ${PROJECT_SOURCE_DIR}/platform/android)
target_sources(unit_tests PRIVATE ${PLATFORM_DIR}/fit_rect.c ${PLATFORM_DIR}/frame_pacer.c ${PLATFORM_DIR}/triple_buffer.c)
target_include_directories(unit_tests PRIVATE ${PLATFORM_DIR})
find_package(Threads REQUIRED)
target_link_libraries(unit_tests PRIVATE Threads::Threads)
target_sources(unit_tests PRIVATE ${PLATFORM_DIR}/audio_ring.c ${PLATFORM_DIR}/rate_control.c)
target_sources(unit_tests PRIVATE ${PLATFORM_DIR}/resampler.c)
target_sources(unit_tests PRIVATE ${PLATFORM_DIR}/key_latch.c)
target_sources(unit_tests PRIVATE ${PLATFORM_DIR}/save_file.c ${PLATFORM_DIR}/save_timer.c)

# RetroAchievements (milestone 14): rcheevos, the glue and its pretend server.
include(${PROJECT_SOURCE_DIR}/third_party/rcheevos.cmake)
target_link_libraries(unit_tests PRIVATE rcheevos)
target_sources(unit_tests PRIVATE ${PROJECT_SOURCE_DIR}/platform/ra/ra.c ${PROJECT_SOURCE_DIR}/platform/ra/ra_fake.c)
target_include_directories(unit_tests PRIVATE ${PROJECT_SOURCE_DIR}/platform/ra)
