cmake_minimum_required(VERSION 3.22)
project(gba_emulator LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(GBA_SANITIZE "Build host binaries with AddressSanitizer and UBSan" OFF)
if(GBA_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

add_subdirectory(core_common) # the rewind ring both cores share
add_subdirectory(core)
add_subdirectory(core_gb) # the Game Boy / Game Boy Color core (milestone 17)
add_subdirectory(machine) # one layer over both cores

# The speed benchmark: on the PC with the other tools, for Android on its own (tools/dev.sh phone-bench).
option(GBA_BENCH_ONLY "Build only the core, gbabench and the unit tests (Android executables for the phone)" OFF)
if(GBA_BENCH_ONLY)
    add_subdirectory(tools/common)
    add_subdirectory(tools/bench)
    add_subdirectory(tests/unit) # the unit tests run on the phone too (tools/dev.sh phone-unit)
    return()
endif()

if(NOT ANDROID)
    enable_testing()
    add_subdirectory(tests/unit)
    # The game check tool's own tests (tools/game_check.py's pure parts), with the rest.
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_FOUND)
        add_test(NAME game_check_tools COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/test_game_check.py)
        add_test(NAME public_scan_tools COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/test_public_scan.py)
    endif()
    add_subdirectory(tools/common)
    add_subdirectory(tools/testrunner)
    add_subdirectory(tools/bench)
    if(WIN32)
        add_subdirectory(tools/viewer)
    endif()

    # jsmolka/gba-tests ROMs, fetched by tools/fetch_test_roms.py; skipped when missing.
    # Each entry is name:result-register (the Thumb ROM keeps its result in r7).
    set(JSMOLKA_DIR ${PROJECT_SOURCE_DIR}/tests/roms/jsmolka)
    foreach(entry arm:12 thumb:7 memory:12 bios:12 sram:12 flash64:12 flash128:12 none:12)
        string(REPLACE ":" ";" parts "${entry}")
        list(GET parts 0 name)
        list(GET parts 1 reg)
        add_test(NAME jsmolka_${name} COMMAND testrunner ${JSMOLKA_DIR}/${name}.gba --result-reg ${reg})
        set_tests_properties(jsmolka_${name} PROPERTIES SKIP_RETURN_CODE 77)
    endforeach()

    # Our own test ROMs that report through r12 (0 = pass).
    foreach(name save_eeprom512 save_eeprom8k)
        add_test(NAME own_${name} COMMAND testrunner ${PROJECT_SOURCE_DIR}/tests/own_roms/${name}.gba --result-reg 12)
    endforeach()

    # A save state written by the build of 2026-10-05, before milestone 13's fixes: it must keep loading and
    # continue exactly (the second half of own_snd_mix), so players' saved states survive core changes.
    add_test(NAME oldstate_snd_mix
             COMMAND testrunner ${PROJECT_SOURCE_DIR}/tests/own_roms/snd_mix.gba
                     --load-state ${PROJECT_SOURCE_DIR}/tests/states/snd_mix_95.state --frames 95
                     --expect-hash 80a69197c1fb9325 --expect-audio-hash 7120fcf05baa3095)

    # Approved frame fingerprints; each runs a ROM for N frames and compares the picture hash.
    set(FRAMES_FILE ${PROJECT_SOURCE_DIR}/tests/expected_frames.json)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${FRAMES_FILE})
    file(READ ${FRAMES_FILE} FRAMES_JSON)
    string(JSON frame_count LENGTH "${FRAMES_JSON}" frames)
    if(frame_count GREATER 0)
        math(EXPR last_frame "${frame_count} - 1")
        foreach(i RANGE ${last_frame})
            string(JSON f_name GET "${FRAMES_JSON}" frames ${i} name)
            string(JSON f_rom GET "${FRAMES_JSON}" frames ${i} rom)
            string(JSON f_frames GET "${FRAMES_JSON}" frames ${i} frames)
            string(JSON f_hash GET "${FRAMES_JSON}" frames ${i} hash)
            string(JSON f_audio ERROR_VARIABLE audio_missing GET "${FRAMES_JSON}" frames ${i} audio)
            set(audio_args)
            if(NOT audio_missing)
                set(audio_args --expect-audio-hash ${f_audio})
            endif()
            add_test(NAME frame_${f_name}
                     COMMAND testrunner ${PROJECT_SOURCE_DIR}/tests/roms/${f_rom} --frames ${f_frames}
                             --expect-hash ${f_hash} ${audio_args})
            set_tests_properties(frame_${f_name} PROPERTIES SKIP_RETURN_CODE 77)
            # The same run through a save state: half-way it continues in a brand-new machine.
            math(EXPR f_half "${f_frames} / 2")
            if(f_half LESS 1)
                set(f_half 1)
            endif()
            add_test(NAME state_${f_name}
                     COMMAND testrunner ${PROJECT_SOURCE_DIR}/tests/roms/${f_rom} --frames ${f_frames}
                             --state-at ${f_half} --expect-hash ${f_hash} ${audio_args})
            set_tests_properties(state_${f_name} PROPERTIES SKIP_RETURN_CODE 77)
            # And through rewind (a snapshot every 4 frames): at two thirds (short runs: the last frame but one),
            # step back half of the snapshots so far, at least 2 - so every run unpacks at least one difference.
            math(EXPR f_rewind_at "${f_frames} * 2 / 3")
            if(f_rewind_at LESS 9)
                math(EXPR f_rewind_at "${f_frames} - 1")
            endif()
            math(EXPR f_rewind_steps "${f_rewind_at} / 4 / 2")
            if(f_rewind_steps LESS 2)
                set(f_rewind_steps 2)
            endif()
            add_test(NAME rewind_${f_name}
                     COMMAND testrunner ${PROJECT_SOURCE_DIR}/tests/roms/${f_rom} --frames ${f_frames}
                             --rewind-at ${f_rewind_at}:${f_rewind_steps} --expect-hash ${f_hash} ${audio_args})
            set_tests_properties(rewind_${f_name} PROPERTIES SKIP_RETURN_CODE 77)
        endforeach()
    endif()

    # Game Boy / Game Boy Color test ROMs (tests/roms/gb/, fetched by tools/fetch_test_roms.py): tests/gb_tests.json
    # lists each ROM and how it reports - "serial" / "memtext" (the text to find, default "Passed") or "mooneye".
    set(GB_TESTS_FILE ${PROJECT_SOURCE_DIR}/tests/gb_tests.json)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${GB_TESTS_FILE})
    file(READ ${GB_TESTS_FILE} GB_TESTS_JSON)
    string(JSON gb_count LENGTH "${GB_TESTS_JSON}" tests)
    if(gb_count GREATER 0)
        math(EXPR last_gb "${gb_count} - 1")
        foreach(i RANGE ${last_gb})
            string(JSON g_name GET "${GB_TESTS_JSON}" tests ${i} name)
            string(JSON g_rom GET "${GB_TESTS_JSON}" tests ${i} rom)
            string(JSON g_mode GET "${GB_TESTS_JSON}" tests ${i} mode)
            string(JSON g_expect ERROR_VARIABLE no_expect GET "${GB_TESTS_JSON}" tests ${i} expect)
            if(no_expect)
                set(g_expect Passed)
            endif()
            string(JSON g_frames ERROR_VARIABLE no_frames GET "${GB_TESTS_JSON}" tests ${i} frames)
            if(no_frames)
                set(g_frames 2400)
            endif()
            if(g_mode STREQUAL "mooneye")
                set(g_args --gb-mooneye)
            else()
                set(g_args --gb-${g_mode} ${g_expect})
            endif()
            add_test(NAME gb_${g_name}
                     COMMAND testrunner "${PROJECT_SOURCE_DIR}/tests/roms/gb/${g_rom}" ${g_args} --frames ${g_frames})
            set_tests_properties(gb_${g_name} PROPERTIES SKIP_RETURN_CODE 77)
        endforeach()
    endif()

    # mGBA test suite (tests/roms/mgba/suite.gba): each group must reach its minimum score.
    set(SUITE_FILE ${PROJECT_SOURCE_DIR}/tests/suite_targets.json)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${SUITE_FILE})
    file(READ ${SUITE_FILE} SUITE_JSON)
    string(JSON suite_count LENGTH "${SUITE_JSON}" groups)
    math(EXPR last_group "${suite_count} - 1")
    foreach(i RANGE ${last_group})
        string(JSON s_group MEMBER "${SUITE_JSON}" groups ${i})
        string(JSON s_min GET "${SUITE_JSON}" groups ${s_group})
        add_test(NAME mgba_suite_${s_group}
                 COMMAND testrunner ${PROJECT_SOURCE_DIR}/tests/roms/mgba/suite.gba --suite ${s_group} --suite-min ${s_min})
        set_tests_properties(mgba_suite_${s_group} PROPERTIES SKIP_RETURN_CODE 77)
    endforeach()
endif()
