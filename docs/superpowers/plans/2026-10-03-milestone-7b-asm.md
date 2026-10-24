# Milestone 7b — Speed: ARM64 Assembly + NEON — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. (This project always executes plans inline with superpowers:executing-plans — see `CLAUDE.md`.)

**Goal:** The CPU loop in ARM64 assembly and the drawing in NEON, each with a plain-C twin (`GBA_FORCE_GENERIC`), changing no result — every speed scene at the agreed speed on phone B's small core.

**Architecture:** `cpu_run` runs a threaded loop: each handler runs one guest instruction and jumps straight to the next one's handler, with only the common instructions taken there and everything else (I/O, mode changes, interrupts, code outside RAM/ROM) through `cpu_step`. Its plain-C form (`cpu_run_generic`, `cpu/cpu_fast.h`, `musttail`) runs on the PC, the x86_64 emulator and with `GBA_FORCE_GENERIC`; on arm64 the same rules run in hand-written assembly (`core/arch/arm64/cpu_a64.S`) with the guest PC, the cycle count and a code "window" in registers. The compositor and whole 4bpp tile rows get NEON versions (`core/arch/arm64/ppu_neon.*`). Exactness: twin tests on random ARM/Thumb programs (the C loop vs `cpu_step`, the asm loop vs the C loop on the phone), 7a's speed-up tests (the drawing vs the pre-7a code), the 120 CTest fingerprints, the 53 on-device tests on the emulator and phone B (both builds), and the 6 scenes' hashes.

**Tech Stack:** C11, ARM64 assembly (GNU syntax, LLVM integrated assembler), NEON intrinsics (`arm_neon.h`), CMake/Ninja, NDK clang 27, Kotlin (one test), Git Bash `tools/dev.sh`, adb.

**Spec:** `docs/superpowers/specs/2026-10-03-milestone-7b-asm-design.md` (finish line in §1).

---

## Conventions (as 7a)

- Git Bash from the repo root. PC: `bash tools/dev.sh <configure|build|unit [filter]|test|run|release|bench [args]>` (a fresh worktree needs `configure` first). Android emulator: `bash tools/dev.sh <apk|jvmtest|emu|install|launch|androidtest>`. Gradle `--offline`; a missing artifact → stop and ask the user before any download.
- **The user's games** are read from `$GBA_GAMES` (default `~/Downloads`): `hack-1.gba` + `.sav`, `game-b.gba`, `game-c.gba`, `game-d.gba`. Never copy them into the repo or git.
- **The user's phone** (spec §4): the **phone B** (its serial, small cores cpu0-3, Cortex-A55 1.77 GHz), only through `dev.sh`'s `phone-*` commands, which refuse unless exactly one USB phone is plugged in. Allowed there: `gbabench`, the unit tests and the games in `/data/local/tmp/gbabench`, installing the app and its test package. **`GENERIC=1`** before a `phone-*` command uses the arm64 build with only the plain-C twins. If phone B is not plugged in when a step needs it, stop and ask the user to plug it in. Ask before restarting or cold-booting the emulator.
- The asm and NEON only run on the phone (the emulator is x86_64): `phone-unit` runs the unit tests there. **A step that runs the phone's unit tests uses `timeout 550`** — a bug in the loop can hang it (found in the prototype).
- `adb shell run-as <pkg> sh -c "…"` does not work (adb re-splits the words): plain `run-as <pkg> <command>` calls.
- Test-first where a test can fail first: the twin tests before the C loop (they do not compile without it), the asm twin tests before the asm (they do not link without it); the drawing tests (7a's) already pass on today's code and must still pass with NEON.
- One commit per task; messages end with the Co-Authored-By trailer. Each commit also includes `docs/PROGRESS.md` and this plan (ticked boxes) — `CLAUDE.md`. The tasks run one after another without stopping; the only stop is Task 6, the user's check. After every task: all documents updated and checked for anything stale.
- This plan's code is the prototype's (worktree `<scratchpad>/p7b`, branch `proto-7b`), which the user tried on phone B on 2026-10-03 ('everything works'). Planted-bug checks done there are listed in each task's notes (they need not be repeated).

## File map

```
core/CMakeLists.txt (GBA_FORCE_GENERIC); CMakeLists.txt (the unit tests in the phone build);
app/build.gradle.kts (-PgbaForceGeneric); tools/dev.sh (GENERIC=1, phone-unit, a failed build stops)     Task 1
tests/unit/test_cpu_twins.c (new); core/src/cpu/cpu.h, cpu.c, cpu_fast.h (new), arm.c, thumb.c;
core/src/mem/bus_inline.h (new), bus.c; core/src/gba.c                                                 Task 2
core/arch/arm64/cpu_a64.c (new), core/arch/arm64/cpu_a64.S (new)                                         Task 3
core/arch/arm64/ppu_neon.h (new), ppu_neon.c (new); core/src/ppu/ppu_internal.h, ppu_compose.c, ppu_bg.c  Task 4
app/src/androidTest/java/com/tengen/gbaemu/MotionTest.kt                                                                            Task 5
```

---
### Task 1: The switch and the phone's unit tests

**Files:**
- Modify: `core/CMakeLists.txt` (`GBA_FORCE_GENERIC`; on arm64 without it: `core/arch/arm64/*.c` / `*.S`, the `arch` include folder, `GBA_ARM64` defined), `CMakeLists.txt` (`GBA_BENCH_ONLY` also builds the unit tests), `app/build.gradle.kts` (`-PgbaForceGeneric=ON` passes `GBA_FORCE_GENERIC` to the native build), `tools/dev.sh` (`GENERIC=1`, `phone-unit [filter]` — run in `/data/local/tmp/gbabench`, as the save-file tests write in the current folder; `phone_bench_build || return 1`: a failed phone build no longer runs the old program — from 7a, found in the prototype)

(Build plumbing: its check is that the unit tests build and pass on the phone both ways.)

- [x] **Step 1: Write the files**

Replace `core/CMakeLists.txt`:

```cmake
file(GLOB_RECURSE GBACORE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)
add_library(gbacore STATIC ${GBACORE_SOURCES})
target_include_directories(gbacore PUBLIC include PRIVATE src)
target_compile_options(gbacore PRIVATE -Wall -Wextra -Wno-unused-parameter)

# ARM64 fast paths (milestone 7b): assembly and NEON in arch/arm64, each with its plain-C twin in src/.
# GBA_FORCE_GENERIC builds an arm64 core with only the twins, so the two can be compared on a phone.
option(GBA_FORCE_GENERIC "On arm64, build the core with only the plain-C twins of the fast paths" OFF)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$" AND NOT GBA_FORCE_GENERIC)
    enable_language(ASM)
    file(GLOB GBACORE_ARM64 CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/arch/arm64/*.c ${CMAKE_CURRENT_SOURCE_DIR}/arch/arm64/*.S)
    target_sources(gbacore PRIVATE ${GBACORE_ARM64})
    target_include_directories(gbacore PRIVATE arch)
    target_compile_definitions(gbacore PUBLIC GBA_ARM64=1) # PUBLIC: the tests compare each fast path with its twin
endif()
```

Replace `CMakeLists.txt`:

```cmake
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

add_subdirectory(core)

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
```

Replace `app/build.gradle.kts`:

```kotlin
plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

android {
    namespace = "com.tengen.gbaemu"
    compileSdk = 36
    ndkVersion = "27.2.12479018"

    defaultConfig {
        applicationId = "com.tengen.gbaemu"
        minSdk = 26
        targetSdk = 36
        versionCode = 1
        versionName = "0.1"
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        ndk { abiFilters += listOf("x86_64", "arm64-v8a") }
        // -PgbaForceGeneric=ON: the arm64 core with only the plain-C twins of its fast paths (tools/dev.sh GENERIC=1)
        externalNativeBuild { cmake { arguments += "-DGBA_FORCE_GENERIC=${project.findProperty("gbaForceGeneric") ?: "OFF"}" } }
    }

    externalNativeBuild {
        cmake {
            path = file("../platform/android/CMakeLists.txt")
            version = "3.31.6"
        }
    }

    buildFeatures { compose = true }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    // On-device tests read the local test ROMs + approved fingerprints from the test APK's assets
    // (copied from the gitignored tests/ folders at build time; never in the app APK or in git).
    sourceSets.getByName("androidTest").assets.srcDir(layout.buildDirectory.dir("generated/testRoms").get().asFile)
}

kotlin {
    compilerOptions { jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17) }
}

val copyTestRoms by tasks.registering(Copy::class) {
    from(rootProject.file("tests")) {
        include("expected_frames.json", "roms/jsmolka/*.gba", "roms/tonc/*.gba", "own_roms/*.gba")
    }
    into(layout.buildDirectory.dir("generated/testRoms"))
}
tasks.matching { it.name == "mergeDebugAndroidTestAssets" }.configureEach { dependsOn(copyTestRoms) }

dependencies {
    implementation("androidx.core:core-ktx:1.17.0")
    implementation("androidx.activity:activity-compose:1.12.3")
    implementation("androidx.compose.ui:ui-android:1.11.4")
    implementation("androidx.compose.foundation:foundation-android:1.11.4")
    implementation("androidx.compose.material3:material3-android:1.4.0")
    testImplementation("junit:junit:4.13.2")
    androidTestImplementation("androidx.test.ext:junit:1.3.0")
    androidTestImplementation("androidx.test:runner:1.7.0")
}
```

Replace `tools/dev.sh`:

```bash
#!/usr/bin/env bash
# Host build helper (Git Bash on Windows).
#   tools/dev.sh configure        configure build/host (Debug; sanitizers unless GBA_SANITIZE=OFF)
#   tools/dev.sh build            build everything
#   tools/dev.sh unit [filter]    build, then run unit tests whose name contains filter
#   tools/dev.sh test             build, then run every CTest test
#   tools/dev.sh run <args>       build, then run tools/testrunner with args
#   tools/dev.sh view <args>      build the optimised build/release, then run its gbaview (Windows)
#   tools/dev.sh release          configure (if needed) and build build/release (Release, no sanitizers)
#   tools/dev.sh bench <args>     build/release, then gbabench (speed scenes, tests/bench/scenes.txt) with the
#                                 games from $GBA_GAMES (default ~/Downloads); e.g. bench --check, bench --only game_b
# Android (Gradle project in app/; always the Pixel 9 phone emulator, never a USB phone; Gradle runs --offline):
#   tools/dev.sh apk              build the debug app (app/build/outputs/apk/debug/app-debug.apk)
#   tools/dev.sh jvmtest          Kotlin unit tests on the PC
#   tools/dev.sh emu              start the phone emulator if it is not running; wait until it has booted
#   tools/dev.sh install          emu + install the debug app
#   tools/dev.sh launch           emu + start the app
#   tools/dev.sh androidtest      emu + run the on-device tests
#   tools/dev.sh push <file>      emu + copy a file into the phone emulator's Download folder
# The user's real phone — ONLY when the user asks; exactly one USB phone (never an emulator), else nothing happens:
#   tools/dev.sh phone-install    build the debug app and install it on the phone (keeps the app's data)
#   tools/dev.sh phone-uninstall  remove the app from the phone
#   tools/dev.sh phone-push <file>  copy a file into the phone's Download folder
#   tools/dev.sh phone-bench <args>   build gbabench for arm64, copy it, the scenes and the games to
#                                 /data/local/tmp/gbabench (only what changed), run it (timed parts on the slowest core)
#   tools/dev.sh phone-profile <scene>  the scene once with gbabench --profile; prints where the time went
#   tools/dev.sh phone-unit [filter]  build the unit tests for arm64 and run them on the phone (the only place the
#                                 ARM64 fast paths are compared with their plain-C twins)
#   tools/dev.sh phone-androidtest    the on-device tests on the phone (installs the app and its tests)
#   tools/dev.sh phone-clean          delete /data/local/tmp/gbabench from the phone
#   GENERIC=1 before phone-bench / phone-profile / phone-unit / phone-androidtest / phone-install: the arm64 build
#                                 with only the plain-C twins (GBA_FORCE_GENERIC=ON), to compare with the fast one
# The Debug + sanitizer build is for tests; it runs real games at only ~30-45 fps, so the viewer
# always uses the release build (about 9x faster than real time).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LAD="$(cygpath -u "${LOCALAPPDATA}")"
SDK="${ANDROID_HOME:+$(cygpath -u "$ANDROID_HOME")}"
SDK="${SDK:-$LAD/Android/Sdk}"
export PATH="$SDK/cmake/3.31.6/bin:$PATH"

if [ -z "${LLVM_MINGW_BIN:-}" ]; then
    for d in "$LAD"/Microsoft/WinGet/Packages/MartinStorsjo.LLVM-MinGW.UCRT*/llvm-mingw-*/bin; do
        [ -x "$d/clang.exe" ] && LLVM_MINGW_BIN="$d"
    done
fi
if [ -n "${LLVM_MINGW_BIN:-}" ]; then
    export PATH="$LLVM_MINGW_BIN:$PATH"
fi
command -v clang >/dev/null || {
    echo "clang not found: install llvm-mingw (winget install MartinStorsjo.LLVM-MinGW.UCRT) or set LLVM_MINGW_BIN" >&2
    exit 1
}

BUILD="$ROOT/build/host"
GENERIC_FLAG=$([ "${GENERIC:-0}" = 1 ] && echo ON || echo OFF)
RELEASE="$ROOT/build/release"

build_release() {
    if [ ! -f "$RELEASE/build.ninja" ]; then
        cmake -S "$ROOT" -B "$RELEASE" -G Ninja -DCMAKE_BUILD_TYPE=Release             -DCMAKE_C_COMPILER=clang -DGBA_SANITIZE=OFF
    fi
    cmake --build "$RELEASE"
}
# ---- Android ----
JBR="/c/Program Files/Android/Android Studio/jbr" # Android Studio's own Java, which runs Gradle
AVD="Pixel_9_API_36_1"
APP_ID="com.tengen.gbaemu"
# MSYS_NO_PATHCONV: stop Git Bash rewriting phone paths such as /sdcard/... into Windows paths.
adb() { MSYS_NO_PATHCONV=1 "$SDK/platform-tools/adb" "$@"; }
gradle_run() {
    [ -f "$ROOT/local.properties" ] || printf 'sdk.dir=%s\n' "$(cygpath -m "$SDK")" > "$ROOT/local.properties"
    (cd "$ROOT" && JAVA_HOME="$(cygpath -m "$JBR")" ./gradlew --offline -PgbaForceGeneric="$GENERIC_FLAG" "$@")
}
emu_serial() { adb devices | awk '$1 ~ /^emulator-/ && $2 == "device" { print $1; exit }'; }
# The one USB-connected real phone (never an emulator); an error unless there is exactly one.
phone_serial() {
    local all n
    all=$(adb devices | awk 'NR > 1 && $2 == "device" && $1 !~ /^emulator-/ { print $1 }')
    n=$(printf '%s' "$all" | grep -c . || true)
    if [ "$n" != 1 ]; then
        echo "need exactly one USB phone with USB debugging on; found $n" >&2
        return 1
    fi
    echo "$all"
}
phone_name() { echo "$(adb -s "$1" shell getprop ro.product.model | tr -d '\r') ($1)"; }
# Start the phone emulator if needed, then point adb AND Gradle at it alone (ANDROID_SERIAL), so a
# real phone plugged in over USB is never used.
emu_up() {
    if [ -z "$(emu_serial)" ]; then
        echo "starting the phone emulator ($AVD)..."
        nohup "$SDK/emulator/emulator" -avd "$AVD" -no-boot-anim >/dev/null 2>&1 &
        disown
    fi
    until [ -n "$(emu_serial)" ]; do sleep 2; done
    export ANDROID_SERIAL="$(emu_serial)"
    until [ "$(adb shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = "1" ]; do sleep 2; done
}

# ---- gbabench on the phone ----
NDK="$SDK/ndk/27.2.12479018"
PHONE_BUILD="$ROOT/build/phone-arm64"
[ "$GENERIC_FLAG" = OFF ] || PHONE_BUILD="$PHONE_BUILD-generic"
PHONE_DIR=/data/local/tmp/gbabench
GAMES="${GBA_GAMES:-$HOME/Downloads}"
# gbabench for arm64 with the app's own optimisation (-O2, link-time optimisation), so the numbers are the app's.
phone_bench_build() {
    if [ ! -f "$PHONE_BUILD/build.ninja" ]; then
        cmake -S "$ROOT" -B "$PHONE_BUILD" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
            -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=26 -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS_RELEASE="-O2 -g" \
            -DGBA_BENCH_ONLY=ON -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON -DGBA_FORCE_GENERIC=$GENERIC_FLAG >/dev/null 2>&1
    fi
    local out
    if ! out=$(cmake --build "$PHONE_BUILD" 2>&1); then
        echo "$out" | grep -E "error" >&2
        return 1
    fi
}
# Copy $1 to the phone as $2 unless a file of the same size is there already
# (</dev/null: adb would otherwise eat the caller's list of games).
phone_copy() {
    local size; size=$(stat -c %s "$1")
    if [ "$(adb -s "$PHONE" shell stat -c %s "'$2'" </dev/null 2>/dev/null | tr -d '\r')" != "$size" ]; then
        echo "copying $(basename "$1") to the phone"
        adb -s "$PHONE" push "$(cygpath -w "$1")" "$2" </dev/null >/dev/null 2>&1
    fi
}
# Everything gbabench needs on the phone: itself (always), the scene list, and every game a scene names.
phone_bench_push() {
    phone_bench_build || return 1 # a failed build must not run the old program
    adb -s "$PHONE" shell mkdir -p "$PHONE_DIR/roms/tonc"
    adb -s "$PHONE" push "$(cygpath -w "$PHONE_BUILD/tools/bench/gbabench")" "$PHONE_DIR/gbabench" >/dev/null 2>&1
    adb -s "$PHONE" shell chmod 755 "$PHONE_DIR/gbabench"
    adb -s "$PHONE" push "$(cygpath -w "$ROOT/tests/bench/scenes.txt")" "$PHONE_DIR/scenes.txt" >/dev/null 2>&1
    local rom save
    while IFS='|' read -r _ rom save _; do
        rom=$(echo "$rom" | xargs); save=$(echo "$save" | xargs)
        for f in "$rom" "$save"; do
            [ -z "$f" ] || [ "$f" = "-" ] && continue
            case "$f" in tonc/*) phone_copy "$ROOT/tests/roms/$f" "$PHONE_DIR/roms/$f" ;;
                *) phone_copy "$GAMES/$f" "$PHONE_DIR/roms/$f" ;; esac
        done
    done < <(grep -v '^#' "$ROOT/tests/bench/scenes.txt" | grep '|')
}
# The slowest core: the last of those with the lowest cpu_capacity, or the lowest top clock speed on phones that
# have no cpu_capacity (cpu0 also serves much of the phone's own work).
phone_small_core() {
    adb -s "$PHONE" shell 'for c in /sys/devices/system/cpu/cpu[0-9]*; do
        echo "$(cat $c/cpu_capacity 2>/dev/null || cat $c/cpufreq/cpuinfo_max_freq) ${c##*cpu}"; done' |
        tr -d '\r' | sort -n -k1,1 -k2,2 | awk 'NR == 1 { cap = $1 } $1 == cap { core = $2 } END { print core }'
}

cmd="${1:-}"
shift || true
case "$cmd" in
    configure)
        cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
            -DCMAKE_C_COMPILER=clang -DGBA_SANITIZE="${GBA_SANITIZE:-ON}" ;;
    build) cmake --build "$BUILD" ;;
    unit)  cmake --build "$BUILD" && "$BUILD/tests/unit/unit_tests" "$@" ;;
    test)  cmake --build "$BUILD" && ctest --test-dir "$BUILD" --output-on-failure "$@" ;;
    run)   cmake --build "$BUILD" && "$BUILD/tools/testrunner/testrunner" "$@" ;;
    view)  build_release && "$RELEASE/tools/viewer/gbaview" "$@" ;;
    release) build_release ;;
    apk) gradle_run :app:assembleDebug ;;
    jvmtest) gradle_run :app:testDebugUnitTest ;;
    emu) emu_up ;;
    install) emu_up && gradle_run :app:installDebug ;;
    launch) emu_up && adb shell am start -a android.intent.action.MAIN -c android.intent.category.LAUNCHER -n "$APP_ID/.MainActivity" ;; # as the app icon does
    androidtest) emu_up && gradle_run :app:connectedDebugAndroidTest ;;
    push) emu_up && adb push "$(cygpath -w "$1")" /sdcard/Download/ ;; # adb is a Windows program: give it a Windows path
    phone-install)
        s=$(phone_serial) && echo "installing the app on $(phone_name "$s")" && gradle_run :app:assembleDebug &&
            adb -s "$s" install -r "$(cygpath -w "$ROOT/app/build/outputs/apk/debug/app-debug.apk")" ;;
    phone-uninstall) s=$(phone_serial) && echo "removing the app from $(phone_name "$s")" && adb -s "$s" uninstall "$APP_ID" ;;
    bench) build_release && (cd "$ROOT" && "$RELEASE/tools/bench/gbabench" --roms "$GAMES" "$@") ;;
    phone-bench)
        PHONE=$(phone_serial) && echo "speed scenes on $(phone_name "$PHONE")" && phone_bench_push &&
            core=$(phone_small_core) && echo "pinned to cpu$core" &&
            adb -s "$PHONE" shell "cd $PHONE_DIR && ./gbabench --roms roms --scenes scenes.txt --cpu $core $*" ;;
    phone-profile)
        PHONE=$(phone_serial) && phone_bench_push && core=$(phone_small_core) &&
            adb -s "$PHONE" shell "cd $PHONE_DIR && ./gbabench --roms roms --scenes scenes.txt --runs 1 --cpu $core --profile samples.txt --only $1" &&
            adb -s "$PHONE" pull "$PHONE_DIR/samples.txt" "$(cygpath -w "$PHONE_BUILD/samples.txt")" >/dev/null 2>&1 &&
            python "$(cygpath -w "$ROOT/tools/bench/profile_report.py")" \
                "$(cygpath -w "$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-symbolizer.exe")" \
                "$(cygpath -w "$PHONE_BUILD/tools/bench/gbabench")" "$(cygpath -w "$PHONE_BUILD/samples.txt")" ;;
    phone-unit)
        PHONE=$(phone_serial) && echo "unit tests on $(phone_name "$PHONE") (GBA_FORCE_GENERIC=$GENERIC_FLAG)" &&
            phone_bench_build && adb -s "$PHONE" shell mkdir -p "$PHONE_DIR" &&
            adb -s "$PHONE" push "$(cygpath -w "$PHONE_BUILD/tests/unit/unit_tests")" "$PHONE_DIR/unit_tests" >/dev/null 2>&1 &&
            adb -s "$PHONE" shell "chmod 755 $PHONE_DIR/unit_tests && cd $PHONE_DIR && ./unit_tests $*" ;; # cd: the save-file tests write in the current folder
    phone-androidtest) PHONE=$(phone_serial) && echo "on-device tests on $(phone_name "$PHONE")" && ANDROID_SERIAL="$PHONE" gradle_run :app:connectedDebugAndroidTest ;;
    phone-clean) PHONE=$(phone_serial) && adb -s "$PHONE" shell rm -rf "$PHONE_DIR" && echo "removed $PHONE_DIR" ;;
    phone-push) s=$(phone_serial) && echo "copying to $(phone_name "$s")" && adb -s "$s" push "$(cygpath -w "$1")" /sdcard/Download/ ;;
    *) echo "usage: tools/dev.sh configure|build|unit [filter]|test|run <args>|view <args>|release|apk|jvmtest|emu|install|launch|androidtest|push <file>|bench <args>|phone-install|phone-uninstall|phone-push <file>|phone-bench <args>|phone-profile <scene>|phone-unit [filter]|phone-androidtest|phone-clean" >&2; exit 2 ;;
esac
```

- [x] **Step 2: The unit tests on the PC and on phone B, both builds**

```bash
bash tools/dev.sh unit 2>&1 | tail -1
timeout 550 bash tools/dev.sh phone-unit 2>&1 | grep -E "unit tests on|tests,"
timeout 550 env GENERIC=1 bash tools/dev.sh phone-unit 2>&1 | grep -E "unit tests on|tests,"
```

Expected: `410 tests, 0 failed`; on phone B `unit tests on phone B (its serial) (GBA_FORCE_GENERIC=OFF)` then `410 tests, 0 failed`, and the same with `=ON` (`core/arch/arm64` does not exist yet: both builds are the same C).

- [x] **Step 3: Commit**

```bash
git add core/CMakeLists.txt CMakeLists.txt app/build.gradle.kts tools/dev.sh docs/PROGRESS.md docs/superpowers/plans/2026-10-03-milestone-7b-asm.md
git commit -m "build: GBA_FORCE_GENERIC, the unit tests on the phone (phone-unit, GENERIC=1)" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 2: The threaded C loop and the twin tests

**Files:**
- Create: `tests/unit/test_cpu_twins.c` (random ARM and Thumb programs in IWRAM and in the ROM, random flags, WAITCNT and prefetch buffer, base registers into RAM, the ROM, the palette, VRAM — also its mirrored top — and OAM; compared every 3000 cycles: registers, banks, flags, the clock, all RAM / VRAM / OAM / palette, I/O, IRQ, DMA, timers): the C loop vs `cpu_step` one at a time, and on arm64 the asm loop vs the C loop
- Create: `core/src/cpu/cpu_fast.h` (the threaded loop's rules: `musttail` dispatch, the fetch, the refill, fast data pointers — EWRAM, IWRAM, palette / VRAM / OAM but not byte stores there, ROM loads —, block transfers, the generic in-loop handler as `cpu_step` runs it), `core/src/mem/bus_inline.h` (the bus's fast paths, inline)
- Modify: `core/src/cpu/arm.c` (data processing specialised by opcode / S / operand kind; fast handlers for data processing, B/BL, LDR/STR(B), LDM/STM; `arm_class` for the asm table), `core/src/cpu/thumb.c` (copies specialised by their variant bits; fast handlers for the ALU / immediate / shift / hi-register forms, B<cond>, B, BL, the loads / stores, PUSH/POP, LDMIA/STMIA; `thumb_class`), `core/src/cpu/cpu.c` (`cpu_run_generic`; `cpu_run` → `a64_cpu_run` on arm64; the condition table shared), `core/src/cpu/cpu.h`, `core/src/mem/bus.c` (uses `bus_inline.h`), `core/src/gba.c` (`run_frame` calls `cpu_run`)

- [x] **Step 1: Write the failing tests**

Create `tests/unit/test_cpu_twins.c`:

```c
/*
 * Milestone 7b: the CPU loops against each other on random instruction streams. The threaded C loop
 * (cpu_run_generic) must match cpu_step run one instruction at a time, and on arm64 the assembly loop
 * (a64_cpu_run) must match the C loop: every register, flag, mode, memory byte, I/O register and cycle.
 */
#include <string.h>

#include "cpu/cpu.h"
#include "gba_internal.h"
#include "test.h"
#include "testutil.h"

static uint32_t rng;

static uint32_t rnd(void) { /* xorshift32: the same streams on every run */
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static uint32_t pick(uint32_t n) {
    return rnd() % n;
}

#define CODE 0x03000000u   /* the random program: 0x1000 bytes of IWRAM */
#define CODE_SIZE 0x1000u
#define DATA_IW 0x03004000u /* where the base registers point */
#define DATA_EW 0x02010000u

/* A condition: mostly always, sometimes any. */
static uint32_t cond(void) {
    return pick(4) ? 0xEu << 28 : pick(16) << 28;
}

/* An ARM instruction from the classes the loops treat differently. Data-processing results go to r0-r7, so the
 * base registers r8-r12 keep pointing at RAM (or the ROM) most of the time. */
static uint32_t arm_op(void) {
    uint32_t c = cond(), k = pick(100);
    uint32_t rd = pick(8), rn = pick(16), rm = pick(16), base = 8 + pick(5);
    if (k < 40) { /* data processing */
        uint32_t opc = pick(16), s = pick(2), op2;
        if (opc >= 8 && opc <= 11) s = 1; /* TST..CMN without S are other instructions */
        switch (pick(3)) {
        case 0: op2 = (1u << 25) | pick(16) << 8 | pick(256); break;                     /* immediate */
        case 1: op2 = pick(32) << 7 | pick(4) << 5 | rm; break;                            /* shifted by an immediate */
        default: op2 = pick(8) << 8 | pick(4) << 5 | 1u << 4 | rm; break;                  /* shifted by a register */
        }
        return c | opc << 21 | s << 20 | rn << 16 | rd << 12 | op2;
    }
    if (k < 62) { /* LDR / STR (B) */
        uint32_t off = pick(3) ? pick(64) : pick(4096);
        uint32_t reg = pick(4) ? 0 : (1u << 25 | pick(32) << 7 | pick(4) << 5 | pick(8)); /* or a shifted register */
        uint32_t pre = pick(4) ? 1 : 0, wb = pre ? pick(4) == 0 : 0;
        return c | 1u << 26 | (reg ? reg : off) | pre << 24 | pick(2) << 23 | pick(2) << 22 | wb << 21 |
               pick(2) << 20 | base << 16 | rd << 12;
    }
    if (k < 70) { /* LDRH / STRH / LDRSB / LDRSH */
        uint32_t sh = 1 + pick(3), load = sh == 1 ? pick(2) : 1;
        return c | 1u << 24 | pick(2) << 23 | 1u << 22 | load << 20 | base << 16 | rd << 12 | pick(16) << 8 |
               1u << 7 | sh << 5 | 1u << 4 | pick(16);
    }
    if (k < 80) { /* LDM / STM */
        uint32_t list = pick(0x10000) & ~(0x1Fu << 8); /* not the base registers */
        if (pick(8) == 0) list |= 1u << base;
        if (pick(8)) list &= 0x7FFF; /* mostly without PC */
        if (!list) list = 1;
        return c | 4u << 25 | pick(2) << 24 | pick(2) << 23 | (pick(10) == 0) << 22 | pick(2) << 21 |
               pick(2) << 20 | base << 16 | list;
    }
    if (k < 88) { /* B / BL, nearby */
        int32_t off = (int32_t)pick(40) - 20;
        return c | 5u << 25 | pick(2) << 24 | ((uint32_t)off & 0xFFFFFF);
    }
    if (k < 93) { /* MUL / MLA / long */
        if (pick(2)) return c | pick(2) << 21 | pick(2) << 20 | rd << 16 | pick(8) << 12 | pick(8) << 8 | 9u << 4 | pick(8);
        return c | 1u << 23 | pick(4) << 21 | pick(2) << 20 | rd << 16 | ((rd + 1) & 7) << 12 | pick(8) << 8 | 9u << 4 | pick(8);
    }
    if (k < 96) { /* MRS / MSR (flags) */
        if (pick(2)) return c | 0x010F0000u | rd << 12;
        return c | 0x0328F000u | pick(16) << 8 | pick(256); /* MSR CPSR_f, #imm */
    }
    if (k < 98) return c | 0x01000090u | pick(2) << 22 | base << 16 | rd << 12 | pick(8); /* SWP */
    return rnd(); /* anything at all */
}

/* A Thumb instruction: ALU results go to r0-r3, r4-r7 hold RAM addresses. */
static uint16_t thumb_op(void) {
    uint32_t k = pick(100), rd = pick(4), rs = pick(8), base = 4 + pick(4);
    if (k < 10) return (uint16_t)(pick(3) << 11 | pick(32) << 6 | rs << 3 | rd);                /* shift by imm */
    if (k < 16) return (uint16_t)(0x1800 | pick(4) << 9 | pick(8) << 6 | rs << 3 | rd);          /* add/sub */
    if (k < 26) return (uint16_t)(0x2000 | pick(4) << 11 | rd << 8 | pick(256));                 /* imm8 */
    if (k < 38) return (uint16_t)(0x4000 | pick(16) << 6 | rs << 3 | rd);                        /* ALU */
    if (k < 42) return (uint16_t)(0x4400 | pick(3) << 8 | pick(2) << 7 | pick(16) << 3 | rd);    /* hi registers */
    if (k < 46) return (uint16_t)(0x4800 | rd << 8 | pick(256));                                 /* LDR [PC] */
    if (k < 52) return (uint16_t)(0x5000 | pick(8) << 9 | pick(4) << 6 | base << 3 | rd);        /* [Rb, Ro] */
    if (k < 62) return (uint16_t)(0x6000 | pick(4) << 11 | pick(32) << 6 | base << 3 | rd);      /* [Rb, #imm] */
    if (k < 66) return (uint16_t)(0x8000 | pick(2) << 11 | pick(32) << 6 | base << 3 | rd);      /* halfwords */
    if (k < 72) return (uint16_t)(0x9000 | pick(2) << 11 | rd << 8 | pick(256));                 /* [SP, #imm] */
    if (k < 75) return (uint16_t)(0xA000 | pick(2) << 11 | rd << 8 | pick(256));                 /* ADD PC/SP */
    if (k < 78) return (uint16_t)(0xB000 | pick(256));                                           /* ADD SP */
    if (k < 84) return (uint16_t)(0xB400 | pick(2) << 11 | pick(2) << 8 | pick(256));            /* PUSH / POP */
    if (k < 87) return (uint16_t)(0xC000 | pick(2) << 11 | base << 8 | (pick(16) & 0x0F));       /* LDMIA / STMIA */
    if (k < 93) return (uint16_t)(0xD000 | pick(14) << 8 | ((uint32_t)((int32_t)pick(24) - 12) & 0xFF)); /* B<cond> */
    if (k < 96) return (uint16_t)(0xE000 | ((uint32_t)((int32_t)pick(24) - 12) & 0x7FF));      /* B */
    if (k < 98) return (uint16_t)(0xF000 | (pick(2) ? 0x800 : 0) | pick(8));                     /* BL halves */
    return (uint16_t)rnd();
}

/* Where a base register points: mostly RAM, else the ROM, the palette, VRAM (also its mirrored top) or OAM. */
static uint32_t data_pointer(bool rom) {
    switch (pick(10)) {
    case 0: return DATA_EW + pick(0x1000);
    case 1: return rom ? 0x08000000u + pick(0x100) : DATA_EW + pick(0x1000);
    case 2: return 0x05000000u + pick(0x380);
    case 3: return 0x06000000u + (pick(2) ? 0x17C00u + pick(0x400) : pick(0x1000)); /* also across 0x18000 */
    case 4: return 0x07000000u + pick(0x380);
    default: return DATA_IW + pick(0x1000);
    }
}

#define ROM_CODE 0x400u /* the program's place in the ROM, when it runs from there */

/* A machine with a random program at CODE (in IWRAM, or in the ROM: the cartridge's wait states and prefetch
 * buffer), random registers and flags, bases into RAM and the ROM. */
static gba_t *machine(uint32_t seed, bool thumb, bool in_rom) {
    static uint32_t rom[(ROM_CODE + CODE_SIZE) / 4];
    static uint8_t code[CODE_SIZE];
    rng = seed;
    for (uint32_t a = 0; a < CODE_SIZE; a += thumb ? 2 : 4) {
        uint32_t op = thumb ? thumb_op() : arm_op();
        for (uint32_t b = 0; b < (thumb ? 2u : 4u); b++) code[a + b] = (uint8_t)(op >> (8 * b));
    }
    rom[0] = 0xEAFFFFFE; /* b . */
    for (uint32_t i = 1; i < ROM_CODE / 4; i++) rom[i] = rnd();
    memcpy(&rom[ROM_CODE / 4], code, CODE_SIZE);
    gba_t *g = tu_arm(rom, TU_LEN(rom));
    if (!in_rom) memcpy(g->bus.iwram, code, CODE_SIZE);
    for (uint32_t i = 0; i < 0x2000; i++) g->bus.iwram[0x4000 + i] = (uint8_t)rnd();
    for (uint32_t i = 0; i < 0x2000; i++) g->bus.ewram[0x10000 + i] = (uint8_t)rnd();
    arm_cpu_t *c = &g->cpu;
    for (int i = 0; i < 8; i++) c->r[i] = rnd();
    for (int i = 8; i < 13; i++) c->r[i] = data_pointer(true);
    if (thumb)
        for (int i = 4; i < 8; i++) c->r[i] = data_pointer(false);
    c->r[13] = DATA_IW + 0x1800 + pick(0x400);
    uint32_t start = in_rom ? 0x08000000u + ROM_CODE : CODE;
    c->r[14] = start + pick(CODE_SIZE / 4) * 4 + (thumb ? 1 : 0);
    c->r[15] = start;
    c->cpsr = (rnd() & 0xF0000000u) | MODE_SYS | (thumb ? CPSR_T : 0);
    c->next_fetch_seq = false;
    if (pick(2)) bus_set_waitcnt(g, (uint16_t)(rnd() & 0x7FFF)); /* other wait states, the prefetch buffer */
    return g;
}

/* Runs the machine for about `cycles` with `run` for the CPU and the events in between (as run_frame). */
static void run_for(gba_t *g, uint64_t cycles, void (*run)(gba_t *)) {
    uint64_t end = g->now + cycles;
    while (g->now < end && !g->irq.stopped) {
        while (g->now < sched_next(&g->sched) && !g->irq.stopped) {
            if (g->irq.halted) {
                if (!irq_wake(g)) {
                    uint64_t next = sched_next(&g->sched);
                    if (g->irq.pending && irq_wake_at(g) < next) {
                        g->now = irq_wake_at(g);
                        continue;
                    }
                    g->now = next;
                    break;
                }
                g->irq.halted = false;
            }
            run(g);
        }
        gba_run_events(g);
    }
}

/* The CPU as it was before 7b: one cpu_step at a time. */
static void run_by_steps(gba_t *g) {
    while (g->now < sched_next(&g->sched) && !g->irq.halted && !g->irq.stopped) cpu_step(g);
}

/* Everything an instruction can change, except the per-instruction scratch flags (branched, data_access, ...). */
static bool same(const gba_t *a, const gba_t *b) {
    const arm_cpu_t *x = &a->cpu, *y = &b->cpu;
    bool ok = !memcmp(x->r, y->r, sizeof x->r) && x->cpsr == y->cpsr && !memcmp(x->spsr, y->spsr, sizeof x->spsr) &&
              !memcmp(x->bank_r13, y->bank_r13, sizeof x->bank_r13) && !memcmp(x->bank_r14, y->bank_r14, sizeof x->bank_r14) &&
              !memcmp(x->bank_usr_r8_12, y->bank_usr_r8_12, sizeof x->bank_usr_r8_12) &&
              !memcmp(x->bank_fiq_r8_12, y->bank_fiq_r8_12, sizeof x->bank_fiq_r8_12) && x->next_fetch_seq == y->next_fetch_seq;
    ok = ok && a->now == b->now && !memcmp(a->bus.ewram, b->bus.ewram, EWRAM_SIZE) &&
         !memcmp(a->bus.iwram, b->bus.iwram, IWRAM_SIZE) && !memcmp(a->bus.vram, b->bus.vram, VRAM_SIZE) &&
         !memcmp(a->bus.pal, b->bus.pal, PAL_SIZE) && !memcmp(a->bus.oam, b->bus.oam, OAM_SIZE);
    ok = ok && a->bus.open_bus == b->bus.open_bus && a->bus.in_bios == b->bus.in_bios &&
         a->bus.bios_latch == b->bus.bios_latch && a->bus.prefetch_end == b->bus.prefetch_end &&
         a->bus.waitcnt == b->bus.waitcnt && !memcmp(&a->io, &b->io, sizeof a->io) &&
         !memcmp(&a->irq, &b->irq, sizeof a->irq) && !memcmp(&a->dma, &b->dma, sizeof a->dma) &&
         !memcmp(&a->timers, &b->timers, sizeof a->timers);
    return ok;
}

static void compare(bool thumb, void (*run_a)(gba_t *), void (*run_b)(gba_t *), int programs) {
    int bad = 0;
    for (int p = 0; p < programs; p++) {
        uint32_t seed = 0x9E3779B9u * (uint32_t)(p + 1) + (thumb ? 7 : 0);
        bool in_rom = p % 2 == 1;
        gba_t *a = machine(seed, thumb, in_rom), *b = machine(seed, thumb, in_rom);
        bool ok = true;
        for (int slice = 0; slice < 8 && ok; slice++) { /* compared every slice: a difference shows near its start */
            run_for(a, 3000, run_a);
            run_for(b, 3000, run_b);
            ok = same(a, b);
        }
        if (!ok && bad++ < 3)
            fprintf(stderr, "  program %d (%s%s): pc %08x/%08x now %llu/%llu cpsr %08x/%08x\n", p, thumb ? "Thumb" : "ARM", in_rom ? ", ROM" : "",
                    a->cpu.r[15], b->cpu.r[15], (unsigned long long)a->now, (unsigned long long)b->now, a->cpu.cpsr,
                    b->cpu.cpsr);
        gba_destroy(a);
        gba_destroy(b);
    }
    CHECK_EQ(bad, 0);
}

TEST(twins_c_loop_matches_cpu_step_arm) {
    compare(false, run_by_steps, cpu_run_generic, 150);
}

TEST(twins_c_loop_matches_cpu_step_thumb) {
    compare(true, run_by_steps, cpu_run_generic, 150);
}

#ifdef GBA_ARM64
TEST(twins_asm_loop_matches_c_loop_arm) {
    compare(false, cpu_run_generic, a64_cpu_run, 400);
}

TEST(twins_asm_loop_matches_c_loop_thumb) {
    compare(true, cpu_run_generic, a64_cpu_run, 400);
}
#endif
```

- [x] **Step 2: Run them to see them fail**

```bash
bash tools/dev.sh unit twins 2>&1 | grep -E "error" | head -3
```

Expected: FAIL to build — `use of undeclared identifier 'cpu_run_generic'`.

- [x] **Step 3: Write the code**

Replace `core/src/cpu/cpu.h`:

```c
#ifndef GBA_CPU_H
#define GBA_CPU_H

#include <stdbool.h>
#include <stdint.h>

typedef struct gba gba_t;

enum {
    MODE_USR = 0x10,
    MODE_FIQ = 0x11,
    MODE_IRQ = 0x12,
    MODE_SVC = 0x13,
    MODE_ABT = 0x17,
    MODE_UND = 0x1B,
    MODE_SYS = 0x1F,
};

#define CPSR_N (1u << 31)
#define CPSR_Z (1u << 30)
#define CPSR_C (1u << 29)
#define CPSR_V (1u << 28)
#define CPSR_I (1u << 7)
#define CPSR_F (1u << 6)
#define CPSR_T (1u << 5)
#define CPSR_MODE 0x1Fu

enum { BANK_USR, BANK_FIQ, BANK_IRQ, BANK_SVC, BANK_ABT, BANK_UND, BANK_COUNT };

/*
 * r[15]: between instructions, the address of the next instruction. While an
 * instruction executes, its address + 8 (ARM) or + 4 (Thumb) — the value the
 * program reads as PC.
 */
typedef struct {
    uint32_t r[16];
    uint32_t cpsr;
    uint32_t spsr[BANK_COUNT]; /* spsr[BANK_USR] is unused */
    uint32_t bank_r13[BANK_COUNT];
    uint32_t bank_r14[BANK_COUNT];
    uint32_t bank_usr_r8_12[5]; /* r8-r12 of every mode except FIQ */
    uint32_t bank_fiq_r8_12[5];
    bool branched;       /* the executing instruction wrote PC */
    bool next_fetch_seq; /* the next fetch continues sequentially */
} arm_cpu_t;

void cpu_init_tables(void); /* builds the ARM and Thumb decode tables; idempotent */
void cpu_reset(gba_t *g);   /* registers zero, SVC mode, IRQ+FIQ masked, ARM state, PC = 0 */
void cpu_step(gba_t *g);    /* take a pending enabled IRQ, then execute one instruction */
/* cpu_step again and again until the next event is due, or the CPU halts or stops: on arm64 the assembly loop
 * (core/arch/arm64), elsewhere (and with GBA_FORCE_GENERIC) its plain-C twin cpu_run_generic. */
void cpu_run(gba_t *g);
void cpu_run_generic(gba_t *g);
#ifdef GBA_ARM64
void a64_cpu_run(gba_t *g);
#endif

int cpu_mode_bank(uint32_t mode);
bool cpu_has_spsr(const arm_cpu_t *c);
void cpu_write_cpsr(arm_cpu_t *c, uint32_t value); /* swaps banked registers on mode change */
void cpu_branch(arm_cpu_t *c, uint32_t target);
void cpu_exception(gba_t *g, uint32_t vector, uint32_t mode, uint32_t return_addr);
static inline bool cpu_cond_passed(uint32_t cpsr, uint32_t cond) {
    bool n = (cpsr & CPSR_N) != 0, z = (cpsr & CPSR_Z) != 0;
    bool cf = (cpsr & CPSR_C) != 0, v = (cpsr & CPSR_V) != 0;
    switch (cond) {
    case 0x0: return z;
    case 0x1: return !z;
    case 0x2: return cf;
    case 0x3: return !cf;
    case 0x4: return n;
    case 0x5: return !n;
    case 0x6: return v;
    case 0x7: return !v;
    case 0x8: return cf && !z;
    case 0x9: return !cf || z;
    case 0xA: return n == v;
    case 0xB: return n != v;
    case 0xC: return !z && n == v;
    case 0xD: return z || n != v;
    case 0xE: return true;
    default: return false; /* NV: never executes on ARMv4 */
    }
}

static inline void cpu_set_nz(arm_cpu_t *c, uint32_t result) {
    c->cpsr = (c->cpsr & ~(CPSR_N | CPSR_Z)) | (result & CPSR_N) | (result == 0 ? CPSR_Z : 0);
}

static inline void cpu_set_c(arm_cpu_t *c, bool on) {
    c->cpsr = on ? c->cpsr | CPSR_C : c->cpsr & ~CPSR_C;
}

static inline void cpu_set_v(arm_cpu_t *c, bool on) {
    c->cpsr = on ? c->cpsr | CPSR_V : c->cpsr & ~CPSR_V;
}

static inline bool cpu_carry(const arm_cpu_t *c) {
    return (c->cpsr & CPSR_C) != 0;
}

static inline bool cpu_overflow(const arm_cpu_t *c) {
    return (c->cpsr & CPSR_V) != 0;
}

/* arm.c */
void arm_init_table(void);
/* What the ARM decode table holds at index i (bits 27-20 << 4 | bits 7-4), for the assembly loop's table. */
enum { ARM_CLASS_OTHER, ARM_CLASS_DP, ARM_CLASS_BRANCH, ARM_CLASS_SINGLE, ARM_CLASS_BLOCK, ARM_CLASS_MUL, ARM_CLASS_HALF,
       ARM_CLASS_BX };
int arm_class(uint32_t i);
void arm_execute(gba_t *g, uint32_t op);
/* thumb.c */
void thumb_init_table(void);
/* The Thumb format at index i (bits 15-6), for the assembly loop's table. */
enum {
    THUMB_CLASS_OTHER, THUMB_CLASS_SHIFT_IMM, THUMB_CLASS_ADD_SUB, THUMB_CLASS_IMM, THUMB_CLASS_ALU, THUMB_CLASS_HI_REG,
    THUMB_CLASS_PC_LOAD, THUMB_CLASS_REG_OFFSET, THUMB_CLASS_REG_OFFSET_SIGNED, THUMB_CLASS_IMM_OFFSET,
    THUMB_CLASS_HALF_IMM, THUMB_CLASS_SP_RELATIVE, THUMB_CLASS_LOAD_ADDRESS, THUMB_CLASS_SP_ADJUST,
    THUMB_CLASS_PUSH_POP, THUMB_CLASS_BLOCK, THUMB_CLASS_COND_BRANCH, THUMB_CLASS_BRANCH, THUMB_CLASS_BL_HIGH,
    THUMB_CLASS_BL_LOW
};
int thumb_class(uint32_t i);
void thumb_execute(gba_t *g, uint16_t op);

#endif
```

Replace `core/src/cpu/cpu.c`:

```c
#include "cpu/cpu.h"

#include <string.h>

#include "gba_internal.h"
#include "mem/bus_inline.h"
#include "cpu/cpu_fast.h"

/* cpu_cond_table[cond] bit n: the condition passes when the flags NZCV are n (built by cpu_init_tables). */
__attribute__((visibility("hidden"))) uint16_t cpu_cond_table[16];

int cpu_mode_bank(uint32_t mode) {
    switch (mode & CPSR_MODE) {
    case MODE_FIQ: return BANK_FIQ;
    case MODE_IRQ: return BANK_IRQ;
    case MODE_SVC: return BANK_SVC;
    case MODE_ABT: return BANK_ABT;
    case MODE_UND: return BANK_UND;
    default: return BANK_USR; /* USR, SYS and invalid mode values */
    }
}

bool cpu_has_spsr(const arm_cpu_t *c) {
    return cpu_mode_bank(c->cpsr) != BANK_USR;
}

static void switch_bank(arm_cpu_t *c, int from, int to) {
    if (from == to) return;
    if (from == BANK_FIQ || to == BANK_FIQ) {
        memcpy(from == BANK_FIQ ? c->bank_fiq_r8_12 : c->bank_usr_r8_12, &c->r[8], sizeof c->bank_usr_r8_12);
        memcpy(&c->r[8], to == BANK_FIQ ? c->bank_fiq_r8_12 : c->bank_usr_r8_12, sizeof c->bank_usr_r8_12);
    }
    c->bank_r13[from] = c->r[13];
    c->bank_r14[from] = c->r[14];
    c->r[13] = c->bank_r13[to];
    c->r[14] = c->bank_r14[to];
}

void cpu_write_cpsr(arm_cpu_t *c, uint32_t value) {
    switch_bank(c, cpu_mode_bank(c->cpsr), cpu_mode_bank(value));
    c->cpsr = value;
}

void cpu_branch(arm_cpu_t *c, uint32_t target) {
    c->r[15] = target;
    c->branched = true;
    c->next_fetch_seq = false;
}

void cpu_exception(gba_t *g, uint32_t vector, uint32_t mode, uint32_t return_addr) {
    arm_cpu_t *c = &g->cpu;
    uint32_t old = c->cpsr;
    cpu_write_cpsr(c, (old & ~(CPSR_MODE | CPSR_T)) | mode | CPSR_I);
    c->spsr[cpu_mode_bank(mode)] = old;
    c->r[14] = return_addr;
    cpu_branch(c, vector);
}

void cpu_init_tables(void) {
    for (uint32_t cond = 0; cond < 16; cond++) {
        cpu_cond_table[cond] = 0;
        for (uint32_t nzcv = 0; nzcv < 16; nzcv++)
            if (cpu_cond_passed(nzcv << 28, cond)) cpu_cond_table[cond] |= (uint16_t)(1u << nzcv);
    }
    arm_init_table();
    thumb_init_table();
}

void cpu_reset(gba_t *g) {
    memset(&g->cpu, 0, sizeof g->cpu);
    g->cpu.cpsr = MODE_SVC | CPSR_I | CPSR_F;
}

static inline void step(gba_t *g) {
    arm_cpu_t *c = &g->cpu;
    /* LR_irq = next instruction + 4, so the handler's "subs pc, lr, #4" resumes it. */
    if (g->irq.pending && irq_line(g) && !(c->cpsr & CPSR_I)) {
        cpu_exception(g, 0x18, MODE_IRQ, c->r[15] + 4);
        g->now += bus_code_cycles(g, 0x18, 4, false) + bus_code_cycles(g, 0x1C, 4, true); /* pipeline refill */
        c->next_fetch_seq = true;
    }

    uint32_t pc = c->r[15];
    c->branched = false;
    g->bus.data_access = false;
    g->bus.prefetch_hint = false;
    g->bus.off_rom = 0;
    if (c->cpsr & CPSR_T) {
        uint16_t op = (uint16_t)bus_fetch16_inline(g, pc, c->next_fetch_seq);
        c->next_fetch_seq = true;
        c->r[15] = pc + 4;
        thumb_execute(g, op);
        if (!c->branched) c->r[15] = pc + 2;
    } else {
        uint32_t op = bus_fetch32_inline(g, pc, c->next_fetch_seq);
        c->next_fetch_seq = true;
        c->r[15] = pc + 8;
        if ((cpu_cond_table[op >> 28] >> (c->cpsr >> 28)) & 1) arm_execute(g, op);
        if (!c->branched) c->r[15] = pc + 4;
    }
    if (c->branched) { /* pipeline refill: N at the target, S after it; the next fetch is then sequential */
        unsigned size = (c->cpsr & CPSR_T) ? 2 : 4;
        g->now += bus_code_cycles(g, c->r[15], size, false) + bus_code_cycles(g, c->r[15] + size, size, true);
        c->next_fetch_seq = true;
        g->bus.prefetch_end = 0; /* a branch empties the prefetch buffer */
    } else if (g->bus.data_access) {
        c->next_fetch_seq = false; /* the bus was used for data (or the multiplier): the next fetch is non-sequential */
        if (g->bus.prefetch_hint) bus_prefetch_stall(g);
    }
}

void cpu_step(gba_t *g) {
    step(g);
}

/* The threaded loop runs what it can; cpu_step takes each instruction it hands back. */
void cpu_run_generic(gba_t *g) {
    while (g->now < sched_next(&g->sched) && !g->irq.halted && !g->irq.stopped) {
        if (g->cpu.cpsr & CPSR_T) thumb_fast_run(g);
        else arm_fast_run(g);
        if (g->now < sched_next(&g->sched) && !g->irq.halted && !g->irq.stopped) step(g);
    }
}

void cpu_run(gba_t *g) {
#ifdef GBA_ARM64
    a64_cpu_run(g);
#else
    cpu_run_generic(g);
#endif
}
```

Create `core/src/cpu/cpu_fast.h`:

```c
#ifndef GBA_CPU_FAST_H
#define GBA_CPU_FAST_H

/*
 * The threaded CPU loop (milestone 7b): each fast handler runs its instruction and jumps straight to the next
 * instruction's handler (a guaranteed tail call), without cpu_step's per-instruction bookkeeping. A fast handler
 * only takes instructions it can finish exactly as cpu_step would; anything else (I/O and save-chip accesses,
 * mode changes, PC loads, an interrupt to take, the next event due, code outside RAM and ROM) leaves the loop
 * before any side effect, with r15 and next_fetch_seq stored, and cpu_run runs that instruction with cpu_step.
 * While the loop runs nothing can change the scheduler, the interrupt flags, a DMA or a halt (only I/O does).
 */
#include "gba_internal.h"
#include "mem/bus_inline.h"

#define FAST_TAIL __attribute__((musttail))

/* cond_table[cond] bit n: the condition passes when the flags NZCV are n (cpu.c). */
extern __attribute__((visibility("hidden"))) uint16_t cpu_cond_table[16];

typedef void arm_fast_t(gba_t *g, uint32_t pc, uint32_t op, bool seq);
typedef void thumb_fast_t(gba_t *g, uint32_t pc, uint32_t op, bool seq);
extern arm_fast_t *arm_fast_table[4096];   /* NULL: run by cpu_step */
extern thumb_fast_t *thumb_fast_table[1024];

void arm_fast_run(gba_t *g);   /* the fast loop from r15 in ARM state; returns when an instruction needs cpu_step */
void thumb_fast_run(gba_t *g); /* the same in Thumb state */

/* Leaves the loop before the instruction at pc: cpu_step continues from here. */
static inline void fast_exit(gba_t *g, uint32_t pc, bool seq) {
    g->cpu.r[15] = pc;
    g->cpu.next_fetch_seq = seq;
}

/* An instruction may start in the loop: the next event is not due and no interrupt is to be taken. */
static inline bool fast_may_start(const gba_t *g) {
    if (g->now >= g->sched.next) return false;
    return !(g->irq.pending && irq_line(g) && !(g->cpu.cpsr & CPSR_I));
}

/* The fetch of an instruction from RAM or ROM, as bus_fetch32 / bus_fetch16 and cpu_step do it. */
static inline void arm_fetched(gba_t *g, uint32_t pc, uint32_t op, bool seq) {
    g->bus.in_bios = false;
    g->now += bus_access_cycles(g, pc & ~3u, 4, seq);
    g->bus.open_bus = op;
    g->cpu.r[15] = pc + 8;
}

static inline void thumb_fetched(gba_t *g, uint32_t pc, uint32_t op, bool seq) {
    g->bus.in_bios = false;
    g->now += bus_access_cycles(g, pc & ~1u, 2, seq);
    g->bus.open_bus = op | op << 16;
    g->cpu.r[15] = pc + 4;
}

/* After a taken branch: the pipeline refill (N at the target, S after it), the prefetch buffer emptied. */
static inline void fast_refill(gba_t *g, uint32_t target, unsigned size) {
    g->now += bus_access_cycles(g, target, size, false) + bus_access_cycles(g, target + size, size, true);
    g->bus.prefetch_end = 0;
}

/* Where a data access at addr (aligned to size) can go straight to memory: EWRAM, IWRAM, the palette, VRAM and OAM
 * (not byte writes there: they have their own rules), and for reads the ROM itself (not past its end, not its first
 * 0x100 bytes, where the GPIO port may answer); NULL for the rest. */
static inline uint8_t *fast_data_ptr(gba_t *g, uint32_t addr, unsigned size, bool write) {
    switch (addr >> 24) {
    case 0x2: return &g->bus.ewram[addr & (EWRAM_SIZE - 1)];
    case 0x3: return &g->bus.iwram[addr & (IWRAM_SIZE - 1)];
    case 0x5: return write && size == 1 ? NULL : &g->bus.pal[addr & (PAL_SIZE - 1)];
    case 0x6: {
        uint32_t off = addr & 0x1FFFF; /* 96 KiB in a 128 KiB window: 0x18000-0x1FFFF mirrors 0x10000-0x17FFF */
        return write && size == 1 ? NULL : &g->bus.vram[off >= 0x18000 ? off - 0x8000 : off];
    }
    case 0x7: return write && size == 1 ? NULL : &g->bus.oam[addr & (OAM_SIZE - 1)];
    case 0x8:
    case 0x9:
    case 0xA:
    case 0xB:
    case 0xC: {
        if (write) return NULL;
        uint32_t off = addr & 0x01FFFFFFu;
        return off >= 0x100 && off + size <= g->cart.rom_size ? g->cart.rom + off : NULL;
    }
    default: return NULL;
    }
}

/* A data access in the loop: its cycles, and the cartridge bus left free when it is not the ROM (bus_read*,
 * bus_write*). The caller has checked that no DMA is due by then (fast_data_ok). */
static inline void fast_data_cycles(gba_t *g, uint32_t addr, unsigned size) {
    uint32_t cyc = bus_access_cycles(g, addr, size, false);
    g->now += cyc;
    if (addr < 0x08000000u) {
        g->bus.off_rom += cyc;
        g->bus.prefetch_hint = true;
    }
}

/* Before the fetch is counted: a data access fetch_cycles later would find no DMA due (bus_read* would run it). */
static inline bool fast_data_ok(const gba_t *g, uint32_t fetch_cycles) {
    return g->now + fetch_cycles < g->dma.due;
}

/* The end of an instruction that made data accesses, as cpu_step ends it: the next fetch is non-sequential, and
 * the prefetch buffer may have used the time the bus was off the cartridge. */
static inline bool fast_after_data(gba_t *g, uint32_t next_pc) {
    g->cpu.next_fetch_seq = false;
    if (g->bus.prefetch_hint) {
        g->cpu.r[15] = next_pc;
        bus_prefetch_stall(g);
    }
    return g->cpu.next_fetch_seq;
}

/* A block of count words from addr (each access aligned down, as bus_read32 / bus_write32 do) in EWRAM or IWRAM,
 * all in one region, with no DMA due before its last access: the region's memory, its address mask and the cycles
 * of each access (EWRAM and IWRAM take as long sequential as not); NULL otherwise. */
static inline uint8_t *fast_block_ptr(gba_t *g, uint32_t addr, uint32_t count, uint32_t fetch_cycles, uint32_t *mask,
                                      uint32_t *cyc) {
    uint32_t a0 = addr & ~3u, region = a0 >> 24;
    if ((region != 0x2 && region != 0x3) || ((a0 + 4 * (count - 1)) >> 24) != region) return NULL;
    uint32_t c = g->bus.cycles[region][1][0];
    if (g->bus.cycles[region][1][1] != c || g->now + fetch_cycles + (uint64_t)(count - 1) * c >= g->dma.due) return NULL;
    *cyc = c;
    *mask = region == 0x2 ? EWRAM_SIZE - 1 : IWRAM_SIZE - 1;
    return region == 0x2 ? g->bus.ewram : g->bus.iwram;
}

/* One word of such a block: its cycles off the cartridge bus. */
static inline uint32_t fast_block_load(gba_t *g, const uint8_t *mem, uint32_t mask, uint32_t cyc, uint32_t addr) {
    g->now += cyc;
    g->bus.off_rom += cyc;
    return bus_load_n(mem + (addr & ~3u & mask), 4);
}

static inline void fast_block_store(gba_t *g, uint8_t *mem, uint32_t mask, uint32_t cyc, uint32_t addr, uint32_t v) {
    g->now += cyc;
    g->bus.off_rom += cyc;
    bus_store32(mem + (addr & ~3u & mask), v);
}

/* Any other instruction, run in the loop exactly as cpu_step runs it: these reset its per-instruction state before
 * the fetch, and finish it after the handler (r15 to the next instruction, the refill after a branch, the
 * non-sequential fetch and prefetch stall after a data access). fast_generic_end is false when the loop must stop:
 * the CPU halted or stopped. */
static inline void fast_generic_begin(gba_t *g) {
    g->cpu.branched = false;
    g->bus.data_access = false;
    g->bus.prefetch_hint = false;
    g->bus.off_rom = 0;
}

static inline bool fast_generic_end(gba_t *g, uint32_t next_pc) {
    arm_cpu_t *c = &g->cpu;
    c->next_fetch_seq = true;
    if (!c->branched) c->r[15] = next_pc;
    if (c->branched) {
        unsigned size = (c->cpsr & CPSR_T) ? 2 : 4;
        g->now += bus_access_cycles(g, c->r[15], size, false) + bus_access_cycles(g, c->r[15] + size, size, true);
        c->next_fetch_seq = true;
        g->bus.prefetch_end = 0;
    } else if (g->bus.data_access) {
        c->next_fetch_seq = false;
        if (g->bus.prefetch_hint) bus_prefetch_stall(g);
    }
    return !g->irq.halted && !g->irq.stopped;
}

/* Starts the per-instruction counters a data access updates (cpu_step zeroes them before every instruction). */
static inline void fast_data_begin(gba_t *g) {
    g->bus.off_rom = 0;
    g->bus.prefetch_hint = false;
}

/* Jumps to the handler of the ARM instruction at pc, or leaves the loop. */
#define ARM_NEXT(g, pc, seq)                                                                   \
    do {                                                                                       \
        uint32_t pc_ = (pc);                                                                   \
        bool seq_ = (seq);                                                                     \
        const uint8_t *p_;                                                                     \
        if (!fast_may_start(g) || !(p_ = bus_code_ptr(g, pc_ & ~3u))) {                        \
            fast_exit(g, pc_, seq_);                                                           \
            return;                                                                            \
        }                                                                                      \
        uint32_t op_ = bus_load_n(p_, 4);                                                      \
        arm_fast_t *f_ = ((cpu_cond_table[op_ >> 28] >> ((g)->cpu.cpsr >> 28)) & 1)            \
                             ? arm_fast_table[((op_ >> 16) & 0xFF0) | ((op_ >> 4) & 0xF)]      \
                             : arm_fast_skip;                                                  \
        if (!f_) {                                                                             \
            fast_exit(g, pc_, seq_);                                                           \
            return;                                                                            \
        }                                                                                      \
        FAST_TAIL return f_(g, pc_, op_, seq_);                                                \
    } while (0)

#define THUMB_NEXT(g, pc, seq)                                                                 \
    do {                                                                                       \
        uint32_t pc_ = (pc);                                                                   \
        bool seq_ = (seq);                                                                     \
        const uint8_t *p_;                                                                     \
        if (!fast_may_start(g) || !(p_ = bus_code_ptr(g, pc_ & ~1u))) {                        \
            fast_exit(g, pc_, seq_);                                                           \
            return;                                                                            \
        }                                                                                      \
        uint32_t op_ = bus_load_n(p_, 2);                                                      \
        thumb_fast_t *f_ = thumb_fast_table[op_ >> 6];                                         \
        if (!f_) {                                                                             \
            fast_exit(g, pc_, seq_);                                                           \
            return;                                                                            \
        }                                                                                      \
        FAST_TAIL return f_(g, pc_, op_, seq_);                                                \
    } while (0)

void arm_fast_skip(gba_t *g, uint32_t pc, uint32_t op, bool seq);    /* an ARM instruction whose condition fails */
void arm_fast_generic(gba_t *g, uint32_t pc, uint32_t op, bool seq); /* any other ARM instruction */
void thumb_fast_generic(gba_t *g, uint32_t pc, uint32_t op, bool seq);

#endif
```

Replace `core/src/cpu/arm.c`:

```c
#include "bios/hle.h"
#include "cpu/alu.h"
#include "cpu/cpu.h"
#include "cpu/cpu_mem.h"
#include "cpu/mul_carry.h"
#include "gba_internal.h"
#include "cpu/cpu_fast.h"

typedef void (*arm_handler_t)(gba_t *g, uint32_t op);

static arm_handler_t arm_table[4096];

#define BIT(op, n) (((op) >> (n)) & 1u)

static void arm_undefined(gba_t *g, uint32_t op) {
    /* The HLE BIOS stub calls into C with a reserved undefined instruction. */
    if (op == HLE_TRAP_OPCODE && g->hle_bios && g->bus.in_bios) {
        hle_swi(g, g->cpu.r[12] & 0xFF);
        return;
    }
    cpu_exception(g, 0x04, MODE_UND, g->cpu.r[15] - 4);
}

/* Operand 2 kinds, from the instruction's bits 25 and 6-4 (a constant in each specialised handler below). */
enum { DP_IMM = 0, DP_SHIFT_IMM = 1 /* + type 0-3 */, DP_SHIFT_REG = 5 /* + type 0-3 */, DP_KINDS = 9 };

/* One data-processing instruction; opcode, s and kind are constants in each specialised copy. */
static inline __attribute__((always_inline)) void data_processing(gba_t *g, uint32_t op, uint32_t opcode, bool s,
                                                                  int kind) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rn = (op >> 16) & 0xF, rd = (op >> 12) & 0xF;
    uint32_t a = c->r[rn];
    bool shift_c = cpu_carry(c);
    uint32_t b;

    if (kind == DP_IMM) {
        uint32_t rot = ((op >> 8) & 0xF) * 2;
        b = ror32(op & 0xFF, rot);
        if (rot) shift_c = (b >> 31) != 0;
    } else if (kind >= DP_SHIFT_REG) {
        /* Register-specified shift: one extra cycle, and PC reads 4 bytes further ahead. */
        uint32_t rm = op & 0xF;
        uint32_t value = c->r[rm] + (rm == 15 ? 4 : 0);
        if (rn == 15) a += 4;
        b = shift_by_reg((uint32_t)(kind - DP_SHIFT_REG), value, c->r[(op >> 8) & 0xF] & 0xFF, &shift_c);
        bus_idle(g, 1);
    } else {
        b = shift_by_imm((uint32_t)(kind - DP_SHIFT_IMM), c->r[op & 0xF], (op >> 7) & 0x1F, &shift_c);
    }

    alu_result_t r = {0, shift_c, cpu_overflow(c)}; /* logical ops: C from shifter, V kept */
    switch (opcode) {
    case 0x0: case 0x8: r.value = a & b; break;        /* AND, TST */
    case 0x1: case 0x9: r.value = a ^ b; break;        /* EOR, TEQ */
    case 0x2: case 0xA: r = alu_add(a, ~b, 1); break;  /* SUB, CMP */
    case 0x3: r = alu_add(b, ~a, 1); break;            /* RSB */
    case 0x4: case 0xB: r = alu_add(a, b, 0); break;   /* ADD, CMN */
    case 0x5: r = alu_add(a, b, cpu_carry(c)); break;  /* ADC */
    case 0x6: r = alu_add(a, ~b, cpu_carry(c)); break; /* SBC */
    case 0x7: r = alu_add(b, ~a, cpu_carry(c)); break; /* RSC */
    case 0xC: r.value = a | b; break;                  /* ORR */
    case 0xD: r.value = b; break;                      /* MOV */
    case 0xE: r.value = a & ~b; break;                 /* BIC */
    default: r.value = ~b; break;                      /* MVN */
    }
    bool is_test = opcode >= 0x8 && opcode <= 0xB;

    if (s && rd == 15) {
        /* S with PC as destination copies SPSR to CPSR (exception return); compares do not branch. */
        if (cpu_has_spsr(c)) cpu_write_cpsr(c, c->spsr[cpu_mode_bank(c->cpsr)]);
    } else if (s) {
        c->cpsr = (c->cpsr & ~(CPSR_N | CPSR_Z | CPSR_C | CPSR_V)) | (r.value & CPSR_N) |
                  (r.value == 0 ? CPSR_Z : 0) | (r.carry ? CPSR_C : 0) | (r.overflow ? CPSR_V : 0);
    }
    if (!is_test) {
        if (rd == 15) cpu_branch(c, r.value & ((c->cpsr & CPSR_T) ? ~1u : ~3u));
        else c->r[rd] = r.value;
    }
}

/* The specialised copies: dp_handlers[opcode][s][kind]. */
#define DP_FN(o, s, k)     static void dp_##o##_##s##_##k(gba_t *g, uint32_t op) { data_processing(g, op, 0x##o, s, k); }
#define DP_FN_KINDS(o, s) DP_FN(o, s, 0) DP_FN(o, s, 1) DP_FN(o, s, 2) DP_FN(o, s, 3) DP_FN(o, s, 4)     DP_FN(o, s, 5) DP_FN(o, s, 6) DP_FN(o, s, 7) DP_FN(o, s, 8)
#define DP_FN_OP(o) DP_FN_KINDS(o, 0) DP_FN_KINDS(o, 1)
DP_FN_OP(0) DP_FN_OP(1) DP_FN_OP(2) DP_FN_OP(3) DP_FN_OP(4) DP_FN_OP(5) DP_FN_OP(6) DP_FN_OP(7)
DP_FN_OP(8) DP_FN_OP(9) DP_FN_OP(A) DP_FN_OP(B) DP_FN_OP(C) DP_FN_OP(D) DP_FN_OP(E) DP_FN_OP(F)
#define DP_ROW(o, s) {dp_##o##_##s##_0, dp_##o##_##s##_1, dp_##o##_##s##_2, dp_##o##_##s##_3, dp_##o##_##s##_4,                       dp_##o##_##s##_5, dp_##o##_##s##_6, dp_##o##_##s##_7, dp_##o##_##s##_8}
#define DP_OP_ROWS(o) {DP_ROW(o, 0), DP_ROW(o, 1)}
static void (*const dp_handlers[16][2][DP_KINDS])(gba_t *, uint32_t) = {
    DP_OP_ROWS(0), DP_OP_ROWS(1), DP_OP_ROWS(2), DP_OP_ROWS(3), DP_OP_ROWS(4), DP_OP_ROWS(5), DP_OP_ROWS(6), DP_OP_ROWS(7),
    DP_OP_ROWS(8), DP_OP_ROWS(9), DP_OP_ROWS(A), DP_OP_ROWS(B), DP_OP_ROWS(C), DP_OP_ROWS(D), DP_OP_ROWS(E), DP_OP_ROWS(F)};

/* hi = instruction bits 27-20, lo = bits 7-4 of a data-processing instruction: its specialised handler. */
static arm_handler_t dp_handler(uint32_t hi, uint32_t lo) {
    int kind = (hi & 0x20) ? DP_IMM : (lo & 1) ? DP_SHIFT_REG + (int)((lo >> 1) & 3) : DP_SHIFT_IMM + (int)((lo >> 1) & 3);
    return dp_handlers[(hi >> 1) & 0xF][hi & 1][kind];
}

static void arm_mrs(gba_t *g, uint32_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rd = (op >> 12) & 0xF;
    if (BIT(op, 22) && cpu_has_spsr(c)) c->r[rd] = c->spsr[cpu_mode_bank(c->cpsr)];
    else c->r[rd] = c->cpsr;
}

static void arm_msr(gba_t *g, uint32_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t value = BIT(op, 25) ? ror32(op & 0xFF, ((op >> 8) & 0xF) * 2) : c->r[op & 0xF];
    uint32_t mask = (BIT(op, 19) ? 0xFF000000u : 0) | (BIT(op, 16) ? 0x000000FFu : 0);
    if (BIT(op, 22)) {
        if (cpu_has_spsr(c)) {
            uint32_t *spsr = &c->spsr[cpu_mode_bank(c->cpsr)];
            *spsr = (*spsr & ~mask) | (value & mask);
        }
        return;
    }
    if ((c->cpsr & CPSR_MODE) == MODE_USR) mask &= 0xFF000000u; /* user mode may only write flags */
    uint32_t next = (c->cpsr & ~mask) | (value & mask);
    next = (next & ~CPSR_T) | (c->cpsr & CPSR_T); /* MSR cannot change the Thumb bit */
    cpu_write_cpsr(c, next);
}

static void arm_branch(gba_t *g, uint32_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t offset = (uint32_t)(((int32_t)(op << 8)) >> 6); /* sign-extended 24-bit word offset */
    if (BIT(op, 24)) c->r[14] = c->r[15] - 4;
    cpu_branch(c, c->r[15] + offset);
}

static void arm_bx(gba_t *g, uint32_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t target = c->r[op & 0xF];
    if (target & 1) {
        c->cpsr |= CPSR_T;
        cpu_branch(c, target & ~1u);
    } else {
        c->cpsr &= ~CPSR_T;
        cpu_branch(c, target & ~3u);
    }
}

static void arm_swi(gba_t *g, uint32_t op) {
    (void)op;
    cpu_exception(g, 0x08, MODE_SVC, g->cpu.r[15] - 4);
}

static void arm_multiply(gba_t *g, uint32_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rd = (op >> 16) & 0xF, rn = (op >> 12) & 0xF, rs = (op >> 8) & 0xF, rm = op & 0xF;
    uint32_t m = c->r[rm], s = c->r[rs], acc = BIT(op, 21) ? c->r[rn] : 0; /* read before rd is written */
    uint32_t result = m * s + acc;
    uint32_t cycles = alu_mul_cycles(s, true) + (BIT(op, 21) ? 1 : 0);
    bus_idle(g, cycles);
    g->bus.data_access = true; /* the next fetch after a multiply is non-sequential, as after a data access */
    g->bus.prefetch_hint = true;
    c->r[rd] = result;
    if (BIT(op, 20)) {
        cpu_set_nz(c, result);
        cpu_set_c(c, mul_carry(MUL_SHORT, m, s, acc)); /* V is left unchanged */
    }
}

static void arm_multiply_long(gba_t *g, uint32_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t hi = (op >> 16) & 0xF, lo = (op >> 12) & 0xF, rs = (op >> 8) & 0xF, rm = op & 0xF;
    bool is_signed = BIT(op, 22);
    uint32_t m = c->r[rm], s = c->r[rs];
    uint64_t acc = BIT(op, 21) ? ((uint64_t)c->r[hi] << 32 | c->r[lo]) : 0;
    uint64_t result = (is_signed ? (uint64_t)((int64_t)(int32_t)m * (int64_t)(int32_t)s) : (uint64_t)m * s) + acc;
    uint32_t cycles = alu_mul_cycles(s, is_signed) + 1 + (BIT(op, 21) ? 1 : 0);
    bus_idle(g, cycles);
    g->bus.data_access = true; /* the next fetch after a multiply is non-sequential, as after a data access */
    g->bus.prefetch_hint = true;
    c->r[lo] = (uint32_t)result;
    c->r[hi] = (uint32_t)(result >> 32);
    if (BIT(op, 20)) {
        c->cpsr = (c->cpsr & ~(CPSR_N | CPSR_Z)) | ((uint32_t)(result >> 32) & CPSR_N) | (result == 0 ? CPSR_Z : 0);
        cpu_set_c(c, mul_carry(is_signed ? MUL_LONG_SIGNED : MUL_LONG_UNSIGNED, m, s, acc));
    }
}

static void arm_single_transfer(gba_t *g, uint32_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rn = (op >> 16) & 0xF, rd = (op >> 12) & 0xF;
    uint32_t offset;
    if (BIT(op, 25)) {
        bool unused_carry = cpu_carry(c); /* RRX reads C; the carry-out is discarded */
        offset = shift_by_imm((op >> 5) & 3, c->r[op & 0xF], (op >> 7) & 0x1F, &unused_carry);
    } else {
        offset = op & 0xFFF;
    }
    uint32_t base = c->r[rn];
    uint32_t moved = BIT(op, 23) ? base + offset : base - offset;
    uint32_t addr = BIT(op, 24) ? moved : base;
    bool writeback = !BIT(op, 24) || BIT(op, 21);

    if (BIT(op, 20)) {
        uint32_t value = BIT(op, 22) ? bus_read8(g, addr, false) : cpu_load_word(g, addr);
        bus_idle(g, 1);
        if (writeback) c->r[rn] = moved; /* a load into the base register wins */
        if (rd == 15) cpu_branch(c, value & ~3u);
        else c->r[rd] = value;
    } else {
        uint32_t value = c->r[rd] + (rd == 15 ? 4 : 0); /* STR PC stores address + 12 */
        if (BIT(op, 22)) bus_write8(g, addr, (uint8_t)value, false);
        else bus_write32(g, addr, value, false);
        if (writeback) c->r[rn] = moved;
    }
}

static void arm_halfword_transfer(gba_t *g, uint32_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rn = (op >> 16) & 0xF, rd = (op >> 12) & 0xF;
    uint32_t offset = BIT(op, 22) ? (((op >> 4) & 0xF0) | (op & 0xF)) : c->r[op & 0xF];
    uint32_t base = c->r[rn];
    uint32_t moved = BIT(op, 23) ? base + offset : base - offset;
    uint32_t addr = BIT(op, 24) ? moved : base;
    bool writeback = !BIT(op, 24) || BIT(op, 21);
    uint32_t sh = (op >> 5) & 3;

    if (BIT(op, 20)) {
        uint32_t value = sh == 1   ? cpu_load_half(g, addr)
                         : sh == 2 ? cpu_load_signed_byte(g, addr)
                                   : cpu_load_signed_half(g, addr);
        bus_idle(g, 1);
        if (writeback) c->r[rn] = moved;
        if (rd == 15) cpu_branch(c, value & ~3u);
        else c->r[rd] = value;
    } else if (sh == 1) {
        uint32_t value = c->r[rd] + (rd == 15 ? 4 : 0);
        bus_write16(g, addr, (uint16_t)value, false);
        if (writeback) c->r[rn] = moved;
    }
    /* sh 2/3 with L = 0 are ARMv5 LDRD/STRD: no effect on the ARM7TDMI. */
}

static void arm_swap(gba_t *g, uint32_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rn = (op >> 16) & 0xF, rd = (op >> 12) & 0xF, rm = op & 0xF;
    uint32_t addr = c->r[rn], source = c->r[rm], loaded;
    if (BIT(op, 22)) {
        loaded = bus_read8(g, addr, false);
        bus_write8(g, addr, (uint8_t)source, false);
    } else {
        loaded = cpu_load_word(g, addr);
        bus_write32(g, addr, source, false);
    }
    bus_idle(g, 1);
    c->r[rd] = loaded;
}

static void arm_block_transfer(gba_t *g, uint32_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rn = (op >> 16) & 0xF;
    uint32_t rlist = op & 0xFFFF;
    bool pre = BIT(op, 24), up = BIT(op, 23), psr = BIT(op, 22), wb = BIT(op, 21), load = BIT(op, 20);

    /* ARMv4 quirk: an empty list transfers only PC but moves the base as if 16 registers were listed. */
    uint32_t count = (uint32_t)__builtin_popcount(rlist);
    if (rlist == 0) {
        rlist = 1u << 15;
        count = 16;
    }

    uint32_t base = c->r[rn];
    uint32_t bytes = count * 4;
    uint32_t new_base = up ? base + bytes : base - bytes;
    uint32_t addr = up ? base + (pre ? 4 : 0) : base - bytes + (pre ? 0 : 4);

    /* The S bit without a PC load transfers the user-mode registers. */
    bool user_bank = psr && !(load && (rlist & 0x8000));
    uint32_t mode = c->cpsr & CPSR_MODE;
    bool seq = false;

    if (load) {
        if (wb) c->r[rn] = new_base; /* a loaded base register overrides the writeback */
        if (user_bank) cpu_write_cpsr(c, (c->cpsr & ~CPSR_MODE) | MODE_USR);
        for (uint32_t i = 0; i < 15; i++) {
            if (!(rlist & (1u << i))) continue;
            c->r[i] = bus_read32(g, addr, seq && (addr & 0x00FFFFFF) != 0);
            seq = true;
            addr += 4;
        }
        if (user_bank) cpu_write_cpsr(c, (c->cpsr & ~CPSR_MODE) | mode);
        bool load_pc = (rlist & 0x8000) != 0;
        uint32_t pc = load_pc ? bus_read32(g, addr, seq && (addr & 0x00FFFFFF) != 0) : 0;
        bus_idle(g, 1);
        if (load_pc) {
            if (psr && cpu_has_spsr(c)) cpu_write_cpsr(c, c->spsr[cpu_mode_bank(c->cpsr)]);
            cpu_branch(c, pc & ((c->cpsr & CPSR_T) ? ~1u : ~3u));
        }
    } else {
        /* A stored base is the original value only when it is the lowest register listed. */
        bool base_first = (rlist & ((1u << rn) - 1)) == 0;
        if (user_bank) cpu_write_cpsr(c, (c->cpsr & ~CPSR_MODE) | MODE_USR);
        for (uint32_t i = 0; i < 16; i++) {
            if (!(rlist & (1u << i))) continue;
            uint32_t value;
            if (i == 15) value = c->r[15] + 4; /* STM PC stores address + 12 */
            else if (i == rn) value = (base_first || !wb) ? base : new_base;
            else value = c->r[i];
            bus_write32(g, addr, value, seq && (addr & 0x00FFFFFF) != 0);
            seq = true;
            addr += 4;
        }
        if (user_bank) cpu_write_cpsr(c, (c->cpsr & ~CPSR_MODE) | mode);
        if (wb) c->r[rn] = new_base;
    }
}

/* hi = instruction bits 27-20, lo = bits 7-4 */
static arm_handler_t arm_classify(uint32_t hi, uint32_t lo) {
    if (hi == 0x12 && lo == 0x1) return arm_bx;
    if ((hi & 0xFC) == 0x00 && lo == 0x9) return arm_multiply;
    if ((hi & 0xF8) == 0x08 && lo == 0x9) return arm_multiply_long;
    if ((hi & 0xFB) == 0x10 && lo == 0x9) return arm_swap;
    if ((hi & 0xE0) == 0x00 && lo == 0x9) return arm_undefined;
    if ((hi & 0xE0) == 0x00 && (lo & 0x9) == 0x9) return arm_halfword_transfer; /* lo = 1011, 1101, 1111 */
    if ((hi & 0xFB) == 0x10 && lo == 0x0) return arm_mrs;
    if ((hi & 0xFB) == 0x12 && lo == 0x0) return arm_msr;
    if ((hi & 0xFB) == 0x32) return arm_msr;
    if ((hi & 0xF9) == 0x10) return arm_undefined; /* TST/TEQ/CMP/CMN space without S */
    if ((hi & 0xFB) == 0x30) return arm_undefined;
    if ((hi & 0xC0) == 0x00) return dp_handler(hi, lo);
    if ((hi & 0xE0) == 0x60 && (lo & 0x1)) return arm_undefined;
    if ((hi & 0xC0) == 0x40) return arm_single_transfer;
    if ((hi & 0xE0) == 0x80) return arm_block_transfer;
    if ((hi & 0xE0) == 0xA0) return arm_branch;
    if ((hi & 0xF0) == 0xF0) return arm_swi;
    return arm_undefined; /* coprocessor space: the GBA has no coprocessors */
}

static void arm_fast_init_table(void);

int arm_class(uint32_t i) {
    arm_handler_t h = arm_table[i];
    if ((i >> 4 & 0xC0) == 0x00 && h == dp_handler(i >> 4, i & 0xF)) return ARM_CLASS_DP;
    if (h == arm_branch) return ARM_CLASS_BRANCH;
    if (h == arm_single_transfer) return ARM_CLASS_SINGLE;
    if (h == arm_block_transfer) return ARM_CLASS_BLOCK;
    if (h == arm_multiply) return ARM_CLASS_MUL;
    if (h == arm_halfword_transfer) return ARM_CLASS_HALF;
    if (h == arm_bx) return ARM_CLASS_BX;
    return ARM_CLASS_OTHER;
}

void arm_init_table(void) {
    for (uint32_t i = 0; i < 4096; i++) arm_table[i] = arm_classify(i >> 4, i & 0xF);
    arm_fast_init_table();
}

void arm_execute(gba_t *g, uint32_t op) {
    arm_table[((op >> 16) & 0xFF0) | ((op >> 4) & 0xF)](g, op);
}

/* ---- The threaded loop's ARM handlers (cpu_fast.h) ---- */

arm_fast_t *arm_fast_table[4096];

void arm_fast_skip(gba_t *g, uint32_t pc, uint32_t op, bool seq) {
    arm_fetched(g, pc, op, seq);
    ARM_NEXT(g, pc + 4, true);
}

void arm_fast_generic(gba_t *g, uint32_t pc, uint32_t op, bool seq) {
    fast_generic_begin(g);
    arm_fetched(g, pc, op, seq);
    arm_execute(g, op);
    if (!fast_generic_end(g, pc + 4) || (g->cpu.cpsr & CPSR_T)) return; /* r15 and next_fetch_seq are stored */
    ARM_NEXT(g, g->cpu.r[15], g->cpu.next_fetch_seq);
}

/* Data processing that does not write PC. */
#define DPF_FN(o, s, k)                                                                     \
    static void dpf_##o##_##s##_##k(gba_t *g, uint32_t pc, uint32_t op, bool seq) {        \
        if (((op >> 12) & 0xF) == 15) FAST_TAIL return arm_fast_generic(g, pc, op, seq);    \
        arm_fetched(g, pc, op, seq);                                                        \
        data_processing(g, op, 0x##o, s, k);                                                \
        ARM_NEXT(g, pc + 4, true);                                                          \
    }
#define DPF_FN_KINDS(o, s) DPF_FN(o, s, 0) DPF_FN(o, s, 1) DPF_FN(o, s, 2) DPF_FN(o, s, 3) DPF_FN(o, s, 4) \
    DPF_FN(o, s, 5) DPF_FN(o, s, 6) DPF_FN(o, s, 7) DPF_FN(o, s, 8)
#define DPF_FN_OP(o) DPF_FN_KINDS(o, 0) DPF_FN_KINDS(o, 1)
DPF_FN_OP(0) DPF_FN_OP(1) DPF_FN_OP(2) DPF_FN_OP(3) DPF_FN_OP(4) DPF_FN_OP(5) DPF_FN_OP(6) DPF_FN_OP(7)
DPF_FN_OP(8) DPF_FN_OP(9) DPF_FN_OP(A) DPF_FN_OP(B) DPF_FN_OP(C) DPF_FN_OP(D) DPF_FN_OP(E) DPF_FN_OP(F)
#define DPF_ROW(o, s) {dpf_##o##_##s##_0, dpf_##o##_##s##_1, dpf_##o##_##s##_2, dpf_##o##_##s##_3, dpf_##o##_##s##_4, \
                       dpf_##o##_##s##_5, dpf_##o##_##s##_6, dpf_##o##_##s##_7, dpf_##o##_##s##_8}
#define DPF_OP_ROWS(o) {DPF_ROW(o, 0), DPF_ROW(o, 1)}
static arm_fast_t *const dpf_handlers[16][2][DP_KINDS] = {
    DPF_OP_ROWS(0), DPF_OP_ROWS(1), DPF_OP_ROWS(2), DPF_OP_ROWS(3), DPF_OP_ROWS(4), DPF_OP_ROWS(5), DPF_OP_ROWS(6),
    DPF_OP_ROWS(7), DPF_OP_ROWS(8), DPF_OP_ROWS(9), DPF_OP_ROWS(A), DPF_OP_ROWS(B), DPF_OP_ROWS(C), DPF_OP_ROWS(D),
    DPF_OP_ROWS(E), DPF_OP_ROWS(F)};

/* B / BL */
static void arm_fast_branch(gba_t *g, uint32_t pc, uint32_t op, bool seq) {
    arm_fetched(g, pc, op, seq);
    uint32_t offset = (uint32_t)(((int32_t)(op << 8)) >> 6);
    if (BIT(op, 24)) g->cpu.r[14] = pc + 4;
    uint32_t target = pc + 8 + offset;
    fast_refill(g, target, 4);
    ARM_NEXT(g, target, true);
}

/* LDR / STR / LDRB / STRB with RAM, or LDR / LDRB from the ROM; not PC. */
static void arm_fast_single(gba_t *g, uint32_t pc, uint32_t op, bool seq) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rn = (op >> 16) & 0xF, rd = (op >> 12) & 0xF;
    c->r[15] = pc + 8; /* the base or the offset register may be PC */
    uint32_t offset;
    if (BIT(op, 25)) {
        bool unused_carry = cpu_carry(c);
        offset = shift_by_imm((op >> 5) & 3, c->r[op & 0xF], (op >> 7) & 0x1F, &unused_carry);
    } else {
        offset = op & 0xFFF;
    }
    uint32_t base = c->r[rn];
    uint32_t moved = BIT(op, 23) ? base + offset : base - offset;
    uint32_t addr = BIT(op, 24) ? moved : base;
    bool writeback = !BIT(op, 24) || BIT(op, 21);
    bool load = BIT(op, 20), byte = BIT(op, 22);
    unsigned size = byte ? 1 : 4;
    uint32_t a = byte ? addr : addr & ~3u;
    uint8_t *p = rd == 15 ? NULL : fast_data_ptr(g, a, size, !load);
    if (!p || !fast_data_ok(g, bus_access_cycles(g, pc & ~3u, 4, seq))) FAST_TAIL return arm_fast_generic(g, pc, op, seq);
    arm_fetched(g, pc, op, seq);
    fast_data_begin(g);
    fast_data_cycles(g, a, size);
    if (load) {
        uint32_t value = byte ? p[0] : ror32(bus_load_n(p, 4), (addr & 3) * 8);
        bus_idle(g, 1);
        if (writeback) c->r[rn] = moved; /* a load into the base register wins */
        c->r[rd] = value;
    } else {
        uint32_t value = c->r[rd];
        if (byte) p[0] = (uint8_t)value;
        else bus_store32(p, value);
        if (writeback) c->r[rn] = moved;
    }
    ARM_NEXT(g, pc + 4, fast_after_data(g, pc + 4));
}

/* LDM / STM with RAM, without the S bit or an empty list. */
static void arm_fast_block(gba_t *g, uint32_t pc, uint32_t op, bool seq) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rn = (op >> 16) & 0xF, rlist = op & 0xFFFF;
    bool pre = BIT(op, 24), up = BIT(op, 23), wb = BIT(op, 21), load = BIT(op, 20);
    if (BIT(op, 22) || rlist == 0 || rn == 15) FAST_TAIL return arm_fast_generic(g, pc, op, seq);
    uint32_t count = (uint32_t)__builtin_popcount(rlist), base = c->r[rn], bytes = count * 4;
    uint32_t new_base = up ? base + bytes : base - bytes;
    uint32_t addr = up ? base + (pre ? 4 : 0) : base - bytes + (pre ? 0 : 4);
    uint32_t mask, cyc;
    uint8_t *mem = fast_block_ptr(g, addr, count, bus_access_cycles(g, pc & ~3u, 4, seq), &mask, &cyc);
    if (!mem) FAST_TAIL return arm_fast_generic(g, pc, op, seq);
    arm_fetched(g, pc, op, seq);
    fast_data_begin(g);
    g->bus.prefetch_hint = true;
    if (load) {
        if (wb) c->r[rn] = new_base; /* a loaded base register overrides the writeback */
        for (uint32_t i = 0; i < 15; i++) {
            if (!(rlist & (1u << i))) continue;
            c->r[i] = fast_block_load(g, mem, mask, cyc, addr);
            addr += 4;
        }
        uint32_t target = (rlist & 0x8000) ? fast_block_load(g, mem, mask, cyc, addr) & ~3u : 0;
        bus_idle(g, 1);
        if (rlist & 0x8000) {
            fast_refill(g, target, 4);
            ARM_NEXT(g, target, true);
        }
    } else {
        bool base_first = (rlist & ((1u << rn) - 1)) == 0; /* a stored base is the original value only then */
        for (uint32_t i = 0; i < 16; i++) {
            if (!(rlist & (1u << i))) continue;
            uint32_t value = i == 15 ? pc + 12 : i == rn ? ((base_first || !wb) ? base : new_base) : c->r[i];
            fast_block_store(g, mem, mask, cyc, addr, value);
            addr += 4;
        }
        if (wb) c->r[rn] = new_base;
    }
    ARM_NEXT(g, pc + 4, fast_after_data(g, pc + 4));
}

static void arm_fast_enter(gba_t *g, uint32_t pc, uint32_t op, bool seq) {
    (void)op;
    ARM_NEXT(g, pc, seq);
}

void arm_fast_run(gba_t *g) {
    arm_fast_enter(g, g->cpu.r[15], 0, g->cpu.next_fetch_seq);
}

static void arm_fast_init_table(void) {
    for (uint32_t i = 0; i < 4096; i++) {
        uint32_t hi = i >> 4, lo = i & 0xF;
        arm_handler_t h = arm_table[i];
        arm_fast_t *f = arm_fast_generic;
        if ((hi & 0xC0) == 0x00 && h == dp_handler(hi, lo)) {
            int kind = (hi & 0x20) ? DP_IMM : (lo & 1) ? DP_SHIFT_REG + (int)((lo >> 1) & 3) : DP_SHIFT_IMM + (int)((lo >> 1) & 3);
            f = dpf_handlers[(hi >> 1) & 0xF][hi & 1][kind];
        } else if (h == arm_branch) {
            f = arm_fast_branch;
        } else if (h == arm_single_transfer) {
            f = arm_fast_single;
        } else if (h == arm_block_transfer) {
            f = arm_fast_block;
        }
        arm_fast_table[i] = f;
    }
}
```

Replace `core/src/cpu/thumb.c`:

```c
#include "cpu/alu.h"
#include "cpu/cpu.h"
#include "cpu/cpu_mem.h"
#include "cpu/mul_carry.h"
#include "gba_internal.h"
#include "cpu/cpu_fast.h"

typedef void (*thumb_handler_t)(gba_t *g, uint16_t op);

static thumb_handler_t thumb_table[1024];

static void set_nzcv(arm_cpu_t *c, alu_result_t r) {
    cpu_set_nz(c, r.value);
    cpu_set_c(c, r.carry);
    cpu_set_v(c, r.overflow);
}

static void thumb_undefined(gba_t *g, uint16_t op) {
    (void)op;
    cpu_exception(g, 0x04, MODE_UND, g->cpu.r[15] - 2);
}

/* THUMB 1: LSL/LSR/ASR Rd, Rs, #imm5 */
static inline __attribute__((always_inline)) void thumb_shift_imm_body(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    bool carry = cpu_carry(c);
    uint32_t result = shift_by_imm((op >> 11) & 3u, c->r[(op >> 3) & 7], (op >> 6) & 0x1Fu, &carry);
    c->r[op & 7] = result;
    cpu_set_nz(c, result);
    cpu_set_c(c, carry);
}

/* THUMB 2: ADD/SUB Rd, Rs, Rn|#imm3 */
static inline __attribute__((always_inline)) void thumb_add_sub_body(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t a = c->r[(op >> 3) & 7];
    uint32_t b = (op & (1u << 10)) ? (op >> 6) & 7u : c->r[(op >> 6) & 7];
    alu_result_t r = (op & (1u << 9)) ? alu_add(a, ~b, 1) : alu_add(a, b, 0);
    c->r[op & 7] = r.value;
    set_nzcv(c, r);
}

/* THUMB 3: MOV/CMP/ADD/SUB Rd, #imm8 */
static inline __attribute__((always_inline)) void thumb_imm_body(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rd = (op >> 8) & 7, imm = op & 0xFFu;
    alu_result_t r;
    switch ((op >> 11) & 3) {
    case 0:
        c->r[rd] = imm;
        cpu_set_nz(c, imm);
        return;
    case 1: set_nzcv(c, alu_add(c->r[rd], ~imm, 1)); return;
    case 2: r = alu_add(c->r[rd], imm, 0); break;
    default: r = alu_add(c->r[rd], ~imm, 1); break;
    }
    c->r[rd] = r.value;
    set_nzcv(c, r);
}

/* THUMB 4: ALU operations on low registers */
static inline __attribute__((always_inline)) void thumb_alu_body(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rd = op & 7;
    uint32_t a = c->r[rd], b = c->r[(op >> 3) & 7];
    bool carry = cpu_carry(c);
    uint32_t result;
    alu_result_t r;
    switch ((op >> 6) & 0xF) {
    case 0x0: result = a & b; break; /* AND */
    case 0x1: result = a ^ b; break; /* EOR */
    case 0x2: result = shift_by_reg(0, a, b & 0xFF, &carry); bus_idle(g, 1); break; /* LSL */
    case 0x3: result = shift_by_reg(1, a, b & 0xFF, &carry); bus_idle(g, 1); break; /* LSR */
    case 0x4: result = shift_by_reg(2, a, b & 0xFF, &carry); bus_idle(g, 1); break; /* ASR */
    case 0x5: r = alu_add(a, b, cpu_carry(c)); c->r[rd] = r.value; set_nzcv(c, r); return;  /* ADC */
    case 0x6: r = alu_add(a, ~b, cpu_carry(c)); c->r[rd] = r.value; set_nzcv(c, r); return; /* SBC */
    case 0x7: result = shift_by_reg(3, a, b & 0xFF, &carry); bus_idle(g, 1); break; /* ROR */
    case 0x8: cpu_set_nz(c, a & b); return;                                        /* TST */
    case 0x9: r = alu_add(0, ~b, 1); c->r[rd] = r.value; set_nzcv(c, r); return;   /* NEG */
    case 0xA: set_nzcv(c, alu_add(a, ~b, 1)); return;                              /* CMP */
    case 0xB: set_nzcv(c, alu_add(a, b, 0)); return;                               /* CMN */
    case 0xC: result = a | b; break;                                               /* ORR */
    case 0xD: /* MUL: Rd = Rs * Rd (Rd is the multiplier); C from the Booth array, V kept */
        result = a * b;
        carry = mul_carry(MUL_SHORT, b, a, 0);
        bus_idle(g, alu_mul_cycles(a, true));
        g->bus.data_access = true;
        g->bus.prefetch_hint = true;
        break;
    case 0xE: result = a & ~b; break;                                              /* BIC */
    default: result = ~b; break;                                                   /* MVN */
    }
    c->r[rd] = result;
    cpu_set_nz(c, result);
    cpu_set_c(c, carry); /* unchanged unless a shift produced a carry-out */
}

/* THUMB 5: ADD/CMP/MOV on high registers, and BX */
static inline __attribute__((always_inline)) void thumb_hi_reg_body(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rd = (op & 7u) | ((op >> 4) & 8u); /* H1 is bit 7 */
    uint32_t value = c->r[(op >> 3) & 0xF];     /* H2 + Rs are bits 6-3 */
    switch ((op >> 8) & 3) {
    case 0: {
        uint32_t result = c->r[rd] + value;
        if (rd == 15) cpu_branch(c, result & ~1u);
        else c->r[rd] = result;
        break;
    }
    case 1: set_nzcv(c, alu_add(c->r[rd], ~value, 1)); break;
    case 2:
        if (rd == 15) cpu_branch(c, value & ~1u);
        else c->r[rd] = value;
        break;
    default:
        if (value & 1) {
            cpu_branch(c, value & ~1u);
        } else {
            c->cpsr &= ~CPSR_T;
            cpu_branch(c, value & ~3u);
        }
        break;
    }
}

/* THUMB 6: LDR Rd, [PC, #imm8*4] — PC is word-aligned first */
static void thumb_pc_load(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t addr = (c->r[15] & ~2u) + (op & 0xFFu) * 4;
    c->r[(op >> 8) & 7] = bus_read32(g, addr, false);
    bus_idle(g, 1);
}

/* THUMB 7: STR/STRB/LDR/LDRB Rd, [Rb, Ro] */
static inline __attribute__((always_inline)) void thumb_reg_offset_body(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rd = op & 7;
    uint32_t addr = c->r[(op >> 3) & 7] + c->r[(op >> 6) & 7];
    switch ((op >> 10) & 3) { /* bit 11 = L, bit 10 = B */
    case 0: bus_write32(g, addr, c->r[rd], false); break;
    case 1: bus_write8(g, addr, (uint8_t)c->r[rd], false); break;
    case 2: c->r[rd] = cpu_load_word(g, addr); bus_idle(g, 1); break;
    default: c->r[rd] = bus_read8(g, addr, false); bus_idle(g, 1); break;
    }
}

/* THUMB 8: STRH/LDSB/LDRH/LDSH Rd, [Rb, Ro] */
static inline __attribute__((always_inline)) void thumb_reg_offset_signed_body(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rd = op & 7;
    uint32_t addr = c->r[(op >> 3) & 7] + c->r[(op >> 6) & 7];
    switch ((op >> 10) & 3) { /* bit 11 = H, bit 10 = S */
    case 0: bus_write16(g, addr, (uint16_t)c->r[rd], false); break;
    case 1: c->r[rd] = cpu_load_signed_byte(g, addr); bus_idle(g, 1); break;
    case 2: c->r[rd] = cpu_load_half(g, addr); bus_idle(g, 1); break;
    default: c->r[rd] = cpu_load_signed_half(g, addr); bus_idle(g, 1); break;
    }
}

/* THUMB 9: STR/LDR/STRB/LDRB Rd, [Rb, #imm5] */
static inline __attribute__((always_inline)) void thumb_imm_offset_body(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rd = op & 7, base = c->r[(op >> 3) & 7], imm = (op >> 6) & 0x1Fu;
    switch ((op >> 11) & 3) { /* bit 12 = B, bit 11 = L */
    case 0: bus_write32(g, base + imm * 4, c->r[rd], false); break;
    case 1: c->r[rd] = cpu_load_word(g, base + imm * 4); bus_idle(g, 1); break;
    case 2: bus_write8(g, base + imm, (uint8_t)c->r[rd], false); break;
    default: c->r[rd] = bus_read8(g, base + imm, false); bus_idle(g, 1); break;
    }
}

/* THUMB 10: STRH/LDRH Rd, [Rb, #imm5*2] */
static inline __attribute__((always_inline)) void thumb_half_imm_body(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rd = op & 7;
    uint32_t addr = c->r[(op >> 3) & 7] + ((op >> 6) & 0x1Fu) * 2;
    if (op & (1u << 11)) {
        c->r[rd] = cpu_load_half(g, addr);
        bus_idle(g, 1);
    } else {
        bus_write16(g, addr, (uint16_t)c->r[rd], false);
    }
}

/* THUMB 11: STR/LDR Rd, [SP, #imm8*4] */
static inline __attribute__((always_inline)) void thumb_sp_relative_body(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rd = (op >> 8) & 7;
    uint32_t addr = c->r[13] + (op & 0xFFu) * 4;
    if (op & (1u << 11)) {
        c->r[rd] = cpu_load_word(g, addr);
        bus_idle(g, 1);
    } else {
        bus_write32(g, addr, c->r[rd], false);
    }
}

/* THUMB 12: ADD Rd, PC|SP, #imm8*4 */
static inline __attribute__((always_inline)) void thumb_load_address_body(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t base = (op & (1u << 11)) ? c->r[13] : (c->r[15] & ~2u);
    c->r[(op >> 8) & 7] = base + (op & 0xFFu) * 4;
}

/* THUMB 13: ADD SP, #±imm7*4 */
static void thumb_sp_adjust(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t offset = (op & 0x7Fu) * 4;
    c->r[13] = (op & 0x80) ? c->r[13] - offset : c->r[13] + offset;
}

/* THUMB 14: PUSH {rlist, LR} / POP {rlist, PC}. SP keeps any misalignment; accesses are aligned. */
static inline __attribute__((always_inline)) void thumb_push_pop_body(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rlist = op & 0xFFu;
    bool extra = (op & (1u << 8)) != 0; /* LR for PUSH, PC for POP */
    uint32_t count = (uint32_t)__builtin_popcount(rlist) + (extra ? 1 : 0);
    bool seq = false;

    if (op & (1u << 11)) { /* POP = LDMIA sp! */
        uint32_t addr = c->r[13];
        if (count == 0) { /* ARMv4 empty-list quirk: loads PC, SP moves by 0x40 */
            uint32_t pc = bus_read32(g, addr, false);
            c->r[13] = addr + 0x40;
            cpu_branch(c, pc & ~1u);
            return;
        }
        for (uint32_t i = 0; i < 8; i++) {
            if (!(rlist & (1u << i))) continue;
            c->r[i] = bus_read32(g, addr, seq && (addr & 0x00FFFFFF) != 0);
            seq = true;
            addr += 4;
        }
        if (extra) {
            uint32_t pc = bus_read32(g, addr, seq && (addr & 0x00FFFFFF) != 0);
            addr += 4;
            cpu_branch(c, pc & ~1u); /* ARMv4: POP {pc} does not change state */
        }
        c->r[13] = addr;
        bus_idle(g, 1);
    } else { /* PUSH = STMDB sp! */
        if (count == 0) {
            c->r[13] -= 0x40;
            bus_write32(g, c->r[13], c->r[15] + 2, false);
            return;
        }
        uint32_t addr = c->r[13] - count * 4;
        c->r[13] = addr;
        for (uint32_t i = 0; i < 8; i++) {
            if (!(rlist & (1u << i))) continue;
            bus_write32(g, addr, c->r[i], seq && (addr & 0x00FFFFFF) != 0);
            seq = true;
            addr += 4;
        }
        if (extra) bus_write32(g, addr, c->r[14], seq && (addr & 0x00FFFFFF) != 0);
    }
}

/* THUMB 15: STMIA/LDMIA Rb!, {rlist} */
static inline __attribute__((always_inline)) void thumb_block_body(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rb = (op >> 8) & 7, rlist = op & 0xFFu;
    uint32_t base = c->r[rb], addr = base;
    bool load = (op & (1u << 11)) != 0;
    bool seq = false;

    if (rlist == 0) { /* ARMv4 quirk: transfers PC, base moves by 0x40 */
        if (load) {
            uint32_t pc = bus_read32(g, addr, false);
            c->r[rb] = base + 0x40;
            cpu_branch(c, pc & ~1u);
        } else {
            bus_write32(g, addr, c->r[15] + 2, false); /* instruction address + 6 */
            c->r[rb] = base + 0x40;
        }
        return;
    }

    uint32_t new_base = base + (uint32_t)__builtin_popcount(rlist) * 4;
    if (load) {
        c->r[rb] = new_base; /* a loaded base register overrides the writeback */
        for (uint32_t i = 0; i < 8; i++) {
            if (!(rlist & (1u << i))) continue;
            c->r[i] = bus_read32(g, addr, seq && (addr & 0x00FFFFFF) != 0);
            seq = true;
            addr += 4;
        }
        bus_idle(g, 1);
    } else {
        bool base_first = (rlist & ((1u << rb) - 1)) == 0;
        for (uint32_t i = 0; i < 8; i++) {
            if (!(rlist & (1u << i))) continue;
            uint32_t value = (i == rb) ? (base_first ? base : new_base) : c->r[i];
            bus_write32(g, addr, value, seq && (addr & 0x00FFFFFF) != 0);
            seq = true;
            addr += 4;
        }
        c->r[rb] = new_base;
    }
}

/* THUMB 16: B<cond> label */
static inline __attribute__((always_inline)) void thumb_cond_branch_body(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    if (!cpu_cond_passed(c->cpsr, (op >> 8) & 0xFu)) return;
    int32_t offset = (int8_t)(op & 0xFF) * 2;
    cpu_branch(c, c->r[15] + (uint32_t)offset);
}

/* THUMB 17: SWI */
static void thumb_swi(gba_t *g, uint16_t op) {
    (void)op;
    cpu_exception(g, 0x08, MODE_SVC, g->cpu.r[15] - 2);
}

/* THUMB 18: B label */
static void thumb_branch(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    int32_t offset = ((int32_t)((uint32_t)op << 21)) >> 20; /* sign-extended offset11 * 2 */
    cpu_branch(c, c->r[15] + (uint32_t)offset);
}

/* THUMB 19, first half: LR = PC + (offset11 << 12) */
static void thumb_bl_high(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    int32_t offset = ((int32_t)((uint32_t)op << 21)) >> 9;
    c->r[14] = c->r[15] + (uint32_t)offset;
}

/* THUMB 19, second half: branch to LR + (offset11 << 1); LR = next instruction | 1 */
static void thumb_bl_low(gba_t *g, uint16_t op) {
    arm_cpu_t *c = &g->cpu;
    uint32_t target = c->r[14] + (op & 0x7FFu) * 2;
    c->r[14] = (c->r[15] - 2) | 1;
    cpu_branch(c, target & ~1u);
}


/* Specialised copies: the variant bits (a constant in each copy) fold the handlers' switches away. */
#define THUMB_COPY(name, mask, value) \
    static void name##_##value(gba_t *g, uint16_t op) { name##_body(g, (uint16_t)((op & ~(mask)) | (value))); }
THUMB_COPY(thumb_shift_imm, 0x1800, 0x0)
THUMB_COPY(thumb_shift_imm, 0x1800, 0x800)
THUMB_COPY(thumb_shift_imm, 0x1800, 0x1000)
THUMB_COPY(thumb_add_sub, 0x0600, 0x0)
THUMB_COPY(thumb_add_sub, 0x0600, 0x200)
THUMB_COPY(thumb_add_sub, 0x0600, 0x400)
THUMB_COPY(thumb_add_sub, 0x0600, 0x600)
THUMB_COPY(thumb_imm, 0x1800, 0x0)
THUMB_COPY(thumb_imm, 0x1800, 0x800)
THUMB_COPY(thumb_imm, 0x1800, 0x1000)
THUMB_COPY(thumb_imm, 0x1800, 0x1800)
THUMB_COPY(thumb_alu, 0x03C0, 0x0)
THUMB_COPY(thumb_alu, 0x03C0, 0x40)
THUMB_COPY(thumb_alu, 0x03C0, 0x80)
THUMB_COPY(thumb_alu, 0x03C0, 0xC0)
THUMB_COPY(thumb_alu, 0x03C0, 0x100)
THUMB_COPY(thumb_alu, 0x03C0, 0x140)
THUMB_COPY(thumb_alu, 0x03C0, 0x180)
THUMB_COPY(thumb_alu, 0x03C0, 0x1C0)
THUMB_COPY(thumb_alu, 0x03C0, 0x200)
THUMB_COPY(thumb_alu, 0x03C0, 0x240)
THUMB_COPY(thumb_alu, 0x03C0, 0x280)
THUMB_COPY(thumb_alu, 0x03C0, 0x2C0)
THUMB_COPY(thumb_alu, 0x03C0, 0x300)
THUMB_COPY(thumb_alu, 0x03C0, 0x340)
THUMB_COPY(thumb_alu, 0x03C0, 0x380)
THUMB_COPY(thumb_alu, 0x03C0, 0x3C0)
THUMB_COPY(thumb_hi_reg, 0x0300, 0x0)
THUMB_COPY(thumb_hi_reg, 0x0300, 0x100)
THUMB_COPY(thumb_hi_reg, 0x0300, 0x200)
THUMB_COPY(thumb_hi_reg, 0x0300, 0x300)
THUMB_COPY(thumb_reg_offset, 0x0C00, 0x0)
THUMB_COPY(thumb_reg_offset, 0x0C00, 0x400)
THUMB_COPY(thumb_reg_offset, 0x0C00, 0x800)
THUMB_COPY(thumb_reg_offset, 0x0C00, 0xC00)
THUMB_COPY(thumb_reg_offset_signed, 0x0C00, 0x0)
THUMB_COPY(thumb_reg_offset_signed, 0x0C00, 0x400)
THUMB_COPY(thumb_reg_offset_signed, 0x0C00, 0x800)
THUMB_COPY(thumb_reg_offset_signed, 0x0C00, 0xC00)
THUMB_COPY(thumb_imm_offset, 0x1800, 0x0)
THUMB_COPY(thumb_imm_offset, 0x1800, 0x800)
THUMB_COPY(thumb_imm_offset, 0x1800, 0x1000)
THUMB_COPY(thumb_imm_offset, 0x1800, 0x1800)
THUMB_COPY(thumb_half_imm, 0x0800, 0x0)
THUMB_COPY(thumb_half_imm, 0x0800, 0x800)
THUMB_COPY(thumb_sp_relative, 0x0800, 0x0)
THUMB_COPY(thumb_sp_relative, 0x0800, 0x800)
THUMB_COPY(thumb_load_address, 0x0800, 0x0)
THUMB_COPY(thumb_load_address, 0x0800, 0x800)
THUMB_COPY(thumb_push_pop, 0x0900, 0x0)
THUMB_COPY(thumb_push_pop, 0x0900, 0x100)
THUMB_COPY(thumb_push_pop, 0x0900, 0x800)
THUMB_COPY(thumb_push_pop, 0x0900, 0x900)
THUMB_COPY(thumb_block, 0x0800, 0x0)
THUMB_COPY(thumb_block, 0x0800, 0x800)
THUMB_COPY(thumb_cond_branch, 0x0F00, 0x0)
THUMB_COPY(thumb_cond_branch, 0x0F00, 0x100)
THUMB_COPY(thumb_cond_branch, 0x0F00, 0x200)
THUMB_COPY(thumb_cond_branch, 0x0F00, 0x300)
THUMB_COPY(thumb_cond_branch, 0x0F00, 0x400)
THUMB_COPY(thumb_cond_branch, 0x0F00, 0x500)
THUMB_COPY(thumb_cond_branch, 0x0F00, 0x600)
THUMB_COPY(thumb_cond_branch, 0x0F00, 0x700)
THUMB_COPY(thumb_cond_branch, 0x0F00, 0x800)
THUMB_COPY(thumb_cond_branch, 0x0F00, 0x900)
THUMB_COPY(thumb_cond_branch, 0x0F00, 0xA00)
THUMB_COPY(thumb_cond_branch, 0x0F00, 0xB00)
THUMB_COPY(thumb_cond_branch, 0x0F00, 0xC00)
THUMB_COPY(thumb_cond_branch, 0x0F00, 0xD00)
THUMB_COPY(thumb_cond_branch, 0x0F00, 0xE00)
THUMB_COPY(thumb_cond_branch, 0x0F00, 0xF00)
static const thumb_handler_t thumb_shift_imm_copies[] = {thumb_shift_imm_0x0, thumb_shift_imm_0x800, thumb_shift_imm_0x1000};
static const thumb_handler_t thumb_add_sub_copies[] = {thumb_add_sub_0x0, thumb_add_sub_0x200, thumb_add_sub_0x400, thumb_add_sub_0x600};
static const thumb_handler_t thumb_imm_copies[] = {thumb_imm_0x0, thumb_imm_0x800, thumb_imm_0x1000, thumb_imm_0x1800};
static const thumb_handler_t thumb_alu_copies[] = {thumb_alu_0x0, thumb_alu_0x40, thumb_alu_0x80, thumb_alu_0xC0, thumb_alu_0x100, thumb_alu_0x140, thumb_alu_0x180, thumb_alu_0x1C0, thumb_alu_0x200, thumb_alu_0x240, thumb_alu_0x280, thumb_alu_0x2C0, thumb_alu_0x300, thumb_alu_0x340, thumb_alu_0x380, thumb_alu_0x3C0};
static const thumb_handler_t thumb_hi_reg_copies[] = {thumb_hi_reg_0x0, thumb_hi_reg_0x100, thumb_hi_reg_0x200, thumb_hi_reg_0x300};
static const thumb_handler_t thumb_reg_offset_copies[] = {thumb_reg_offset_0x0, thumb_reg_offset_0x400, thumb_reg_offset_0x800, thumb_reg_offset_0xC00};
static const thumb_handler_t thumb_reg_offset_signed_copies[] = {thumb_reg_offset_signed_0x0, thumb_reg_offset_signed_0x400, thumb_reg_offset_signed_0x800, thumb_reg_offset_signed_0xC00};
static const thumb_handler_t thumb_imm_offset_copies[] = {thumb_imm_offset_0x0, thumb_imm_offset_0x800, thumb_imm_offset_0x1000, thumb_imm_offset_0x1800};
static const thumb_handler_t thumb_half_imm_copies[] = {thumb_half_imm_0x0, thumb_half_imm_0x800};
static const thumb_handler_t thumb_sp_relative_copies[] = {thumb_sp_relative_0x0, thumb_sp_relative_0x800};
static const thumb_handler_t thumb_load_address_copies[] = {thumb_load_address_0x0, thumb_load_address_0x800};
static const thumb_handler_t thumb_push_pop_copies[] = {thumb_push_pop_0x0, thumb_push_pop_0x100, thumb_push_pop_0x800, thumb_push_pop_0x900};
static const thumb_handler_t thumb_block_copies[] = {thumb_block_0x0, thumb_block_0x800};
static const thumb_handler_t thumb_cond_branch_copies[] = {thumb_cond_branch_0x0, thumb_cond_branch_0x100, thumb_cond_branch_0x200, thumb_cond_branch_0x300, thumb_cond_branch_0x400, thumb_cond_branch_0x500, thumb_cond_branch_0x600, thumb_cond_branch_0x700, thumb_cond_branch_0x800, thumb_cond_branch_0x900, thumb_cond_branch_0xA00, thumb_cond_branch_0xB00, thumb_cond_branch_0xC00, thumb_cond_branch_0xD00, thumb_cond_branch_0xE00, thumb_cond_branch_0xF00};

/* idx = instruction bits 15-6 */
static thumb_handler_t thumb_classify(uint32_t idx) {
    uint32_t op = idx << 6;
    if ((op & 0xF800) == 0x1800) return thumb_add_sub_copies[(op & 0x0600) >> 9];           /* 00011 */
    if ((op & 0xE000) == 0x0000) return thumb_shift_imm_copies[(op & 0x1800) >> 11];         /* 000xx */
    if ((op & 0xE000) == 0x2000) return thumb_imm_copies[(op & 0x1800) >> 11];               /* 001xx */
    if ((op & 0xFC00) == 0x4000) return thumb_alu_copies[(op & 0x03C0) >> 6];               /* 010000 */
    if ((op & 0xFC00) == 0x4400) return thumb_hi_reg_copies[(op & 0x0300) >> 8];            /* 010001 */
    if ((op & 0xF800) == 0x4800) return thumb_pc_load;           /* 01001 */
    if ((op & 0xF200) == 0x5000) return thumb_reg_offset_copies[(op & 0x0C00) >> 10];        /* 0101xx0 */
    if ((op & 0xF200) == 0x5200) return thumb_reg_offset_signed_copies[(op & 0x0C00) >> 10]; /* 0101xx1 */
    if ((op & 0xE000) == 0x6000) return thumb_imm_offset_copies[(op & 0x1800) >> 11];        /* 011xx */
    if ((op & 0xF000) == 0x8000) return thumb_half_imm_copies[(op & 0x0800) >> 11];          /* 1000x */
    if ((op & 0xF000) == 0x9000) return thumb_sp_relative_copies[(op & 0x0800) >> 11];       /* 1001x */
    if ((op & 0xF000) == 0xA000) return thumb_load_address_copies[(op & 0x0800) >> 11];      /* 1010x */
    if ((op & 0xFF00) == 0xB000) return thumb_sp_adjust;         /* 10110000 */
    if ((op & 0xF600) == 0xB400) return thumb_push_pop_copies[((op >> 10) & 2) | ((op >> 8) & 1)];          /* 1011x10x */
    if ((op & 0xF000) == 0xC000) return thumb_block_copies[(op & 0x0800) >> 11];             /* 1100x */
    if ((op & 0xFF00) == 0xDF00) return thumb_swi;               /* 11011111 */
    if ((op & 0xFF00) == 0xDE00) return thumb_undefined;         /* 11011110 */
    if ((op & 0xF000) == 0xD000) return thumb_cond_branch_copies[(op & 0x0F00) >> 8];       /* 1101 */
    if ((op & 0xF800) == 0xE000) return thumb_branch;            /* 11100 */
    if ((op & 0xF800) == 0xF000) return thumb_bl_high;           /* 11110 */
    if ((op & 0xF800) == 0xF800) return thumb_bl_low;            /* 11111 */
    return thumb_undefined; /* 11101 (BLX on ARMv5) and unused 1011 encodings */
}

static void thumb_fast_init_table(void);

int thumb_class(uint32_t i) {
    thumb_handler_t h = thumb_table[i];
    static const struct {
        const thumb_handler_t *copies;
        size_t n;
        int cls;
    } families[] = {
        {thumb_shift_imm_copies, 3, THUMB_CLASS_SHIFT_IMM}, {thumb_add_sub_copies, 4, THUMB_CLASS_ADD_SUB},
        {thumb_imm_copies, 4, THUMB_CLASS_IMM}, {thumb_alu_copies, 16, THUMB_CLASS_ALU},
        {thumb_hi_reg_copies, 4, THUMB_CLASS_HI_REG}, {thumb_reg_offset_copies, 4, THUMB_CLASS_REG_OFFSET},
        {thumb_reg_offset_signed_copies, 4, THUMB_CLASS_REG_OFFSET_SIGNED}, {thumb_imm_offset_copies, 4, THUMB_CLASS_IMM_OFFSET},
        {thumb_half_imm_copies, 2, THUMB_CLASS_HALF_IMM}, {thumb_sp_relative_copies, 2, THUMB_CLASS_SP_RELATIVE},
        {thumb_load_address_copies, 2, THUMB_CLASS_LOAD_ADDRESS}, {thumb_push_pop_copies, 4, THUMB_CLASS_PUSH_POP},
        {thumb_block_copies, 2, THUMB_CLASS_BLOCK}, {thumb_cond_branch_copies, 16, THUMB_CLASS_COND_BRANCH},
    };
    for (size_t f = 0; f < sizeof families / sizeof families[0]; f++)
        for (size_t k = 0; k < families[f].n; k++)
            if (h == families[f].copies[k]) return families[f].cls;
    if (h == thumb_pc_load) return THUMB_CLASS_PC_LOAD;
    if (h == thumb_sp_adjust) return THUMB_CLASS_SP_ADJUST;
    if (h == thumb_branch) return THUMB_CLASS_BRANCH;
    if (h == thumb_bl_high) return THUMB_CLASS_BL_HIGH;
    if (h == thumb_bl_low) return THUMB_CLASS_BL_LOW;
    return THUMB_CLASS_OTHER;
}

void thumb_init_table(void) {
    for (uint32_t i = 0; i < 1024; i++) thumb_table[i] = thumb_classify(i);
    thumb_fast_init_table();
}

void thumb_execute(gba_t *g, uint16_t op) {
    thumb_table[op >> 6](g, op);
}

/* ---- The threaded loop's Thumb handlers (cpu_fast.h) ---- */

thumb_fast_t *thumb_fast_table[1024];

void thumb_fast_generic(gba_t *g, uint32_t pc, uint32_t op, bool seq) {
    fast_generic_begin(g);
    thumb_fetched(g, pc, op, seq);
    thumb_execute(g, (uint16_t)op);
    if (!fast_generic_end(g, pc + 2) || !(g->cpu.cpsr & CPSR_T)) return; /* r15 and next_fetch_seq are stored */
    THUMB_NEXT(g, g->cpu.r[15], g->cpu.next_fetch_seq);
}

/* Instructions that touch neither the bus nor PC: the fetch, the handler, the next instruction. */
#define THUMB_PLAIN(copy)                                                              \
    static void copy##_fast(gba_t *g, uint32_t pc, uint32_t op, bool seq) {           \
        thumb_fetched(g, pc, op, seq);                                                 \
        copy(g, (uint16_t)op);                                                         \
        THUMB_NEXT(g, pc + 2, true);                                                   \
    }
THUMB_PLAIN(thumb_shift_imm_0x0) THUMB_PLAIN(thumb_shift_imm_0x800) THUMB_PLAIN(thumb_shift_imm_0x1000)
THUMB_PLAIN(thumb_add_sub_0x0) THUMB_PLAIN(thumb_add_sub_0x200) THUMB_PLAIN(thumb_add_sub_0x400) THUMB_PLAIN(thumb_add_sub_0x600)
THUMB_PLAIN(thumb_imm_0x0) THUMB_PLAIN(thumb_imm_0x800) THUMB_PLAIN(thumb_imm_0x1000) THUMB_PLAIN(thumb_imm_0x1800)
THUMB_PLAIN(thumb_alu_0x0) THUMB_PLAIN(thumb_alu_0x40) THUMB_PLAIN(thumb_alu_0x80) THUMB_PLAIN(thumb_alu_0xC0)
THUMB_PLAIN(thumb_alu_0x100) THUMB_PLAIN(thumb_alu_0x140) THUMB_PLAIN(thumb_alu_0x180) THUMB_PLAIN(thumb_alu_0x1C0)
THUMB_PLAIN(thumb_alu_0x200) THUMB_PLAIN(thumb_alu_0x240) THUMB_PLAIN(thumb_alu_0x280) THUMB_PLAIN(thumb_alu_0x2C0)
THUMB_PLAIN(thumb_alu_0x300) THUMB_PLAIN(thumb_alu_0x380) THUMB_PLAIN(thumb_alu_0x3C0) /* not 0x340, MUL */
THUMB_PLAIN(thumb_load_address_0x0) THUMB_PLAIN(thumb_load_address_0x800)
THUMB_PLAIN(thumb_sp_adjust) THUMB_PLAIN(thumb_bl_high)

/* ADD / CMP / MOV with high registers, not writing PC. */
#define THUMB_HI(copy)                                                                 \
    static void copy##_fast(gba_t *g, uint32_t pc, uint32_t op, bool seq) {           \
        if (((op >> 8) & 1) == 0 && ((op & 7u) | ((op >> 4) & 8u)) == 15)              \
            FAST_TAIL return thumb_fast_generic(g, pc, op, seq);                       \
        thumb_fetched(g, pc, op, seq);                                                 \
        copy(g, (uint16_t)op);                                                         \
        THUMB_NEXT(g, pc + 2, true);                                                   \
    }
THUMB_HI(thumb_hi_reg_0x0) THUMB_HI(thumb_hi_reg_0x100) THUMB_HI(thumb_hi_reg_0x200)

/* B<cond> */
#define THUMB_COND(cond)                                                                       \
    static void thumb_cond_##cond##_fast(gba_t *g, uint32_t pc, uint32_t op, bool seq) {      \
        thumb_fetched(g, pc, op, seq);                                                         \
        if (!cpu_cond_passed(g->cpu.cpsr, cond)) THUMB_NEXT(g, pc + 2, true);                  \
        uint32_t target = pc + 4 + (uint32_t)((int8_t)(op & 0xFF) * 2);                        \
        fast_refill(g, target, 2);                                                             \
        THUMB_NEXT(g, target, true);                                                           \
    }
THUMB_COND(0) THUMB_COND(1) THUMB_COND(2) THUMB_COND(3) THUMB_COND(4) THUMB_COND(5) THUMB_COND(6)
THUMB_COND(7) THUMB_COND(8) THUMB_COND(9) THUMB_COND(10) THUMB_COND(11) THUMB_COND(12) THUMB_COND(13)

/* B */
static void thumb_branch_fast(gba_t *g, uint32_t pc, uint32_t op, bool seq) {
    thumb_fetched(g, pc, op, seq);
    uint32_t target = pc + 4 + (uint32_t)(((int32_t)(op << 21)) >> 20);
    fast_refill(g, target, 2);
    THUMB_NEXT(g, target, true);
}

/* BL, second half */
static void thumb_bl_low_fast(gba_t *g, uint32_t pc, uint32_t op, bool seq) {
    thumb_fetched(g, pc, op, seq);
    uint32_t target = (g->cpu.r[14] + (op & 0x7FFu) * 2) & ~1u;
    g->cpu.r[14] = (pc + 2) | 1;
    fast_refill(g, target, 2);
    THUMB_NEXT(g, target, true);
}

enum { TM_STR, TM_STRB, TM_STRH, TM_LDR, TM_LDRB, TM_LDRH, TM_LDSB, TM_LDSH };

/* One load or store at addr with RAM (or a load from the ROM), as the handlers above do it with the bus. Returns
 * the next fetch's sequential flag, or -1 before any side effect when the access needs the bus. */
static inline __attribute__((always_inline)) int thumb_fast_mem(gba_t *g, uint32_t pc, uint32_t op, bool seq,
                                                                 uint32_t addr, uint32_t rd, int kind) {
    unsigned size = kind == TM_STR || kind == TM_LDR ? 4 : kind == TM_STRB || kind == TM_LDRB || kind == TM_LDSB ? 1 : 2;
    if (kind == TM_LDSH && (addr & 1)) size = 1; /* misaligned: a signed byte */
    uint32_t a = addr & ~(uint32_t)(size - 1);
    bool store = kind <= TM_STRH;
    uint8_t *p = fast_data_ptr(g, a, size, store);
    if (!p || !fast_data_ok(g, bus_access_cycles(g, pc & ~1u, 2, seq))) return -1; /* needs the bus */
    thumb_fetched(g, pc, op, seq);
    fast_data_begin(g);
    fast_data_cycles(g, a, size);
    uint32_t *r = g->cpu.r;
    switch (kind) {
    case TM_STR: bus_store32(p, r[rd]); break;
    case TM_STRB: p[0] = (uint8_t)r[rd]; break;
    case TM_STRH: bus_store16(p, r[rd]); break;
    case TM_LDR: r[rd] = ror32(bus_load_n(p, 4), (addr & 3) * 8); break;
    case TM_LDRB: r[rd] = p[0]; break;
    case TM_LDRH: r[rd] = ror32(bus_load_n(p, 2), (addr & 1) * 8); break;
    case TM_LDSB: r[rd] = (uint32_t)(int32_t)(int8_t)p[0]; break;
    default: r[rd] = size == 1 ? (uint32_t)(int32_t)(int8_t)p[0] : (uint32_t)(int32_t)(int16_t)bus_load_n(p, 2); break;
    }
    if (!store) bus_idle(g, 1);
    return fast_after_data(g, pc + 2);
}

/* THUMB 6: LDR Rd, [PC, #imm8*4] */
static void thumb_pc_load_fast(gba_t *g, uint32_t pc, uint32_t op, bool seq) {
    int next_seq = thumb_fast_mem(g, pc, op, seq, ((pc + 4) & ~2u) + (op & 0xFFu) * 4, (op >> 8) & 7, TM_LDR);
    if (next_seq < 0) FAST_TAIL return thumb_fast_generic(g, pc, op, seq);
    THUMB_NEXT(g, pc + 2, next_seq);
}

#define THUMB_MEM(name, kind, addr_expr, rd_expr)                                                   \
    static void name##_fast(gba_t *g, uint32_t pc, uint32_t op, bool seq) {                         \
        uint32_t *r = g->cpu.r;                                                                     \
        int next_seq = thumb_fast_mem(g, pc, op, seq, (addr_expr), (rd_expr), kind);                \
        if (next_seq < 0) FAST_TAIL return thumb_fast_generic(g, pc, op, seq);                      \
        THUMB_NEXT(g, pc + 2, next_seq);                                                            \
    }
/* THUMB 7 / 8: [Rb, Ro] */
#define RB_RO (r[(op >> 3) & 7] + r[(op >> 6) & 7])
THUMB_MEM(thumb_reg_offset_0x0, TM_STR, RB_RO, op & 7)
THUMB_MEM(thumb_reg_offset_0x400, TM_STRB, RB_RO, op & 7)
THUMB_MEM(thumb_reg_offset_0x800, TM_LDR, RB_RO, op & 7)
THUMB_MEM(thumb_reg_offset_0xC00, TM_LDRB, RB_RO, op & 7)
THUMB_MEM(thumb_reg_offset_signed_0x0, TM_STRH, RB_RO, op & 7)
THUMB_MEM(thumb_reg_offset_signed_0x400, TM_LDSB, RB_RO, op & 7)
THUMB_MEM(thumb_reg_offset_signed_0x800, TM_LDRH, RB_RO, op & 7)
THUMB_MEM(thumb_reg_offset_signed_0xC00, TM_LDSH, RB_RO, op & 7)
/* THUMB 9 / 10: [Rb, #imm5] */
#define RB_IMM(scale) (r[(op >> 3) & 7] + ((op >> 6) & 0x1Fu) * (scale))
THUMB_MEM(thumb_imm_offset_0x0, TM_STR, RB_IMM(4), op & 7)
THUMB_MEM(thumb_imm_offset_0x800, TM_LDR, RB_IMM(4), op & 7)
THUMB_MEM(thumb_imm_offset_0x1000, TM_STRB, RB_IMM(1), op & 7)
THUMB_MEM(thumb_imm_offset_0x1800, TM_LDRB, RB_IMM(1), op & 7)
THUMB_MEM(thumb_half_imm_0x0, TM_STRH, RB_IMM(2), op & 7)
THUMB_MEM(thumb_half_imm_0x800, TM_LDRH, RB_IMM(2), op & 7)
/* THUMB 11: [SP, #imm8*4] */
THUMB_MEM(thumb_sp_relative_0x0, TM_STR, r[13] + (op & 0xFFu) * 4, (op >> 8) & 7)
THUMB_MEM(thumb_sp_relative_0x800, TM_LDR, r[13] + (op & 0xFFu) * 4, (op >> 8) & 7)

/* PUSH / POP with RAM, not an empty list. */
static void thumb_push_pop_fast_any(gba_t *g, uint32_t pc, uint32_t op, bool seq) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rlist = op & 0xFFu;
    bool extra = (op & (1u << 8)) != 0, pop = (op & (1u << 11)) != 0;
    uint32_t count = (uint32_t)__builtin_popcount(rlist) + (extra ? 1 : 0);
    uint32_t addr = pop ? c->r[13] : c->r[13] - count * 4;
    uint32_t mask, cyc;
    uint8_t *mem = count ? fast_block_ptr(g, addr, count, bus_access_cycles(g, pc & ~1u, 2, seq), &mask, &cyc) : NULL;
    if (!mem) FAST_TAIL return thumb_fast_generic(g, pc, op, seq);
    thumb_fetched(g, pc, op, seq);
    fast_data_begin(g);
    g->bus.prefetch_hint = true;
    if (pop) {
        for (uint32_t i = 0; i < 8; i++) {
            if (!(rlist & (1u << i))) continue;
            c->r[i] = fast_block_load(g, mem, mask, cyc, addr);
            addr += 4;
        }
        uint32_t target = 0;
        if (extra) {
            target = fast_block_load(g, mem, mask, cyc, addr) & ~1u; /* ARMv4: POP {pc} does not change state */
            addr += 4;
        }
        c->r[13] = addr;
        bus_idle(g, 1);
        if (extra) {
            fast_refill(g, target, 2);
            THUMB_NEXT(g, target, true);
        }
    } else {
        c->r[13] = addr;
        for (uint32_t i = 0; i < 8; i++) {
            if (!(rlist & (1u << i))) continue;
            fast_block_store(g, mem, mask, cyc, addr, c->r[i]);
            addr += 4;
        }
        if (extra) fast_block_store(g, mem, mask, cyc, addr, c->r[14]);
    }
    THUMB_NEXT(g, pc + 2, fast_after_data(g, pc + 2));
}

/* STMIA / LDMIA Rb!, {rlist} with RAM, not an empty list. */
static void thumb_block_fast_any(gba_t *g, uint32_t pc, uint32_t op, bool seq) {
    arm_cpu_t *c = &g->cpu;
    uint32_t rb = (op >> 8) & 7, rlist = op & 0xFFu;
    uint32_t base = c->r[rb], addr = base, count = (uint32_t)__builtin_popcount(rlist);
    uint32_t mask, cyc;
    uint8_t *mem = count ? fast_block_ptr(g, addr, count, bus_access_cycles(g, pc & ~1u, 2, seq), &mask, &cyc) : NULL;
    if (!mem) FAST_TAIL return thumb_fast_generic(g, pc, op, seq);
    thumb_fetched(g, pc, op, seq);
    fast_data_begin(g);
    g->bus.prefetch_hint = true;
    uint32_t new_base = base + count * 4;
    if (op & (1u << 11)) {
        c->r[rb] = new_base; /* a loaded base register overrides the writeback */
        for (uint32_t i = 0; i < 8; i++) {
            if (!(rlist & (1u << i))) continue;
            c->r[i] = fast_block_load(g, mem, mask, cyc, addr);
            addr += 4;
        }
        bus_idle(g, 1);
    } else {
        bool base_first = (rlist & ((1u << rb) - 1)) == 0;
        for (uint32_t i = 0; i < 8; i++) {
            if (!(rlist & (1u << i))) continue;
            fast_block_store(g, mem, mask, cyc, addr, (i == rb) ? (base_first ? base : new_base) : c->r[i]);
            addr += 4;
        }
        c->r[rb] = new_base;
    }
    THUMB_NEXT(g, pc + 2, fast_after_data(g, pc + 2));
}

static void thumb_fast_enter(gba_t *g, uint32_t pc, uint32_t op, bool seq) {
    (void)op;
    THUMB_NEXT(g, pc, seq);
}

void thumb_fast_run(gba_t *g) {
    thumb_fast_enter(g, g->cpu.r[15], 0, g->cpu.next_fetch_seq);
}

static void thumb_fast_init_table(void) {
#define PAIR(copy) {copy, copy##_fast}
    static const struct {
        thumb_handler_t slow;
        thumb_fast_t *fast;
    } pairs[] = {
        PAIR(thumb_shift_imm_0x0), PAIR(thumb_shift_imm_0x800), PAIR(thumb_shift_imm_0x1000),
        PAIR(thumb_add_sub_0x0), PAIR(thumb_add_sub_0x200), PAIR(thumb_add_sub_0x400), PAIR(thumb_add_sub_0x600),
        PAIR(thumb_imm_0x0), PAIR(thumb_imm_0x800), PAIR(thumb_imm_0x1000), PAIR(thumb_imm_0x1800),
        PAIR(thumb_alu_0x0), PAIR(thumb_alu_0x40), PAIR(thumb_alu_0x80), PAIR(thumb_alu_0xC0), PAIR(thumb_alu_0x100),
        PAIR(thumb_alu_0x140), PAIR(thumb_alu_0x180), PAIR(thumb_alu_0x1C0), PAIR(thumb_alu_0x200), PAIR(thumb_alu_0x240),
        PAIR(thumb_alu_0x280), PAIR(thumb_alu_0x2C0), PAIR(thumb_alu_0x300), PAIR(thumb_alu_0x380), PAIR(thumb_alu_0x3C0),
        PAIR(thumb_load_address_0x0), PAIR(thumb_load_address_0x800), PAIR(thumb_sp_adjust), PAIR(thumb_bl_high),
        PAIR(thumb_hi_reg_0x0), PAIR(thumb_hi_reg_0x100), PAIR(thumb_hi_reg_0x200),
        {thumb_cond_branch_0x0, thumb_cond_0_fast}, {thumb_cond_branch_0x100, thumb_cond_1_fast},
        {thumb_cond_branch_0x200, thumb_cond_2_fast}, {thumb_cond_branch_0x300, thumb_cond_3_fast},
        {thumb_cond_branch_0x400, thumb_cond_4_fast}, {thumb_cond_branch_0x500, thumb_cond_5_fast},
        {thumb_cond_branch_0x600, thumb_cond_6_fast}, {thumb_cond_branch_0x700, thumb_cond_7_fast},
        {thumb_cond_branch_0x800, thumb_cond_8_fast}, {thumb_cond_branch_0x900, thumb_cond_9_fast},
        {thumb_cond_branch_0xA00, thumb_cond_10_fast}, {thumb_cond_branch_0xB00, thumb_cond_11_fast},
        {thumb_cond_branch_0xC00, thumb_cond_12_fast}, {thumb_cond_branch_0xD00, thumb_cond_13_fast},
        PAIR(thumb_branch), PAIR(thumb_bl_low), PAIR(thumb_pc_load),
        PAIR(thumb_reg_offset_0x0), PAIR(thumb_reg_offset_0x400), PAIR(thumb_reg_offset_0x800), PAIR(thumb_reg_offset_0xC00),
        PAIR(thumb_reg_offset_signed_0x0), PAIR(thumb_reg_offset_signed_0x400), PAIR(thumb_reg_offset_signed_0x800),
        PAIR(thumb_reg_offset_signed_0xC00), PAIR(thumb_imm_offset_0x0), PAIR(thumb_imm_offset_0x800),
        PAIR(thumb_imm_offset_0x1000), PAIR(thumb_imm_offset_0x1800), PAIR(thumb_half_imm_0x0), PAIR(thumb_half_imm_0x800),
        PAIR(thumb_sp_relative_0x0), PAIR(thumb_sp_relative_0x800),
        {thumb_push_pop_0x0, thumb_push_pop_fast_any}, {thumb_push_pop_0x100, thumb_push_pop_fast_any},
        {thumb_push_pop_0x800, thumb_push_pop_fast_any}, {thumb_push_pop_0x900, thumb_push_pop_fast_any},
        {thumb_block_0x0, thumb_block_fast_any}, {thumb_block_0x800, thumb_block_fast_any},
    };
#undef PAIR
    for (uint32_t i = 0; i < 1024; i++) {
        thumb_fast_table[i] = thumb_fast_generic;
        for (size_t k = 0; k < sizeof pairs / sizeof pairs[0]; k++)
            if (thumb_table[i] == pairs[k].slow) thumb_fast_table[i] = pairs[k].fast;
    }
}
```

Create `core/src/mem/bus_inline.h`:

```c
#ifndef GBA_BUS_INLINE_H
#define GBA_BUS_INLINE_H

#include "gba_internal.h"

/* The bus's fast paths, inline for the CPU loop (bus.c holds the general paths). */

static inline uint32_t bus_load_n(const uint8_t *p, unsigned size) {
    switch (size) {
    case 1: return p[0];
    case 2: return (uint32_t)p[0] | (uint32_t)p[1] << 8;
    default: return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
    }
}

static inline void bus_store32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static inline void bus_store16(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static inline uint32_t bus_access_cycles(const gba_t *g, uint32_t addr, unsigned size, bool seq) {
    uint32_t region = addr >> 24;
    if (region > 0xF) return 1;
    return g->bus.cycles[region][size == 4][seq];
}

/* Where an instruction at addr can be read straight from memory: EWRAM, IWRAM and the ROM itself (not past
 * its end, not the GPIO port at 0x080000C4, not region 0xD where an EEPROM may answer); NULL for the rest. */
static inline const uint8_t *bus_code_ptr(const gba_t *g, uint32_t addr) {
    switch (addr >> 24) {
    case 0x2: return &g->bus.ewram[addr & (EWRAM_SIZE - 1)];
    case 0x3: return &g->bus.iwram[addr & (IWRAM_SIZE - 1)];
    case 0x8:
    case 0x9:
    case 0xA:
    case 0xB:
    case 0xC: {
        uint32_t off = addr & 0x01FFFFFFu;
        return off >= 0x100 && off + 4 <= g->cart.rom_size ? g->cart.rom + off : NULL;
    }
    default: return NULL;
    }
}

/* bus_fetch32 / bus_fetch16 with the common case inline: no DMA due, code in RAM or ROM. */
static inline uint32_t bus_fetch32_inline(gba_t *g, uint32_t addr, bool seq) {
    const uint8_t *p;
    if (!dma_bus_due(&g->dma, g->now) && (p = bus_code_ptr(g, addr & ~3u))) {
        g->bus.in_bios = false;
        g->now += bus_access_cycles(g, addr & ~3u, 4, seq);
        return g->bus.open_bus = bus_load_n(p, 4);
    }
    return bus_fetch32(g, addr, seq);
}

static inline uint32_t bus_fetch16_inline(gba_t *g, uint32_t addr, bool seq) {
    const uint8_t *p;
    if (!dma_bus_due(&g->dma, g->now) && (p = bus_code_ptr(g, addr & ~1u))) {
        g->bus.in_bios = false;
        g->now += bus_access_cycles(g, addr & ~1u, 2, seq);
        uint32_t op = bus_load_n(p, 2);
        g->bus.open_bus = op | op << 16;
        return op;
    }
    return bus_fetch16(g, addr, seq);
}

#endif
```

Replace `core/src/mem/bus.c`:

```c
#include "mem/bus.h"

#include <string.h>

#include "gba_internal.h"
#include "mem/bus_inline.h"

static inline uint32_t load_n(const uint8_t *p, unsigned size) {
    switch (size) {
    case 1: return p[0];
    case 2: return (uint32_t)p[0] | (uint32_t)p[1] << 8;
    default: return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
    }
}

static inline void store_n(uint8_t *p, uint32_t v, unsigned size) {
    p[0] = (uint8_t)v;
    if (size >= 2) p[1] = (uint8_t)(v >> 8);
    if (size == 4) {
        p[2] = (uint8_t)(v >> 16);
        p[3] = (uint8_t)(v >> 24);
    }
}

/* The size-byte lane of a 32-bit bus value that an address selects. */
static inline uint32_t lane(uint32_t word, uint32_t addr, unsigned size) {
    switch (size) {
    case 1: return (word >> ((addr & 3) * 8)) & 0xFF;
    case 2: return (word >> ((addr & 2) * 8)) & 0xFFFF;
    default: return word;
    }
}

/* 96 KiB of VRAM in a 128 KiB window: 0x18000-0x1FFFF mirrors 0x10000-0x17FFF. */
static inline uint32_t vram_offset(uint32_t addr) {
    uint32_t off = addr & 0x1FFFF;
    return off >= 0x18000 ? off - 0x8000 : off;
}

/* The save area has an 8-bit bus: accesses of every size keep their exact address. */
static inline bool save_area(uint32_t addr) {
    return (addr >> 24) == 0xE || (addr >> 24) == 0xF;
}

/* A halfword of the prefetch pipeline: real memory only, no side effects or cycles. Anything else
 * (unmapped space, I/O, the save area) falls back to the last fetched opcode, so a PC in unmapped
 * space cannot recurse into open bus. */
static uint32_t pipeline16(gba_t *g, uint32_t addr) {
    bus_t *b = &g->bus;
    addr &= ~1u;
    switch (addr >> 24) {
    case 0x0:
        if (addr < BIOS_SIZE && b->in_bios) return load_n(&b->bios[addr], 2);
        break;
    case 0x2: return load_n(&b->ewram[addr & (EWRAM_SIZE - 1)], 2);
    case 0x3: return load_n(&b->iwram[addr & (IWRAM_SIZE - 1)], 2);
    case 0x5: return load_n(&b->pal[addr & (PAL_SIZE - 1)], 2);
    case 0x6: return load_n(&b->vram[vram_offset(addr)], 2);
    case 0x7: return load_n(&b->oam[addr & (OAM_SIZE - 1)], 2);
    case 0x8:
    case 0x9:
    case 0xA:
    case 0xB:
    case 0xC:
    case 0xD: return cart_rom_read(&g->cart, addr, 2);
    default: break;
    }
    return lane(b->open_bus, addr, 2);
}

uint32_t bus_open_bus(gba_t *g) {
    const arm_cpu_t *c = &g->cpu;
    uint32_t next = c->r[15]; /* while an instruction runs: its address + 8 (ARM) or + 4 (Thumb) */
    if (!(c->cpsr & CPSR_T)) return pipeline16(g, next) | pipeline16(g, next + 2) << 16;
    uint32_t pc = next - 4; /* $, the Thumb instruction being executed */
    switch (pc >> 24) {
    case 0x0: /* BIOS and OAM: 32-bit bus */
    case 0x7:
        return (pc & 2) ? pipeline16(g, pc + 2) | pipeline16(g, pc + 4) << 16
                        : pipeline16(g, pc + 4) | pipeline16(g, pc + 6) << 16;
    case 0x3: /* IWRAM */
        return (pc & 2) ? pipeline16(g, pc + 2) | pipeline16(g, pc + 4) << 16
                        : pipeline16(g, pc + 4) | pipeline16(g, pc + 2) << 16;
    default: { /* EWRAM, palette, VRAM, ROM: 16-bit bus */
        uint32_t half = pipeline16(g, pc + 4);
        return half | half << 16;
    }
    }
}

static uint32_t mem_read(gba_t *g, uint32_t addr, unsigned size) {
    bus_t *b = &g->bus;
    switch (addr >> 24) {
    case 0x0:
        if (addr < BIOS_SIZE) {
            uint32_t word = b->in_bios ? load_n(&b->bios[addr & 0x3FFC], 4) : b->bios_latch;
            return lane(word, addr, size);
        }
        return lane(bus_open_bus(g), addr, size);
    case 0x2: return load_n(&b->ewram[addr & (EWRAM_SIZE - 1)], size);
    case 0x3: return load_n(&b->iwram[addr & (IWRAM_SIZE - 1)], size);
    case 0x4: return io_read(g, addr, size);
    case 0x5: return load_n(&b->pal[addr & (PAL_SIZE - 1)], size);
    case 0x6: return load_n(&b->vram[vram_offset(addr)], size);
    case 0x7: return load_n(&b->oam[addr & (OAM_SIZE - 1)], size);
    case 0x8:
    case 0x9:
    case 0xA:
    case 0xB:
    case 0xC:
    case 0xD:
        if ((addr >> 24) == 0xD && save_eeprom_mapped(g, addr)) return save_eeprom_read(g);
        if (gpio_mapped(g, addr)) return gpio_read(g, addr, size);
        return cart_rom_read(&g->cart, addr, size);
    case 0xE:
    case 0xF: {
        uint32_t byte = save_read8(g, addr); /* wider reads repeat the byte */
        return size == 1 ? byte : size == 2 ? byte * 0x0101u : byte * 0x01010101u;
    }
    default: return lane(bus_open_bus(g), addr, size);
    }
}

static void mem_write(gba_t *g, uint32_t addr, uint32_t v, unsigned size) {
    bus_t *b = &g->bus;
    switch (addr >> 24) {
    case 0x2: store_n(&b->ewram[addr & (EWRAM_SIZE - 1)], v, size); break;
    case 0x3: store_n(&b->iwram[addr & (IWRAM_SIZE - 1)], v, size); break;
    case 0x4: io_write(g, addr, v, size); break;
    case 0x5:
        if (size == 1) store_n(&b->pal[addr & (PAL_SIZE - 2)], (v & 0xFF) * 0x0101u, 2);
        else store_n(&b->pal[addr & (PAL_SIZE - 1)], v, size);
        break;
    case 0x6: {
        uint32_t off = vram_offset(addr);
        if (size == 1) {
            /* Byte writes land as a doubled halfword in BG VRAM and are dropped in OBJ VRAM. */
            uint32_t bg_end = (g->ppu.dispcnt & 7) >= 3 ? 0x14000 : 0x10000;
            if (off < bg_end) store_n(&b->vram[off & ~1u], (v & 0xFF) * 0x0101u, 2);
        } else {
            store_n(&b->vram[off], v, size);
        }
        break;
    }
    case 0x7:
        if (size != 1) store_n(&b->oam[addr & (OAM_SIZE - 1)], v, size); /* byte writes are ignored */
        break;
    case 0x8:
    case 0x9:
    case 0xA:
    case 0xB:
    case 0xC:
    case 0xD:
        if ((addr >> 24) == 0xD && save_eeprom_mapped(g, addr)) save_eeprom_write(g, v); /* bit 0 */
        else if (gpio_mapped(g, addr)) gpio_write(g, addr, v, size);
        break;
    case 0xE:
    case 0xF: save_write8(g, addr, (uint8_t)(v >> (8 * (addr & (size - 1))))); break; /* one byte, by address */
    default: break; /* BIOS, ROM and unmapped space ignore writes */
    }
}

/* Data reads: EWRAM, IWRAM and plain ROM straight from memory, the rest through mem_read. */
static inline uint32_t data_read(gba_t *g, uint32_t addr, unsigned size) {
    switch (addr >> 24) {
    case 0x2: return load_n(&g->bus.ewram[addr & (EWRAM_SIZE - 1)], size);
    case 0x3: return load_n(&g->bus.iwram[addr & (IWRAM_SIZE - 1)], size);
    case 0x8:
    case 0x9:
    case 0xA:
    case 0xB:
    case 0xC: {
        uint32_t off = addr & 0x01FFFFFFu;
        if (off >= 0x100 && off + size <= g->cart.rom_size) return load_n(g->cart.rom + off, size);
        return mem_read(g, addr, size);
    }
    default: return mem_read(g, addr, size);
    }
}

/* Data writes: EWRAM and IWRAM straight to memory, the rest through mem_write. */
static inline void data_write(gba_t *g, uint32_t addr, uint32_t v, unsigned size) {
    switch (addr >> 24) {
    case 0x2: store_n(&g->bus.ewram[addr & (EWRAM_SIZE - 1)], v, size); break;
    case 0x3: store_n(&g->bus.iwram[addr & (IWRAM_SIZE - 1)], v, size); break;
    default: mem_write(g, addr, v, size); break;
    }
}

static inline uint32_t access_cycles(const gba_t *g, uint32_t addr, unsigned size, bool seq) {
    uint32_t region = addr >> 24;
    if (region > 0xF) return 1;
    return g->bus.cycles[region][size == 4][seq];
}

uint32_t bus_read8(gba_t *g, uint32_t addr, bool seq) {
    if (dma_bus_due(&g->dma, g->now)) dma_run_immediate(g);
    g->bus.data_access = true;
    uint32_t cyc = access_cycles(g, addr, 1, seq);
    g->now += cyc;
    if (addr < 0x08000000u) { /* not the cartridge: the prefetch buffer may use the bus meanwhile */
        g->bus.off_rom += cyc;
        g->bus.prefetch_hint = true;
    }
    return data_read(g, addr, 1);
}

uint32_t bus_read16(gba_t *g, uint32_t addr, bool seq) {
    if (dma_bus_due(&g->dma, g->now)) dma_run_immediate(g);
    g->bus.data_access = true;
    if (!save_area(addr)) addr &= ~1u; /* the save area keeps exact addresses */
    uint32_t cyc = access_cycles(g, addr, 2, seq);
    g->now += cyc;
    if (addr < 0x08000000u) { /* not the cartridge: the prefetch buffer may use the bus meanwhile */
        g->bus.off_rom += cyc;
        g->bus.prefetch_hint = true;
    }
    return data_read(g, addr, 2);
}

uint32_t bus_read32(gba_t *g, uint32_t addr, bool seq) {
    if (dma_bus_due(&g->dma, g->now)) dma_run_immediate(g);
    g->bus.data_access = true;
    if (!save_area(addr)) addr &= ~3u; /* the save area keeps exact addresses */
    uint32_t cyc = access_cycles(g, addr, 4, seq);
    g->now += cyc;
    if (addr < 0x08000000u) { /* not the cartridge: the prefetch buffer may use the bus meanwhile */
        g->bus.off_rom += cyc;
        g->bus.prefetch_hint = true;
    }
    return data_read(g, addr, 4);
}

void bus_write8(gba_t *g, uint32_t addr, uint8_t value, bool seq) {
    if (dma_bus_due(&g->dma, g->now)) dma_run_immediate(g);
    g->bus.data_access = true;
    uint32_t cyc = access_cycles(g, addr, 1, seq);
    g->now += cyc;
    if (addr < 0x08000000u) { /* not the cartridge: the prefetch buffer may use the bus meanwhile */
        g->bus.off_rom += cyc;
        g->bus.prefetch_hint = true;
    }
    data_write(g, addr, value, 1);
}

void bus_write16(gba_t *g, uint32_t addr, uint16_t value, bool seq) {
    if (dma_bus_due(&g->dma, g->now)) dma_run_immediate(g);
    g->bus.data_access = true;
    if (!save_area(addr)) addr &= ~1u; /* the save area keeps exact addresses */
    uint32_t cyc = access_cycles(g, addr, 2, seq);
    g->now += cyc;
    if (addr < 0x08000000u) { /* not the cartridge: the prefetch buffer may use the bus meanwhile */
        g->bus.off_rom += cyc;
        g->bus.prefetch_hint = true;
    }
    data_write(g, addr, value, 2);
}

void bus_write32(gba_t *g, uint32_t addr, uint32_t value, bool seq) {
    if (dma_bus_due(&g->dma, g->now)) dma_run_immediate(g);
    g->bus.data_access = true;
    if (!save_area(addr)) addr &= ~3u; /* the save area keeps exact addresses */
    uint32_t cyc = access_cycles(g, addr, 4, seq);
    g->now += cyc;
    if (addr < 0x08000000u) { /* not the cartridge: the prefetch buffer may use the bus meanwhile */
        g->bus.off_rom += cyc;
        g->bus.prefetch_hint = true;
    }
    data_write(g, addr, value, 4);
}

/* Where an instruction at addr can be read straight from memory: EWRAM, IWRAM and the ROM itself (not past
 * its end, not the GPIO port at 0x080000C4, not region 0xD where an EEPROM may answer); NULL for the rest. */
static inline const uint8_t *code_ptr(const gba_t *g, uint32_t addr) {
    switch (addr >> 24) {
    case 0x2: return &g->bus.ewram[addr & (EWRAM_SIZE - 1)];
    case 0x3: return &g->bus.iwram[addr & (IWRAM_SIZE - 1)];
    case 0x8:
    case 0x9:
    case 0xA:
    case 0xB:
    case 0xC: {
        uint32_t off = addr & 0x01FFFFFFu;
        return off >= 0x100 && off + 4 <= g->cart.rom_size ? g->cart.rom + off : NULL;
    }
    default: return NULL;
    }
}

uint32_t bus_fetch32(gba_t *g, uint32_t addr, bool seq) {
    if (dma_bus_due(&g->dma, g->now)) dma_run_immediate(g);
    bus_t *b = &g->bus;
    const uint8_t *p = code_ptr(g, addr & ~3u);
    if (p) {
        b->in_bios = false;
        g->now += access_cycles(g, addr & ~3u, 4, seq);
        return b->open_bus = load_n(p, 4);
    }
    b->in_bios = addr < BIOS_SIZE;
    addr &= ~3u;
    g->now += access_cycles(g, addr, 4, seq);
    uint32_t op = mem_read(g, addr, 4);
    /* The BIOS latch holds the last BIOS word fetched: two instructions ahead in the pipeline. */
    if (b->in_bios) b->bios_latch = load_n(&b->bios[(addr + 8) & 0x3FFC], 4);
    b->open_bus = op;
    return op;
}

uint32_t bus_fetch16(gba_t *g, uint32_t addr, bool seq) {
    if (dma_bus_due(&g->dma, g->now)) dma_run_immediate(g);
    bus_t *b = &g->bus;
    const uint8_t *p = code_ptr(g, addr & ~1u);
    if (p) {
        b->in_bios = false;
        g->now += access_cycles(g, addr & ~1u, 2, seq);
        uint32_t op = load_n(p, 2);
        b->open_bus = op | op << 16;
        return op;
    }
    b->in_bios = addr < BIOS_SIZE;
    addr &= ~1u;
    g->now += access_cycles(g, addr, 2, seq);
    uint32_t op = mem_read(g, addr, 2);
    if (b->in_bios) b->bios_latch = load_n(&b->bios[(addr + 4) & 0x3FFC], 4);
    b->open_bus = op | op << 16;
    return op;
}

void bus_prefetch_stall(gba_t *g) {
    bus_t *b = &g->bus;
    uint32_t pc = g->cpu.r[15];
    uint32_t region = pc >> 24;
    if (!(b->waitcnt & 0x4000) || region < 0x8 || region > 0xD) return;
    int32_t s = (int32_t)b->cycles[region][0][1] - 1; /* the cartridge's sequential wait states (16-bit) */
    int32_t wait = (int32_t)b->off_rom;
    int32_t previous = 0, max_loads = 8;
    uint32_t dist = b->prefetch_end - pc; /* halfwords still buffered from an earlier stall */
    if (dist < 16) {
        previous = (int32_t)(dist >> 1);
        max_loads -= previous;
    }
    int32_t stall = s + 1, loads = 1;
    while (stall < wait && loads < max_loads) {
        stall += s;
        loads++;
    }
    b->prefetch_end = pc + 2u * (uint32_t)(loads + previous - 1);
    int32_t used = stall > wait ? stall : wait; /* the wait cannot be shorter than the prefetch */
    g->now = (uint64_t)((int64_t)g->now + (used - wait) - stall);
    g->cpu.next_fetch_seq = true; /* the next fetch comes out of the buffer: sequential */
}

uint32_t bus_code_cycles(const gba_t *g, uint32_t addr, unsigned size, bool seq) {
    return access_cycles(g, addr, size, seq);
}

void bus_idle(gba_t *g, uint32_t cycles) {
    g->now += cycles;
    g->bus.off_rom += cycles;
}

uint32_t bus_peek(gba_t *g, uint32_t addr, unsigned size) {
    return mem_read(g, addr & ~(uint32_t)(size - 1), size);
}

void bus_poke(gba_t *g, uint32_t addr, uint32_t value, unsigned size) {
    uint32_t region = addr >> 24;
    if (region >= 0x2 && region <= 0x7) mem_write(g, addr & ~(uint32_t)(size - 1), value, size);
}

static void set_region(bus_t *b, int region, uint8_t n16, uint8_t s16, uint8_t n32, uint8_t s32) {
    b->cycles[region][0][0] = n16;
    b->cycles[region][0][1] = s16;
    b->cycles[region][1][0] = n32;
    b->cycles[region][1][1] = s32;
}

void bus_set_waitcnt(gba_t *g, uint16_t value) {
    static const uint8_t nonseq[4] = {4, 3, 2, 8};
    static const uint8_t seq[3][2] = {{2, 1}, {4, 1}, {8, 1}};
    bus_t *b = &g->bus;
    b->waitcnt = value & 0x7FFF;
    for (int ws = 0; ws < 3; ws++) {
        uint8_t n = (uint8_t)(1 + nonseq[(value >> (2 + ws * 3)) & 3]);
        uint8_t s = (uint8_t)(1 + seq[ws][(value >> (4 + ws * 3)) & 1]);
        /* The cartridge bus is 16 bits wide: a 32-bit access is one N plus one S access. */
        set_region(b, 0x8 + ws * 2, n, s, (uint8_t)(n + s), (uint8_t)(2 * s));
        set_region(b, 0x9 + ws * 2, n, s, (uint8_t)(n + s), (uint8_t)(2 * s));
    }
    uint8_t sram = (uint8_t)(1 + nonseq[value & 3]);
    set_region(b, 0xE, sram, sram, sram, sram);
    set_region(b, 0xF, sram, sram, sram, sram);
}

void bus_reset(gba_t *g) {
    bus_t *b = &g->bus;
    memset(b->ewram, 0, sizeof b->ewram);
    memset(b->iwram, 0, sizeof b->iwram);
    memset(b->pal, 0, sizeof b->pal);
    memset(b->vram, 0, sizeof b->vram);
    memset(b->oam, 0, sizeof b->oam);
    set_region(b, 0x0, 1, 1, 1, 1); /* BIOS */
    set_region(b, 0x1, 1, 1, 1, 1);
    set_region(b, 0x2, 3, 3, 6, 6); /* EWRAM: 16-bit bus, 2 wait states */
    set_region(b, 0x3, 1, 1, 1, 1); /* IWRAM */
    set_region(b, 0x4, 1, 1, 1, 1); /* I/O */
    set_region(b, 0x5, 1, 1, 2, 2); /* palette: 16-bit bus */
    set_region(b, 0x6, 1, 1, 2, 2); /* VRAM: 16-bit bus */
    set_region(b, 0x7, 1, 1, 1, 1); /* OAM */
    bus_set_waitcnt(g, 0);
    b->in_bios = false;
    b->bios_latch = 0;
    b->open_bus = 0;
}
```

Replace `core/src/gba.c`:

```c
#include <stdlib.h>
#include <string.h>

#include "bios/hle.h"
#include "cheats/cheats.h"
#include "gba_internal.h"
#include "rewind.h"
#include "sys/keypad.h"

gba_t *gba_create(const gba_config_t *cfg) {
    (void)cfg;
    cpu_init_tables();
    io_init_tables();
    gba_t *g = calloc(1, sizeof(gba_t));
    if (!g) return NULL;
    g->hle_bios = true;
    gba_reset(g);
    return g;
}

void gba_destroy(gba_t *g) {
    if (!g) return;
    cheats_drop(g);
    cart_unload(&g->cart);
    rewind_free(g->rewind);
    free(g);
}

gba_err_t gba_load_bios(gba_t *g, const uint8_t *bios, size_t size) {
    if (size != BIOS_SIZE) return GBA_ERR_BIOS_SIZE;
    memcpy(g->bus.bios, bios, BIOS_SIZE);
    g->hle_bios = false;
    return GBA_OK;
}

gba_err_t gba_load_rom(gba_t *g, const uint8_t *rom, size_t size) {
    cheats_drop(g); /* their ROM patches and addresses belong to the old ROM */
    gba_err_t err = cart_load(&g->cart, rom, size);
    if (err == GBA_OK) {
        hardware_setup(g);
        gba_reset(g);
    }
    return err;
}

void gba_reset(gba_t *g) {
    rewind_clear(g->rewind);
    hardware_reset(g); /* before the cycle count restarts: it re-bases the clock */
    g->now = 0;
    g->frame_done = false;
    g->keyinput = 0x3FF;
    sched_init(&g->sched);
    bus_reset(g);
    io_reset(g);
    irq_reset(g);
    save_reset(&g->cart.save);
    timer_reset(g);
    dma_reset(g);
    sio_reset(g);
    apu_reset(g);
    ppu_reset(g);
    cpu_reset(g);
    if (g->hle_bios) hle_boot(g);
}

void gba_run_events(gba_t *g) {
    uint64_t when;
    int id;
    while ((id = sched_pop_due(&g->sched, g->now, &when)) >= 0) {
        switch (id) {
        case EV_HBLANK: ppu_on_hblank(g, when); break;
        case EV_LINE_END: ppu_on_line_end(g, when); break;
        case EV_TIMER0:
        case EV_TIMER1:
        case EV_TIMER2:
        case EV_TIMER3: timer_on_overflow(g, id - EV_TIMER0, when); break;
        case EV_DMA: dma_run_immediate(g); break;
        case EV_APU_FRAME: apu_on_frame_seq(g, when); break;
        case EV_APU_SAMPLE: apu_on_sample(g, when); break;
        case EV_SIO: sio_on_transfer_done(g); break;
        default: break;
        }
    }
}

static void run_frame(gba_t *g) {
    g->frame_done = false;
    while (!g->frame_done) {
        if (g->irq.stopped) {
            if (!irq_stop_wake(g)) {
                apu_stopped_frame(g); /* nothing inside moves; the frontend still gets its sound */
                return;
            }
            g->irq.stopped = false;
        }
        /* Re-read the next event after every instruction: an I/O write can schedule one sooner
         * (an immediate DMA must run before the CPU continues). */
        while (g->now < sched_next(&g->sched) && !g->irq.stopped) {
            if (g->irq.halted) {
                if (!irq_wake(g)) {
                    uint64_t next = sched_next(&g->sched);
                    if (g->irq.pending && irq_wake_at(g) < next) {
                        g->now = irq_wake_at(g); /* the request reaches the halted CPU: wake then */
                        continue;
                    }
                    g->now = next; /* nothing can happen before the next event */
                    break;
                }
                g->irq.halted = false;
            }
            cpu_run(g);
        }
        gba_run_events(g);
    }
}

void gba_run_frame(gba_t *g) {
    run_frame(g);
    cheats_apply(g); /* VBlank has just started: before the next frame's code, before the snapshot */
    rewind_after_frame(g);
}

void gba_set_keys(gba_t *g, uint16_t pressed) {
    g->keyinput = (uint16_t)(~pressed & 0x3FF);
    keypad_check(g);
}

const uint16_t *gba_framebuffer(const gba_t *g) {
    return g->ppu.framebuffer;
}

size_t gba_audio_read(gba_t *g, int16_t *out, size_t max_frames) {
    return apu_read(g, out, max_frames);
}

const char *gba_rom_title(const gba_t *g) {
    return g->cart.title;
}

uint32_t gba_rom_crc32(const gba_t *g) {
    return g->cart.crc32;
}

const char *gba_rom_code(const gba_t *g) {
    return g->cart.code;
}
```

- [x] **Step 4: Every PC test, the scenes, and the C loop on phone B**

```bash
bash tools/dev.sh unit 2>&1 | tail -1
bash tools/dev.sh test 2>&1 | grep -E "tests passed|tests failed"
bash tools/dev.sh bench --runs 1 --check
```
```bash
timeout 550 env GENERIC=1 bash tools/dev.sh phone-unit 2>&1 | tail -1
GENERIC=1 bash tools/dev.sh phone-bench --runs 3 --check
```

Expected: `412 tests, 0 failed` (+ `twins_c_loop_matches_cpu_step_arm` / `_thumb`); `100% tests passed, 0 tests failed out of 120`; 6 scenes `ok ok`; on phone B (`GBA_FORCE_GENERIC=ON`) `412 tests, 0 failed` and 6 scenes `ok ok`, about game A 2.68×, game B 1.98×, game C 4.06×, game D 2.19×, `m7_demo` 5.04×, `snd1_demo` 14.43× (after 7a: 2.04 / 1.22 / 3.24 / 1.63 / 4.48 / 12.28). The fast phone build does not link yet (`a64_cpu_run` arrives in Task 3). (In the prototype a dropped idle cycle in the Thumb fast loads made the C twin test fail.)

- [x] **Step 5: Commit**

```bash
git add tests/unit/test_cpu_twins.c core/src/cpu/cpu.h core/src/cpu/cpu.c core/src/cpu/cpu_fast.h core/src/cpu/arm.c core/src/cpu/thumb.c core/src/mem/bus_inline.h core/src/mem/bus.c core/src/gba.c docs/PROGRESS.md docs/superpowers/plans/2026-10-03-milestone-7b-asm.md
git commit -m "perf(core): a threaded C CPU loop (cpu_run_generic) with twin tests on random programs" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 3: The ARM64 assembly loop

**Files:**
- Create: `core/arch/arm64/cpu_a64.c` (the loop's state `a64_ctx_t` — offsets checked by `_Static_assert` —, the code window, the loop limit: the next event or an interrupt becoming visible, `a64_slow` → `cpu_step`, the handler tables from `arm_class` / `thumb_class`, `a64_cpu_run`), `core/arch/arm64/cpu_a64.S` (ARM: data processing with immediate and immediate-shift operands, B/BL, LDR/STR(B) by bits 25-20, LDRH/STRH/LDRSB/LDRSH, LDM/STM, MUL/MLA, BX; Thumb: every form except SWI / undefined; the prefetch stall; data in EWRAM, IWRAM, palette / VRAM / OAM, ROM loads; a BX into the other state returns 2)

- [x] **Step 1: See the asm twin tests fail on the phone**

```bash
timeout 550 bash tools/dev.sh phone-unit twins 2>&1 | grep -E "undefined|tests," | head -3
```

Expected: the fast build does not link — `undefined symbol: a64_cpu_run`.

- [x] **Step 2: Write the code**

Create `core/arch/arm64/cpu_a64.c`:

```c
/*
 * The ARM64 assembly CPU loop (cpu_a64.S): its state, and the C it calls. Its plain-C twin is cpu_run_generic
 * (cpu.c, cpu_fast.h); both run the same instructions with the same results and cycles.
 *
 * The loop keeps the guest PC, the cycle count, the sequential-fetch flag and a few pointers in registers. It runs
 * the common instructions itself while their code is in one "window" of RAM or ROM and nothing is due; for anything
 * else it calls a64_slow, which runs that one instruction with cpu_step. While it runs, nothing can change the
 * scheduler, the interrupt flags, a DMA, WAITCNT or a halt: only an I/O access does, and those go to cpu_step.
 */
#include <stddef.h>

#include "gba_internal.h"
#include "cpu/cpu_fast.h"
#include "mem/bus_inline.h"

typedef struct a64_ctx {
    gba_t *g;           /* 0 */
    uint64_t limit;     /* 8: leave the loop at this cycle: the next event, or an interrupt becoming visible */
    uint8_t *win_host;  /* 16: the host address of guest address win_lo */
    uint32_t win_lo;    /* 24: the code window: [win_lo, win_lo + win_len) */
    uint32_t win_len;   /* 28 */
    uint8_t fetch_n;    /* 32: cycles of a non-sequential / sequential code fetch in the window */
    uint8_t fetch_s;    /* 33 */
    uint8_t stall;      /* 34: the window is the cartridge and the prefetch buffer is on */
    uint8_t pad0;
    uint32_t rom_size;  /* 36 */
    uint8_t *ewram;     /* 40 */
    uint8_t *iwram;     /* 48 */
    const uint8_t *rom; /* 56 */
    uint64_t *now;      /* 64 */
    uint16_t waitcnt;   /* 72: WAITCNT when the window was found (its fetch cycles and the stall flag depend on it) */
    uint8_t thumb;      /* 74: the window is for this state's fetch size */
    uint8_t stall_s;    /* 75: with stall: the cartridge's sequential wait states (16-bit), bus_prefetch_stall's s */
    uint8_t *pal;       /* 80 */
    uint8_t *vram;      /* 88 */
    uint8_t *oam;       /* 96 */
} a64_ctx_t;

_Static_assert(offsetof(a64_ctx_t, limit) == 8, "cpu_a64.S");
_Static_assert(offsetof(a64_ctx_t, win_host) == 16, "cpu_a64.S");
_Static_assert(offsetof(a64_ctx_t, win_lo) == 24, "cpu_a64.S");
_Static_assert(offsetof(a64_ctx_t, win_len) == 28, "cpu_a64.S");
_Static_assert(offsetof(a64_ctx_t, fetch_n) == 32, "cpu_a64.S");
_Static_assert(offsetof(a64_ctx_t, fetch_s) == 33, "cpu_a64.S");
_Static_assert(offsetof(a64_ctx_t, stall) == 34, "cpu_a64.S");
_Static_assert(offsetof(a64_ctx_t, rom_size) == 36, "cpu_a64.S");
_Static_assert(offsetof(a64_ctx_t, ewram) == 40, "cpu_a64.S");
_Static_assert(offsetof(a64_ctx_t, iwram) == 48, "cpu_a64.S");
_Static_assert(offsetof(a64_ctx_t, rom) == 56, "cpu_a64.S");
_Static_assert(offsetof(a64_ctx_t, now) == 64, "cpu_a64.S");
_Static_assert(offsetof(a64_ctx_t, stall_s) == 75, "cpu_a64.S");
_Static_assert(offsetof(a64_ctx_t, pal) == 80, "cpu_a64.S");
_Static_assert(offsetof(a64_ctx_t, vram) == 88, "cpu_a64.S");
_Static_assert(offsetof(a64_ctx_t, oam) == 96, "cpu_a64.S");
/* The guest registers: r[0..15], then CPSR; next_fetch_seq further on. */
_Static_assert(offsetof(arm_cpu_t, cpsr) == 64, "cpu_a64.S");
_Static_assert(offsetof(arm_cpu_t, next_fetch_seq) == 181, "cpu_a64.S");
/* The bus fields the loop touches, from &bus.waitcnt. */
#define BUS_OFF(field) (offsetof(bus_t, field) - offsetof(bus_t, waitcnt))
_Static_assert(BUS_OFF(cycles) == 2, "cpu_a64.S");
_Static_assert(BUS_OFF(in_bios) == 66, "cpu_a64.S");
_Static_assert(BUS_OFF(off_rom) == 76, "cpu_a64.S");
_Static_assert(BUS_OFF(prefetch_end) == 80, "cpu_a64.S");
_Static_assert(BUS_OFF(open_bus) == 84, "cpu_a64.S");

/* cpu_a64.S */
/* Return 2 after a BX into the other state (go on there, no cpu_step in between), else 0. */
int a64_run_arm(a64_ctx_t *x, uint32_t *regs, uint8_t *bus);
int a64_run_thumb(a64_ctx_t *x, uint32_t *regs, uint8_t *bus);
extern const uintptr_t a64_thumb_handlers[58];
extern char a64_thumb_slow[];
__attribute__((visibility("hidden"))) uintptr_t a64_thumb_table[1024];
extern const uintptr_t a64_arm_dp_handlers[16][2][5];
extern const uintptr_t a64_arm_single_handlers[64];
extern char a64_arm_slow[], a64_arm_branch[], a64_arm_block[], a64_arm_mul[], a64_arm_half[], a64_arm_bx[];
__attribute__((visibility("hidden"))) uintptr_t a64_arm_table[4096];

/* Where the loop must stop: the next event, or (with an interrupt waiting that the CPU would take) the moment
 * the request becomes visible. The same test as fast_may_start, as one number. */
static uint64_t loop_limit(const gba_t *g) {
    uint64_t limit = g->sched.next;
    const irq_t *q = &g->irq;
    if (q->pending && (q->ime & 1) && !(g->cpu.cpsr & CPSR_I) && q->visible_at < limit) limit = q->visible_at;
    return limit;
}

/* The code window around pc for fetches of size bytes (bus_code_ptr's rules); false when pc is elsewhere. */
int a64_window(a64_ctx_t *x, uint32_t pc, uint32_t size) {
    gba_t *g = x->g;
    uint32_t region = pc >> 24, lo, hi;
    uint8_t *host;
    if (pc & (size - 1)) return 0;
    switch (region) {
    case 0x2:
        lo = pc & ~(EWRAM_SIZE - 1);
        hi = lo + EWRAM_SIZE - 3;
        host = g->bus.ewram;
        break;
    case 0x3:
        lo = pc & ~(IWRAM_SIZE - 1);
        hi = lo + IWRAM_SIZE - 3;
        host = g->bus.iwram;
        break;
    case 0x8:
    case 0x9:
    case 0xA:
    case 0xB:
    case 0xC: {
        uint32_t base = (region & ~1u) << 24, top = (region + 1) << 24;
        if (g->cart.rom_size < 0x104) return 0;
        lo = base + 0x100 > region << 24 ? base + 0x100 : region << 24;
        hi = base + g->cart.rom_size - 3; /* off + 4 <= rom_size */
        if (hi > top || hi < base) hi = top;
        if (hi <= lo || pc < lo || pc >= hi) return 0;
        host = g->cart.rom + (lo - base);
        break;
    }
    default: return 0;
    }
    if (pc < lo || pc >= hi) return 0; /* e.g. Thumb code in RAM's last halfword (bus_code_ptr reads 4 bytes) */
    x->win_lo = lo;
    x->win_len = hi - lo;
    x->win_host = host;
    x->fetch_n = g->bus.cycles[region][size == 4][0];
    x->fetch_s = g->bus.cycles[region][size == 4][1];
    x->stall = region >= 0x8 && (g->bus.waitcnt & 0x4000);
    x->stall_s = (uint8_t)(g->bus.cycles[region][0][1] - 1);
    x->waitcnt = g->bus.waitcnt;
    x->thumb = size == 2;
    return 1;
}

static void prepare(a64_ctx_t *x) {
    x->limit = loop_limit(x->g);
    /* The window stays while it is still right: the same WAITCNT and the same state's fetch size. */
    if (x->g->bus.waitcnt != x->waitcnt || x->thumb != ((x->g->cpu.cpsr & CPSR_T) != 0)) x->win_len = 0;
}

/* The instruction at r15 by cpu_step (the loop has stored r15, now and next_fetch_seq). Returns 1 to go on in
 * the same state, 0 to leave the loop. */
int a64_slow(a64_ctx_t *x) {
    gba_t *g = x->g;
    uint32_t thumb = g->cpu.cpsr & CPSR_T;
    cpu_step(g);
    if (g->irq.halted || g->irq.stopped || (g->cpu.cpsr & CPSR_T) != thumb) return 0;
    prepare(x);
    return 1;
}

/* The prefetch buffer after a data access from cartridge code (bus_prefetch_stall; the loop has stored r15 = the
 * next instruction, now and off_rom). */
void a64_stall(a64_ctx_t *x) {
    bus_prefetch_stall(x->g);
}

static void init_tables(void) {
    static bool done;
    if (done) return;
    for (uint32_t i = 0; i < 4096; i++) {
        uint32_t hi = i >> 4, lo = i & 0xF;
        uintptr_t h = (uintptr_t)a64_arm_slow;
        switch (arm_class(i)) { /* the C decoder's own classes */
        case ARM_CLASS_DP:
            if ((hi & 0x20) || !(lo & 1)) /* an immediate or immediate-shift operand */
                h = a64_arm_dp_handlers[(hi >> 1) & 0xF][hi & 1][(hi & 0x20) ? 0 : 1 + (int)((lo >> 1) & 3)];
            break;
        case ARM_CLASS_BRANCH: h = (uintptr_t)a64_arm_branch; break;
        case ARM_CLASS_SINGLE: h = a64_arm_single_handlers[hi & 0x3F]; break;
        case ARM_CLASS_BLOCK: h = (uintptr_t)a64_arm_block; break;
        case ARM_CLASS_MUL: h = (uintptr_t)a64_arm_mul; break;
        case ARM_CLASS_HALF: h = (uintptr_t)a64_arm_half; break;
        case ARM_CLASS_BX: h = (uintptr_t)a64_arm_bx; break;
        default: break;
        }
        a64_arm_table[i] = h;
    }
    for (uint32_t i = 0; i < 1024; i++) {
        uint32_t op = i << 6;
        int k = -1;
        switch (thumb_class(i)) { /* the asm's order: see a64_thumb_handlers */
        case THUMB_CLASS_SHIFT_IMM: k = 0 + (int)((op & 0x1800) >> 11); break;
        case THUMB_CLASS_ADD_SUB: k = 4 + (int)((op & 0x0600) >> 9); break;
        case THUMB_CLASS_IMM: k = 8 + (int)((op & 0x1800) >> 11); break;
        case THUMB_CLASS_ALU: k = 12 + (int)((op & 0x03C0) >> 6); break;
        case THUMB_CLASS_HI_REG: k = 28 + (int)((op & 0x0300) >> 8); break;
        case THUMB_CLASS_REG_OFFSET: k = 32 + (int)((op & 0x0C00) >> 10); break;
        case THUMB_CLASS_REG_OFFSET_SIGNED: k = 36 + (int)((op & 0x0C00) >> 10); break;
        case THUMB_CLASS_IMM_OFFSET: k = 40 + (int)((op & 0x1800) >> 11); break;
        case THUMB_CLASS_HALF_IMM: k = 44 + (int)((op & 0x0800) >> 11); break;
        case THUMB_CLASS_SP_RELATIVE: k = 46 + (int)((op & 0x0800) >> 11); break;
        case THUMB_CLASS_LOAD_ADDRESS: k = 48 + (int)((op & 0x0800) >> 11); break;
        case THUMB_CLASS_PC_LOAD: k = 50; break;
        case THUMB_CLASS_SP_ADJUST: k = 51; break;
        case THUMB_CLASS_COND_BRANCH: k = 52; break;
        case THUMB_CLASS_BRANCH: k = 53; break;
        case THUMB_CLASS_BL_HIGH: k = 54; break;
        case THUMB_CLASS_BL_LOW: k = 55; break;
        case THUMB_CLASS_PUSH_POP: k = 56; break;
        case THUMB_CLASS_BLOCK: k = 57; break;
        default: break;
        }
        a64_thumb_table[i] = k < 0 ? (uintptr_t)a64_thumb_slow : a64_thumb_handlers[k];
    }
    done = true;
}

void a64_cpu_run(gba_t *g) {
    init_tables();
    a64_ctx_t x = {.g = g, .ewram = g->bus.ewram, .iwram = g->bus.iwram, .rom = g->cart.rom,
                   .rom_size = g->cart.rom_size, .now = &g->now, .pal = g->bus.pal,
                   .vram = g->bus.vram, .oam = g->bus.oam};
    while (g->now < sched_next(&g->sched) && !g->irq.halted && !g->irq.stopped) {
        prepare(&x);
        int r = (g->cpu.cpsr & CPSR_T) ? a64_run_thumb(&x, g->cpu.r, (uint8_t *)&g->bus.waitcnt)
                                       : a64_run_arm(&x, g->cpu.r, (uint8_t *)&g->bus.waitcnt);
        if (r == 2) continue; /* a BX into the other state: on in that loop */
        if (g->now < sched_next(&g->sched) && !g->irq.halted && !g->irq.stopped) cpu_step(g);
    }
}
```

Create `core/arch/arm64/cpu_a64.S`:

```asm
/*
 * The ARM64 assembly CPU loop (milestone 7b); its state and the C it calls are in cpu_a64.c, its plain-C twin is
 * cpu_run_generic (cpu.c, cpu_fast.h). Each handler runs one guest instruction and jumps straight to the next
 * one's handler. Anything a handler does not take goes to a64_arm_slow (cpu_step, in C) before any side effect.
 *
 * Registers while the loop runs (callee-saved, so they survive the calls into C):
 *   x19 ctx (a64_ctx_t)        w20 guest PC (the instruction to run)   x21 now (cycles)     x22 limit
 *   x23 guest registers r0-r15, CPSR at +64                            x24 &bus.waitcnt
 *   x25 the code window's host address    w26 next fetch sequential    w27 window start     w28 window length
 * Within a handler: w10 the opcode. x18 is never used (Android's shadow call stack).
 */

#define CTX_LIMIT 8
#define CTX_WIN_HOST 16
#define CTX_WIN_LO 24
#define CTX_FETCH_N 32
#define CTX_FETCH_S 33
#define CTX_STALL 34
#define CTX_ROM_SIZE 36
#define CTX_EWRAM 40
#define CTX_IWRAM 48
#define CTX_ROM 56
#define CTX_NOW 64
#define CTX_PAL 80
#define CTX_VRAM 88
#define CTX_OAM 96
#define CTX_STALL_S 75
#define R_LR 56
#define R_PC 60
#define R_CPSR 64
#define R_NEXT_SEQ 181
#define BUS_CYCLES 2
#define BUS_IN_BIOS 66
#define BUS_OFF_ROM 76
#define BUS_PREFETCH_END 80
#define BUS_OPEN_BUS 84

    .text
    .hidden cpu_cond_table
    .hidden a64_arm_table
    .hidden a64_thumb_table

/* State between the registers and memory (r15, now, next_fetch_seq; limit and window from ctx). */
.macro STORE_STATE
    str w20, [x23, #R_PC]
    ldr x9, [x19, #CTX_NOW]
    str x21, [x9]
    strb w26, [x23, #R_NEXT_SEQ]
.endm

.macro LOAD_STATE
    ldr w20, [x23, #R_PC]
    ldr x9, [x19, #CTX_NOW]
    ldr x21, [x9]
    ldrb w26, [x23, #R_NEXT_SEQ]
    ldr x22, [x19, #CTX_LIMIT]
    ldr x25, [x19, #CTX_WIN_HOST]
    ldp w27, w28, [x19, #CTX_WIN_LO]
.endm

/* The ARM instruction at w20: limit, window, condition, then its handler. */
.macro ARM_DISPATCH
    cmp x21, x22
    b.hs .Larm_exit
    sub w9, w20, w27
    cmp w9, w28
    b.hs .Larm_window
    ldr w10, [x25, w9, uxtw]
    lsr w13, w10, #28
    cmp w13, #14
    b.eq 99f /* AL: no flags to look at */
    ldr w11, [x23, #R_CPSR]
    adrp x12, cpu_cond_table
    add x12, x12, :lo12:cpu_cond_table
    ldrh w12, [x12, w13, uxtw #1]
    lsr w11, w11, #28
    lsr w12, w12, w11
    tst w12, #1
    b.eq .Larm_skip
99:
    ubfx w11, w10, #20, #8
    ubfx w12, w10, #4, #4
    orr w11, w12, w11, lsl #4
    adrp x12, a64_arm_table
    add x12, x12, :lo12:a64_arm_table
    ldr x12, [x12, w11, uxtw #3]
    br x12
.endm

/* w9 = the cycles of this instruction's fetch. */
.macro FETCH_CYCLES
    ldrb w9, [x19, #CTX_FETCH_N]
    ldrb w11, [x19, #CTX_FETCH_S]
    cmp w26, #0
    csel w9, w11, w9, ne
.endm

/* The fetch counted (w9 its cycles), as bus_fetch32 and cpu_step do it; r15 = PC + 8. */
.macro ARM_FETCHED_W9
    add x21, x21, x9
    strb wzr, [x24, #BUS_IN_BIOS]
    str w10, [x24, #BUS_OPEN_BUS]
    add w9, w20, #8
    str w9, [x23, #R_PC]
.endm

.macro ARM_FETCHED
    FETCH_CYCLES
    ARM_FETCHED_W9
.endm

/* \dst = the cycles of an access to region \reg (0-255): bus.cycles[region][wide][seq], 1 above region 15. */
.macro CYCLES dst, reg, wide, seq, tmp
    mov \dst, #1
    cmp \reg, #15
    b.hi 77f
    add \tmp, x24, \reg, uxtw #2
    ldrb \dst, [\tmp, #(BUS_CYCLES + \wide * 2 + \seq)]
77:
.endm

/* After a taken branch to w20: the refill (N at the target, S after it), the prefetch buffer emptied. */
.macro REFILL size, wide
    lsr w1, w20, #24
    CYCLES w2, w1, \wide, 0, x3
    add w4, w20, #\size
    lsr w4, w4, #24
    CYCLES w5, w4, \wide, 1, x3
    add x21, x21, x2
    add x21, x21, x5
    str wzr, [x24, #BUS_PREFETCH_END]
.endm

/* N and Z from w0; C and V kept. */
.macro FLAGS_NZ
    ldr w7, [x23, #R_CPSR]
    and w7, w7, #0x3fffffff
    and w6, w0, #0x80000000
    orr w7, w7, w6
    cmp w0, #0
    cset w6, eq
    orr w7, w7, w6, lsl #30
    str w7, [x23, #R_CPSR]
.endm

/* bus_prefetch_stall in the loop (the window is the cartridge and the buffer is on): w11 = the cycles the bus was
 * off the cartridge, w20 = the next instruction. Credits the cycles the buffer saves; the next fetch is sequential. */
.macro STALL
    ldrb w9, [x19, #CTX_STALL_S] /* the cartridge's sequential wait states (16-bit) */
    ldr w13, [x24, #BUS_PREFETCH_END]
    sub w13, w13, w20 /* halfwords still buffered from an earlier stall */
    mov w14, #0
    mov w15, #8
    cmp w13, #16
    b.hs 41f
    lsr w14, w13, #1
    sub w15, w15, w14
41:
    add w16, w9, #1 /* stall */
    mov w17, #1 /* loads */
42:
    cmp w16, w11
    b.ge 43f
    cmp w17, w15
    b.ge 43f
    add w16, w16, w9
    add w17, w17, #1
    b 42b
43:
    add w13, w17, w14
    sub w13, w13, #1
    add w13, w20, w13, lsl #1
    str w13, [x24, #BUS_PREFETCH_END]
    cmp w16, w11
    csel w13, w16, w11, gt /* the wait cannot be shorter than the prefetch */
    sub w13, w13, w11
    sub w13, w13, w16
    add x21, x21, w13, sxtw
    mov w26, #1
.endm

/* After a data access (w11 the cycles off the cartridge bus, w12 the data's region; w20 already the next
 * instruction): the next fetch is non-sequential, and cartridge code may stall for the prefetch buffer. */
.macro AFTER_DATA dispatch
    mov w26, #0
    cmp w12, #8
    b.hs 91f
    ldrb w9, [x19, #CTX_STALL]
    cbz w9, 91f
    STALL
91:
    \dispatch
.endm

/* x8 = the host address of the data at w6 (aligned to \size): EWRAM, IWRAM, the palette, VRAM, OAM (not byte
 * stores there), or for a load the ROM itself (not its first 0x100 bytes, not past its end); w12 = its region.
 * Anything else goes to \slow (fast_data_ptr's rules). */
.macro DATA_PTR store, size, slow
    lsr w12, w6, #24
    cmp w12, #3
    b.eq 82f
    cmp w12, #2
    b.eq 81f
    sub w13, w12, #5
    cmp w13, #2
    b.ls 86f
.if \store
    b \slow
.else
    sub w13, w12, #8
    cmp w13, #4
    b.hi \slow
    and w13, w6, #0x1ffffff
    cmp w13, #0x100
    b.lo \slow
    ldr w14, [x19, #CTX_ROM_SIZE]
    add w15, w13, #\size
    cmp w15, w14
    b.hi \slow
    ldr x8, [x19, #CTX_ROM]
    add x8, x8, w13, uxtw
    b 83f
.endif
86: /* the palette, VRAM, OAM */
.if \store && (\size == 1)
    b \slow
.else
    cmp w12, #6
    b.eq 87f
    b.hi 88f
    ldr x8, [x19, #CTX_PAL]
    and w13, w6, #0x3ff
    add x8, x8, w13, uxtw
    b 83f
87: /* 96 KiB in a 128 KiB window: 0x18000-0x1FFFF mirrors 0x10000-0x17FFF */
    ldr x8, [x19, #CTX_VRAM]
    and w13, w6, #0x1ffff
    sub w14, w13, #0x8000
    cmp w13, #0x18, lsl #12
    csel w13, w14, w13, hs
    add x8, x8, w13, uxtw
    b 83f
88:
    ldr x8, [x19, #CTX_OAM]
    and w13, w6, #0x3ff
    add x8, x8, w13, uxtw
    b 83f
.endif
81:
    ldr x8, [x19, #CTX_EWRAM]
    and w13, w6, #0x3ffff
    add x8, x8, w13, uxtw
    b 83f
82:
    ldr x8, [x19, #CTX_IWRAM]
    and w13, w6, #0x7fff
    add x8, x8, w13, uxtw
83:
.endm

/* ---- the loop ---- */

    .global a64_run_arm
    .type a64_run_arm, %function
/* int a64_run_arm(a64_ctx_t *x, uint32_t *regs, uint8_t *bus): the loop in ARM state, from r15. Returns 2 after a
 * BX to Thumb, else 0. */
a64_run_arm:
    stp x29, x30, [sp, #-96]!
    mov x29, sp
    stp x19, x20, [sp, #16]
    stp x21, x22, [sp, #32]
    stp x23, x24, [sp, #48]
    stp x25, x26, [sp, #64]
    stp x27, x28, [sp, #80]
    mov x19, x0
    mov x23, x1
    mov x24, x2
    LOAD_STATE
    ARM_DISPATCH

.Larm_exit:
    STORE_STATE
    mov w0, #0
.Larm_return:
    ldp x19, x20, [sp, #16]
    ldp x21, x22, [sp, #32]
    ldp x23, x24, [sp, #48]
    ldp x25, x26, [sp, #64]
    ldp x27, x28, [sp, #80]
    ldp x29, x30, [sp], #96
    ret

/* BX to Thumb: the state is stored; the caller goes on in the Thumb loop (2: no cpu_step in between). */
.Larm_switch:
    STORE_STATE
    mov w0, #2
    b .Larm_return

/* PC left the code window: find the new one, or run the instruction with cpu_step. */
.Larm_window:
    mov x0, x19
    mov w1, w20
    mov w2, #4
    bl a64_window
    cbz w0, a64_arm_slow
    ldr x25, [x19, #CTX_WIN_HOST]
    ldp w27, w28, [x19, #CTX_WIN_LO]
    ARM_DISPATCH

/* Any instruction the loop does not take (before any side effect): cpu_step runs it. */
    .global a64_arm_slow
    .type a64_arm_slow, %function
a64_arm_slow:
    STORE_STATE
    mov x0, x19
    bl a64_slow
    cbz w0, .Larm_return /* cpu_step has stored everything */
    LOAD_STATE
    ARM_DISPATCH

/* The condition failed: only the fetch. */
.Larm_skip:
    ARM_FETCHED
    add w20, w20, #4
    mov w26, #1
    ARM_DISPATCH

/* ---- data processing: a64_arm_dp_<opcode>_<S>_<kind>, kind 0 immediate, 1-4 LSL/LSR/ASR/ROR by an immediate ---- */

/* w4 = the C flag before the instruction. */
.macro OLD_C
    ldr w4, [x23, #R_CPSR]
    ubfx w4, w4, #29, #1
.endm

/* w3 = w3 shifted by the immediate amount w6 (shift_by_imm's rules: LSL #0 none, LSR/ASR #0 = #32, ROR #0 = RRX);
 * with \needc the carry-out into w4. \kind: 1 LSL, 2 LSR, 3 ASR, 4 ROR. */
.macro SHIFT_IMM kind, needc
.if \kind == 1 /* LSL: #0 = no shift */
  .if \needc
    OLD_C
    cbz w6, 72f
    mov w7, #32
    sub w7, w7, w6
    lsr w4, w3, w7
    and w4, w4, #1
    lsl w3, w3, w6
72:
  .else
    lsl w3, w3, w6
  .endif
.elseif \kind == 2 /* LSR: #0 = #32 */
    cbnz w6, 73f
  .if \needc
    lsr w4, w3, #31
  .endif
    mov w3, #0
    b 74f
73:
  .if \needc
    sub w7, w6, #1
    lsr w4, w3, w7
    and w4, w4, #1
  .endif
    lsr w3, w3, w6
74:
.elseif \kind == 3 /* ASR: #0 = #32 */
    cbnz w6, 73f
  .if \needc
    lsr w4, w3, #31
  .endif
    asr w3, w3, #31
    b 74f
73:
  .if \needc
    sub w7, w6, #1
    lsr w4, w3, w7
    and w4, w4, #1
  .endif
    asr w3, w3, w6
74:
.else /* ROR: #0 = RRX */
    cbnz w6, 73f
    ldr w7, [x23, #R_CPSR]
    ubfx w7, w7, #29, #1
  .if \needc
    and w4, w3, #1
  .endif
    lsr w3, w3, #1
    orr w3, w3, w7, lsl #31
    b 74f
73:
  .if \needc
    sub w7, w6, #1
    lsr w4, w3, w7
    and w4, w4, #1
  .endif
    ror w3, w3, w6
74:
.endif
.endm

/* Operand 2 into w3; with \needc the shifter's carry-out into w4. */
.macro OPERAND kind, needc
.if \kind == 0
    and w3, w10, #0xff
    ubfx w5, w10, #8, #4
    lsl w5, w5, #1
    ror w3, w3, w5
  .if \needc
    OLD_C
    cbz w5, 71f
    lsr w4, w3, #31
71:
  .endif
.else
    and w5, w10, #0xf
    ldr w3, [x23, w5, uxtw #2]
    ubfx w6, w10, #7, #5
    SHIFT_IMM \kind, \needc
.endif
.endm

/* NZCV from the host flags into CPSR. */
.macro FLAGS_ARITH
    mrs x6, nzcv
    ldr w7, [x23, #R_CPSR]
    lsr w6, w6, #28
    bfi w7, w6, #28, #4
    str w7, [x23, #R_CPSR]
.endm

/* N and Z from w0, C from w4, V kept. */
.macro FLAGS_LOGIC
    ldr w7, [x23, #R_CPSR]
    and w7, w7, #0x1fffffff
    and w6, w0, #0x80000000
    orr w7, w7, w6
    cmp w0, #0
    cset w6, eq
    orr w7, w7, w6, lsl #30
    orr w7, w7, w4, lsl #29
    str w7, [x23, #R_CPSR]
.endm

/* The guest C flag into the host's (for ADC / SBC / RSC). */
.macro HOST_C
    ldr w6, [x23, #R_CPSR]
    msr nzcv, x6
.endm

.macro DP opc, s, kind
    .type a64_arm_dp_\opc\()_\s\()_\kind, %function
a64_arm_dp_\opc\()_\s\()_\kind:
    ubfx w1, w10, #12, #4
    cmp w1, #15
    b.eq a64_arm_slow
    ARM_FETCHED
    ubfx w2, w10, #16, #4
    ldr w2, [x23, w2, uxtw #2]
    .set logical, (\opc == 0) || (\opc == 1) || (\opc == 8) || (\opc == 9) || (\opc >= 12)
    OPERAND \kind, (\s && logical)
.if (\opc == 0) || (\opc == 8)
    and w0, w2, w3
.elseif (\opc == 1) || (\opc == 9)
    eor w0, w2, w3
.elseif (\opc == 2) || (\opc == 10)
  .if \s
    subs w0, w2, w3
  .else
    sub w0, w2, w3
  .endif
.elseif \opc == 3
  .if \s
    subs w0, w3, w2
  .else
    sub w0, w3, w2
  .endif
.elseif (\opc == 4) || (\opc == 11)
  .if \s
    adds w0, w2, w3
  .else
    add w0, w2, w3
  .endif
.elseif \opc == 5
    HOST_C
  .if \s
    adcs w0, w2, w3
  .else
    adc w0, w2, w3
  .endif
.elseif \opc == 6
    HOST_C
  .if \s
    sbcs w0, w2, w3
  .else
    sbc w0, w2, w3
  .endif
.elseif \opc == 7
    HOST_C
  .if \s
    sbcs w0, w3, w2
  .else
    sbc w0, w3, w2
  .endif
.elseif \opc == 12
    orr w0, w2, w3
.elseif \opc == 13
    mov w0, w3
.elseif \opc == 14
    bic w0, w2, w3
.else
    mvn w0, w3
.endif
.if \s
  .if logical
    FLAGS_LOGIC
  .else
    FLAGS_ARITH
  .endif
.endif
.if (\opc < 8) || (\opc > 11)
    str w0, [x23, w1, uxtw #2]
.endif
    add w20, w20, #4
    mov w26, #1
    ARM_DISPATCH
.endm

.irp opc, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
  .irp s, 0,1
    .if ((\opc < 8) || (\opc > 11)) || \s
      .irp kind, 0,1,2,3,4
        DP \opc, \s, \kind
      .endr
    .endif
  .endr
.endr

/* ---- B / BL ---- */
    .global a64_arm_branch
    .type a64_arm_branch, %function
a64_arm_branch:
    ARM_FETCHED
    sbfx w1, w10, #0, #24
    tbz w10, #24, 75f
    add w2, w20, #4
    str w2, [x23, #R_LR]
75:
    add w20, w20, #8
    add w20, w20, w1, lsl #2
    REFILL 4, 1
    mov w26, #1
    ARM_DISPATCH

/* ---- LDR / STR / LDRB / STRB with memory DATA_PTR takes; not PC as the destination ---- */

/* !P or W: Rn = the moved base (w4). */
.macro WRITEBACK
    tst w10, #(1 << 24)
    b.eq 97f
    tst w10, #(1 << 21)
    b.eq 98f
97:
    ubfx w9, w10, #16, #4
    str w4, [x23, w9, uxtw #2]
98:
.endm

/* The rest of one: w5 the address, w4 the moved base, w1 Rd; \wb: Rn = the moved base. */
.macro ARM_SINGLE_TAIL load, byte, wb
.if \byte
    mov w6, w5
    DATA_PTR (1 - \load), 1, a64_arm_slow
.else
    and w6, w5, #~3
    DATA_PTR (1 - \load), 4, a64_arm_slow
.endif
    add x13, x24, w12, uxtw #2
    ldrb w13, [x13, #(BUS_CYCLES + (1 - \byte) * 2)]
    FETCH_CYCLES
    add x14, x21, x9
    cmp x14, x22
    b.hs a64_arm_slow /* a DMA may be due by the access: the bus runs it */
    ARM_FETCHED_W9
    add x21, x21, x13
    cmp w12, #8
    csel w11, w13, wzr, lo /* off_rom: the data access, when not the cartridge */
.if \load
  .if \byte
    ldrb w0, [x8]
  .else
    ldr w0, [x8]
    and w9, w5, #3
    lsl w9, w9, #3
    ror w0, w0, w9
  .endif
    add x21, x21, #1 /* bus_idle(1) */
    add w11, w11, #1
  .if \wb
    ubfx w9, w10, #16, #4
    str w4, [x23, w9, uxtw #2]
  .endif
    str w0, [x23, w1, uxtw #2] /* a load into the base register wins */
.else
    ldr w0, [x23, w1, uxtw #2]
  .if \byte
    strb w0, [x8]
  .else
    str w0, [x8]
  .endif
  .if \wb
    ubfx w9, w10, #16, #4
    str w4, [x23, w9, uxtw #2]
  .endif
.endif
    add w20, w20, #4
    AFTER_DATA ARM_DISPATCH
.endm

/* One copy per I P U B W L (instruction bits 25-20): a64_arm_sgl_<bits 27-20>. */
.macro SINGLE hi
a64_arm_sgl_\hi:
    ubfx w1, w10, #12, #4
    cmp w1, #15
    b.eq a64_arm_slow
    add w9, w20, #8
    str w9, [x23, #R_PC] /* the base or the offset register may be PC */
.if (\hi >> 5) & 1 /* offset = Rm shifted by an immediate (shift_by_imm; its carry-out is unused) */
    and w5, w10, #0xf
    ldr w3, [x23, w5, uxtw #2]
    ubfx w6, w10, #7, #5
    ubfx w7, w10, #5, #2
    cbz w7, 63f
    cmp w7, #2
    b.lo 64f
    b.eq 65f
    cbnz w6, 66f /* ROR #0 = RRX */
    ldr w7, [x23, #R_CPSR]
    ubfx w7, w7, #29, #1
    lsr w3, w3, #1
    orr w3, w3, w7, lsl #31
    b 62f
66:
    ror w3, w3, w6
    b 62f
63:
    lsl w3, w3, w6
    b 62f
64: /* LSR: #0 = #32 */
    lsr w3, w3, w6
    cbnz w6, 62f
    mov w3, #0
    b 62f
65: /* ASR: #0 = #32 */
    cbnz w6, 67f
    mov w6, #31
67:
    asr w3, w3, w6
62:
.else
    and w3, w10, #0xfff
.endif
    ubfx w2, w10, #16, #4
    ldr w2, [x23, w2, uxtw #2] /* base */
.if (\hi >> 3) & 1
    add w4, w2, w3 /* moved */
.else
    sub w4, w2, w3
.endif
.if (\hi >> 4) & 1
    mov w5, w4 /* pre-indexed */
.else
    mov w5, w2
.endif
    ARM_SINGLE_TAIL (\hi & 1), ((\hi >> 2) & 1), ((((\hi >> 4) & 1) == 0) || ((\hi >> 1) & 1))
.endm

.irp hi, 64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,96,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,115,116,117,118,119,120,121,122,123,124,125,126,127
    SINGLE \hi
.endr

/* ==== Thumb state ==== */

/* The Thumb instruction at w20: limit, window, then its handler. */
.macro THUMB_DISPATCH
    cmp x21, x22
    b.hs .Lthumb_exit
    sub w9, w20, w27
    cmp w9, w28
    b.hs .Lthumb_window
    ldrh w10, [x25, w9, uxtw]
    lsr w11, w10, #6
    adrp x12, a64_thumb_table
    add x12, x12, :lo12:a64_thumb_table
    ldr x12, [x12, w11, uxtw #3]
    br x12
.endm

/* The fetch counted (w9 its cycles), as bus_fetch16 and cpu_step do it; r15 = PC + 4. */
.macro THUMB_FETCHED_W9
    add x21, x21, x9
    strb wzr, [x24, #BUS_IN_BIOS]
    orr w11, w10, w10, lsl #16
    str w11, [x24, #BUS_OPEN_BUS]
    add w11, w20, #4
    str w11, [x23, #R_PC]
.endm

.macro THUMB_FETCHED
    FETCH_CYCLES
    THUMB_FETCHED_W9
.endm

/* The next instruction, after one without a data access or a branch. */
.macro THUMB_NEXT
    add w20, w20, #2
    mov w26, #1
    THUMB_DISPATCH
.endm




/* One Thumb load or store: w5 the address, w1 the register. Kinds: 0 STR, 1 STRB, 2 STRH, 3 LDR, 4 LDRB, 5 LDRH,
 * 6 LDSB, 7 LDSH (aligned; a misaligned LDSH is an LDSB, see TMEM). */
.macro TMEM_BODY kind, size
.if \size == 4
    and w6, w5, #~3
.elseif \size == 2
    and w6, w5, #~1
.else
    mov w6, w5
.endif
    DATA_PTR (\kind <= 2), \size, a64_thumb_slow
    add x13, x24, w12, uxtw #2
.if \size == 4
    ldrb w13, [x13, #(BUS_CYCLES + 2)]
.else
    ldrb w13, [x13, #BUS_CYCLES]
.endif
    FETCH_CYCLES
    add x14, x21, x9
    cmp x14, x22
    b.hs a64_thumb_slow /* a DMA may be due by the access: the bus runs it */
    THUMB_FETCHED_W9
    add x21, x21, x13
    cmp w12, #8
    csel w11, w13, wzr, lo
.if \kind == 0
    ldr w0, [x23, w1, uxtw #2]
    str w0, [x8]
.elseif \kind == 1
    ldr w0, [x23, w1, uxtw #2]
    strb w0, [x8]
.elseif \kind == 2
    ldr w0, [x23, w1, uxtw #2]
    strh w0, [x8]
.else
  .if \kind == 3
    ldr w0, [x8]
    and w9, w5, #3
    lsl w9, w9, #3
    ror w0, w0, w9
  .elseif \kind == 4
    ldrb w0, [x8]
  .elseif \kind == 5
    ldrh w0, [x8]
    and w9, w5, #1
    lsl w9, w9, #3
    ror w0, w0, w9
  .elseif \kind == 6
    ldrsb w0, [x8]
  .else
    ldrsh w0, [x8]
  .endif
    str w0, [x23, w1, uxtw #2]
    add x21, x21, #1 /* bus_idle(1) */
    add w11, w11, #1
.endif
    add w20, w20, #2
    AFTER_DATA THUMB_DISPATCH
.endm

.macro TMEM kind
.if \kind == 7
    tst w5, #1
    b.ne 92f
    TMEM_BODY 7, 2
92:
    TMEM_BODY 6, 1
.elseif \kind == 0 || \kind == 3
    TMEM_BODY \kind, 4
.elseif \kind == 2 || \kind == 5
    TMEM_BODY \kind, 2
.else
    TMEM_BODY \kind, 1
.endif
.endm

/* w3 = w3 shifted by register amount w6 (shift_by_reg); w4 = the carry (unchanged for amount 0). \t: 0 LSL,
 * 1 LSR, 2 ASR, 3 ROR. */
.macro SHIFT_REG t
    OLD_C
    cbz w6, 96f
.if \t == 0
    cmp w6, #32
    b.hi 93f
    b.eq 94f
    mov w7, #32
    sub w7, w7, w6
    lsr w4, w3, w7
    and w4, w4, #1
    lsl w3, w3, w6
    b 96f
94:
    and w4, w3, #1
    mov w3, #0
    b 96f
93:
    mov w4, #0
    mov w3, #0
.elseif \t == 1
    cmp w6, #32
    b.hi 93f
    b.eq 94f
    sub w7, w6, #1
    lsr w4, w3, w7
    and w4, w4, #1
    lsr w3, w3, w6
    b 96f
94:
    lsr w4, w3, #31
    mov w3, #0
    b 96f
93:
    mov w4, #0
    mov w3, #0
.elseif \t == 2
    cmp w6, #32
    b.hs 93f
    sub w7, w6, #1
    lsr w4, w3, w7
    and w4, w4, #1
    asr w3, w3, w6
    b 96f
93:
    lsr w4, w3, #31
    asr w3, w3, #31
.else
    and w6, w6, #31
    cbnz w6, 93f
    lsr w4, w3, #31
    b 96f
93:
    sub w7, w6, #1
    lsr w4, w3, w7
    and w4, w4, #1
    ror w3, w3, w6
.endif
96:
.endm

    .text
    .global a64_run_thumb
    .type a64_run_thumb, %function
/* void a64_run_thumb(a64_ctx_t *x, uint32_t *regs, uint8_t *bus): the loop in Thumb state, from r15. */
a64_run_thumb:
    stp x29, x30, [sp, #-96]!
    mov x29, sp
    stp x19, x20, [sp, #16]
    stp x21, x22, [sp, #32]
    stp x23, x24, [sp, #48]
    stp x25, x26, [sp, #64]
    stp x27, x28, [sp, #80]
    mov x19, x0
    mov x23, x1
    mov x24, x2
    LOAD_STATE
    THUMB_DISPATCH

.Lthumb_exit:
    STORE_STATE
    mov w0, #0
.Lthumb_return:
    ldp x19, x20, [sp, #16]
    ldp x21, x22, [sp, #32]
    ldp x23, x24, [sp, #48]
    ldp x25, x26, [sp, #64]
    ldp x27, x28, [sp, #80]
    ldp x29, x30, [sp], #96
    ret

.Lthumb_switch:
    STORE_STATE
    mov w0, #2
    b .Lthumb_return

.Lthumb_window:
    mov x0, x19
    mov w1, w20
    mov w2, #2
    bl a64_window
    cbz w0, a64_thumb_slow
    ldr x25, [x19, #CTX_WIN_HOST]
    ldp w27, w28, [x19, #CTX_WIN_LO]
    THUMB_DISPATCH

    .global a64_thumb_slow
    .type a64_thumb_slow, %function
a64_thumb_slow:
    STORE_STATE
    mov x0, x19
    bl a64_slow
    cbz w0, .Lthumb_return
    LOAD_STATE
    THUMB_DISPATCH

/* THUMB 1: LSL / LSR / ASR Rd, Rs, #imm5 */
.macro T_SHIFT t
a64_thumb_shift_\t:
    THUMB_FETCHED
    ubfx w5, w10, #3, #3
    ldr w3, [x23, w5, uxtw #2]
    ubfx w6, w10, #6, #5
    SHIFT_IMM (1 + \t), 1
    mov w0, w3
    and w1, w10, #7
    str w0, [x23, w1, uxtw #2]
    FLAGS_LOGIC
    THUMB_NEXT
.endm

/* THUMB 2: ADD / SUB Rd, Rs, Rn | #imm3 */
.macro T_ADDSUB v
a64_thumb_addsub_\v:
    THUMB_FETCHED
    ubfx w5, w10, #3, #3
    ldr w2, [x23, w5, uxtw #2]
    ubfx w6, w10, #6, #3
.if \v & 2
    mov w3, w6
.else
    ldr w3, [x23, w6, uxtw #2]
.endif
.if \v & 1
    subs w0, w2, w3
.else
    adds w0, w2, w3
.endif
    FLAGS_ARITH
    and w1, w10, #7
    str w0, [x23, w1, uxtw #2]
    THUMB_NEXT
.endm

/* THUMB 3: MOV / CMP / ADD / SUB Rd, #imm8 */
.macro T_IMM v
a64_thumb_imm_\v:
    THUMB_FETCHED
    ubfx w1, w10, #8, #3
    and w3, w10, #0xff
.if \v == 0
    mov w0, w3
    str w0, [x23, w1, uxtw #2]
    FLAGS_NZ
.else
    ldr w2, [x23, w1, uxtw #2]
  .if \v == 2
    adds w0, w2, w3
  .else
    subs w0, w2, w3
  .endif
    FLAGS_ARITH
  .if \v != 1
    str w0, [x23, w1, uxtw #2]
  .endif
.endif
    THUMB_NEXT
.endm

/* THUMB 4: ALU operations (not MUL, which cpu_step runs) */
.macro T_ALU v
a64_thumb_alu_\v:
    THUMB_FETCHED
    and w1, w10, #7
    ldr w2, [x23, w1, uxtw #2]
    ubfx w5, w10, #3, #3
    ldr w3, [x23, w5, uxtw #2]
.if \v == 0 || \v == 8
    and w0, w2, w3
    FLAGS_NZ
.elseif \v == 1
    eor w0, w2, w3
    FLAGS_NZ
.elseif \v == 2 || \v == 3 || \v == 4 || \v == 7
    and w6, w3, #0xff
    mov w3, w2
  .if \v == 7
    SHIFT_REG 3
  .else
    SHIFT_REG (\v - 2)
  .endif
    mov w0, w3
    FLAGS_LOGIC
    add x21, x21, #1 /* bus_idle(1) */
.elseif \v == 5
    HOST_C
    adcs w0, w2, w3
    FLAGS_ARITH
.elseif \v == 6
    HOST_C
    sbcs w0, w2, w3
    FLAGS_ARITH
.elseif \v == 9
    negs w0, w3
    FLAGS_ARITH
.elseif \v == 10
    subs w0, w2, w3
    FLAGS_ARITH
.elseif \v == 11
    adds w0, w2, w3
    FLAGS_ARITH
.elseif \v == 12
    orr w0, w2, w3
    FLAGS_NZ
.elseif \v == 14
    bic w0, w2, w3
    FLAGS_NZ
.else
    mvn w0, w3
    FLAGS_NZ
.endif
.if \v != 8 && \v != 10 && \v != 11
    str w0, [x23, w1, uxtw #2]
.endif
    THUMB_NEXT
.endm

/* THUMB 5: ADD / CMP / MOV with high registers, not writing PC */
.macro T_HI v
a64_thumb_hi_\v:
    and w1, w10, #7
    lsr w5, w10, #4
    and w5, w5, #8
    orr w1, w1, w5 /* rd: H1 is bit 7 */
.if \v != 1
    cmp w1, #15
    b.eq a64_thumb_slow
.endif
    THUMB_FETCHED
    ubfx w5, w10, #3, #4
    ldr w3, [x23, w5, uxtw #2]
.if \v == 0
    ldr w2, [x23, w1, uxtw #2]
    add w0, w2, w3
    str w0, [x23, w1, uxtw #2]
.elseif \v == 1
    ldr w2, [x23, w1, uxtw #2]
    subs w0, w2, w3
    FLAGS_ARITH
.else
    str w3, [x23, w1, uxtw #2]
.endif
    THUMB_NEXT
.endm

/* THUMB 6: LDR Rd, [PC, #imm8*4] */
a64_thumb_pcload:
    add w5, w20, #4
    and w5, w5, #~2
    and w6, w10, #0xff
    add w5, w5, w6, lsl #2
    ubfx w1, w10, #8, #3
    TMEM 3

/* THUMB 7 / 8: [Rb, Ro] */
.macro T_REGOFF name, kind
a64_thumb_\name:
    ubfx w5, w10, #3, #3
    ldr w5, [x23, w5, uxtw #2]
    ubfx w6, w10, #6, #3
    ldr w6, [x23, w6, uxtw #2]
    add w5, w5, w6
    and w1, w10, #7
    TMEM \kind
.endm

/* THUMB 9 / 10: [Rb, #imm5 * scale] */
.macro T_IMMOFF name, kind, shift
a64_thumb_\name:
    ubfx w5, w10, #3, #3
    ldr w5, [x23, w5, uxtw #2]
    ubfx w6, w10, #6, #5
    add w5, w5, w6, lsl #\shift
    and w1, w10, #7
    TMEM \kind
.endm

/* THUMB 11: [SP, #imm8*4] */
.macro T_SPREL name, kind
a64_thumb_\name:
    ldr w5, [x23, #52]
    and w6, w10, #0xff
    add w5, w5, w6, lsl #2
    ubfx w1, w10, #8, #3
    TMEM \kind
.endm

/* THUMB 12: ADD Rd, PC | SP, #imm8*4 */
.macro T_LDADDR v
a64_thumb_ldaddr_\v:
    THUMB_FETCHED
.if \v
    ldr w2, [x23, #52]
.else
    add w2, w20, #4
    and w2, w2, #~2
.endif
    and w6, w10, #0xff
    add w0, w2, w6, lsl #2
    ubfx w1, w10, #8, #3
    str w0, [x23, w1, uxtw #2]
    THUMB_NEXT
.endm

/* THUMB 13: ADD SP, #±imm7*4 */
a64_thumb_spadj:
    THUMB_FETCHED
    ldr w2, [x23, #52]
    and w6, w10, #0x7f
    lsl w6, w6, #2
    add w3, w2, w6
    sub w4, w2, w6
    tst w10, #0x80
    csel w2, w4, w3, ne
    str w2, [x23, #52]
    THUMB_NEXT

/* THUMB 16: B<cond> */
a64_thumb_bcond:
    THUMB_FETCHED
    ldr w11, [x23, #R_CPSR]
    adrp x12, cpu_cond_table
    add x12, x12, :lo12:cpu_cond_table
    ubfx w13, w10, #8, #4
    ldrh w12, [x12, w13, uxtw #1]
    lsr w11, w11, #28
    lsr w12, w12, w11
    tst w12, #1
    b.ne 1f
    THUMB_NEXT
1:
    sxtb w1, w10
    add w20, w20, #4
    add w20, w20, w1, lsl #1
    REFILL 2, 0
    mov w26, #1
    THUMB_DISPATCH

/* THUMB 18: B */
a64_thumb_b:
    THUMB_FETCHED
    sbfx w1, w10, #0, #11
    add w20, w20, #4
    add w20, w20, w1, lsl #1
    REFILL 2, 0
    mov w26, #1
    THUMB_DISPATCH

/* THUMB 19: BL, first half: LR = PC + (offset11 << 12) */
a64_thumb_blhi:
    THUMB_FETCHED
    sbfx w1, w10, #0, #11
    add w2, w20, #4
    add w2, w2, w1, lsl #12
    str w2, [x23, #R_LR]
    THUMB_NEXT

/* THUMB 19, second half: to LR + (offset11 << 1); LR = the next instruction | 1 */
a64_thumb_bllo:
    THUMB_FETCHED
    ldr w2, [x23, #R_LR]
    and w1, w10, #0x7ff
    add w2, w2, w1, lsl #1
    add w3, w20, #2
    orr w3, w3, #1
    str w3, [x23, #R_LR]
    and w20, w2, #~1
    REFILL 2, 0
    mov w26, #1
    THUMB_DISPATCH

.irp t, 0,1,2
    T_SHIFT \t
.endr
.irp v, 0,1,2,3
    T_ADDSUB \v
    T_IMM \v
.endr
.irp v, 0,1,2,3,4,5,6,7,8,9,10,11,12,14,15
    T_ALU \v
.endr
.irp v, 0,1,2
    T_HI \v
.endr
    T_REGOFF regoff_0, 0
    T_REGOFF regoff_1, 1
    T_REGOFF regoff_2, 3
    T_REGOFF regoff_3, 4
    T_REGOFF regoffs_0, 2
    T_REGOFF regoffs_1, 6
    T_REGOFF regoffs_2, 5
    T_REGOFF regoffs_3, 7
    T_IMMOFF immoff_0, 0, 2
    T_IMMOFF immoff_1, 3, 2
    T_IMMOFF immoff_2, 1, 0
    T_IMMOFF immoff_3, 4, 0
    T_IMMOFF half_0, 2, 1
    T_IMMOFF half_1, 5, 1
    T_SPREL sprel_0, 0
    T_SPREL sprel_1, 3
    T_LDADDR 0
    T_LDADDR 1

/* ==== multi-register transfers with RAM (fast_block_ptr's rules) ==== */

/* w3 = the number of bits set in w2 (up to 16 bits). */
.macro POPCOUNT16
    fmov s0, w2
    cnt v0.8b, v0.8b
    addv b0, v0.8b
    fmov w3, s0
.endm

/* A block of w3 words from w5 (each aligned down) in EWRAM or IWRAM, all in one region, whose accesses take as
 * long sequential as not: x8 the region's memory, w13 its address mask, w14 the cycles per word, w12 the region.
 * Anything else goes to \slow. */
.macro BLOCK_CHECK slow
    and w16, w5, #~3
    lsr w12, w16, #24
    sub w17, w3, #1
    add w17, w16, w17, lsl #2
    lsr w17, w17, #24
    cmp w17, w12
    b.ne \slow
    cmp w12, #2
    b.eq 84f
    cmp w12, #3
    b.ne \slow
    ldr x8, [x19, #CTX_IWRAM]
    mov w13, #0x7fff
    b 85f
84:
    ldr x8, [x19, #CTX_EWRAM]
    mov w13, #0x3ffff
85:
    add x16, x24, w12, uxtw #2
    ldrb w14, [x16, #(BUS_CYCLES + 2)]
    ldrb w17, [x16, #(BUS_CYCLES + 3)]
    cmp w14, w17
    b.ne \slow
.endm

/* w9 = the fetch's cycles; no DMA may be due by the last word: now + fetch + (count - 1) * cycles < limit. */
.macro BLOCK_TIME slow
    FETCH_CYCLES
    sub w17, w3, #1
    mul w17, w17, w14
    add x16, x21, x9
    add x16, x16, x17
    cmp x16, x22
    b.hs \slow
.endm

/* One word at w5 (then w5 += 4), its cycles off the cartridge bus. */
.macro BLK_LOAD dst
    and w16, w5, #~3
    and w16, w16, w13
    ldr \dst, [x8, w16, uxtw]
    add x21, x21, x14
    add w11, w11, w14
    add w5, w5, #4
.endm

.macro BLK_STORE src
    and w16, w5, #~3
    and w16, w16, w13
    str \src, [x8, w16, uxtw]
    add x21, x21, x14
    add w11, w11, w14
    add w5, w5, #4
.endm

/* w15 = the lowest set bit of w2, which is cleared. */
.macro NEXT_BIT
    rbit w15, w2
    clz w15, w15
    sub w17, w2, #1
    and w2, w2, w17
.endm

/* THUMB 14: PUSH {rlist, LR} / POP {rlist, PC}, not an empty list */
a64_thumb_pushpop:
    and w2, w10, #0xff
    POPCOUNT16
    ubfx w4, w10, #8, #1
    add w3, w3, w4
    cbz w3, a64_thumb_slow
    ldr w5, [x23, #52]
    tst w10, #0x800
    b.ne 1f
    sub w5, w5, w3, lsl #2 /* PUSH: from SP - 4 * count */
1:
    mov w6, w5
    BLOCK_CHECK a64_thumb_slow
    BLOCK_TIME a64_thumb_slow
    THUMB_FETCHED_W9
    mov w11, #0
    tst w10, #0x800
    b.eq 3f
2: /* POP */
    cbz w2, 4f
    NEXT_BIT
    BLK_LOAD w0
    str w0, [x23, w15, uxtw #2]
    b 2b
4:
    tst w10, #0x100
    b.eq 5f
    BLK_LOAD w0 /* POP {pc}: ARMv4 does not change state */
    str w5, [x23, #52]
    add x21, x21, #1 /* bus_idle(1) */
    and w20, w0, #~1
    REFILL 2, 0
    mov w26, #1
    THUMB_DISPATCH
5:
    str w5, [x23, #52]
    add x21, x21, #1 /* bus_idle(1) */
    add w11, w11, #1
    add w20, w20, #2
    AFTER_DATA THUMB_DISPATCH
3: /* PUSH */
    str w6, [x23, #52]
6:
    cbz w2, 7f
    NEXT_BIT
    ldr w0, [x23, w15, uxtw #2]
    BLK_STORE w0
    b 6b
7:
    tst w10, #0x100
    b.eq 8f
    ldr w0, [x23, #R_LR]
    BLK_STORE w0
8:
    add w20, w20, #2
    AFTER_DATA THUMB_DISPATCH

/* THUMB 15: STMIA / LDMIA Rb!, {rlist}, not an empty list */
a64_thumb_block:
    and w2, w10, #0xff
    POPCOUNT16
    cbz w3, a64_thumb_slow
    ubfx w1, w10, #8, #3
    ldr w4, [x23, w1, uxtw #2]
    mov w5, w4
    BLOCK_CHECK a64_thumb_slow
    BLOCK_TIME a64_thumb_slow
    THUMB_FETCHED_W9
    mov w11, #0
    add w6, w4, w3, lsl #2 /* the new base */
    tst w10, #0x800
    b.eq 3f
    str w6, [x23, w1, uxtw #2] /* a loaded base register overrides the writeback */
2:
    cbz w2, 4f
    NEXT_BIT
    BLK_LOAD w0
    str w0, [x23, w15, uxtw #2]
    b 2b
4:
    add x21, x21, #1 /* bus_idle(1) */
    add w11, w11, #1
    b 9f
3: /* a stored base is the original value only when it is the lowest register listed */
    mov w17, #1
    lsl w17, w17, w1
    sub w17, w17, #1
    tst w2, w17
    csel w7, w4, w6, eq
6:
    cbz w2, 7f
    NEXT_BIT
    ldr w0, [x23, w15, uxtw #2]
    cmp w15, w1
    csel w0, w7, w0, eq
    BLK_STORE w0
    b 6b
7:
    str w6, [x23, w1, uxtw #2]
9:
    add w20, w20, #2
    AFTER_DATA THUMB_DISPATCH

/* ARM LDM / STM with RAM, without the S bit, an empty list or PC as the base */
    .global a64_arm_block
    .type a64_arm_block, %function
a64_arm_block:
    tst w10, #(1 << 22)
    b.ne a64_arm_slow
    and w2, w10, #0xffff
    cbz w2, a64_arm_slow
    ubfx w1, w10, #16, #4
    cmp w1, #15
    b.eq a64_arm_slow
    POPCOUNT16
    ldr w4, [x23, w1, uxtw #2] /* base */
    add w6, w4, w3, lsl #2
    sub w7, w4, w3, lsl #2
    tst w10, #(1 << 23)
    b.eq 1f
    mov w5, w4 /* up: base (+ 4 if pre) */
    tst w10, #(1 << 24)
    b.eq 2f
    add w5, w5, #4
    b 2f
1:
    mov w5, w7 /* down: base - bytes (+ 4 if post) */
    mov w6, w7
    tst w10, #(1 << 24)
    b.ne 2f
    add w5, w5, #4
2: /* w6 = the new base */
    BLOCK_CHECK a64_arm_slow
    BLOCK_TIME a64_arm_slow
    ARM_FETCHED_W9
    mov w11, #0
    tst w10, #(1 << 20)
    b.eq 5f
    /* LDM */
    tst w10, #(1 << 21)
    b.eq 3f
    str w6, [x23, w1, uxtw #2] /* a loaded base register overrides the writeback */
3:
    and w0, w2, #0x8000
    and w2, w2, #0x7fff
4:
    cbz w2, 6f
    NEXT_BIT
    BLK_LOAD w4
    str w4, [x23, w15, uxtw #2]
    b 4b
6:
    cbz w0, 7f
    BLK_LOAD w4 /* PC */
    add x21, x21, #1 /* bus_idle(1) */
    and w20, w4, #~3
    REFILL 4, 1
    mov w26, #1
    ARM_DISPATCH
7:
    add x21, x21, #1 /* bus_idle(1) */
    add w11, w11, #1
    add w20, w20, #4
    AFTER_DATA ARM_DISPATCH
5: /* STM: a stored base is the original value only when it is the lowest register listed, or without writeback */
    mov w17, #1
    lsl w17, w17, w1
    sub w17, w17, #1
    tst w2, w17 /* Z: the base is the lowest listed */
    csel w7, w4, w6, eq
    tst w10, #(1 << 21)
    csel w7, w4, w7, eq
8:
    cbz w2, 9f
    NEXT_BIT
    ldr w0, [x23, w15, uxtw #2]
    cmp w15, w1
    csel w0, w7, w0, eq
    cmp w15, #15
    add w17, w20, #12 /* STM PC stores the address + 12 */
    csel w0, w17, w0, eq
    BLK_STORE w0
    b 8b
9:
    tst w10, #(1 << 21)
    b.eq 10f
    str w6, [x23, w1, uxtw #2]
10:
    add w20, w20, #4
    AFTER_DATA ARM_DISPATCH

/* ==== multiplies, halfword transfers, BX ==== */

/* w7 = the multiplier's array cycles for w3 (alu_mul_cycles with all-ones too): 1-4. */
.macro MUL_CYCLES
    eor w6, w3, w3, asr #31
    mov w7, #1
    cmp w6, #0x100
    cinc w7, w7, hs
    cmp w6, #0x10, lsl #12
    cinc w7, w7, hs
    lsr w8, w6, #24
    cmp w8, #0
    cinc w7, w7, ne
.endm

/* w4 = mul_carry(MUL_SHORT, w2 = multiplicand, w3 = multiplier, w5 = acc); keeps w0, w1, w11. */
.macro CALL_MUL_CARRY
    stp x0, x1, [sp, #-32]!
    str x11, [sp, #16]
    mov w0, #0
    mov w1, w2
    mov w2, w3
    mov w3, w5
    bl mul_carry
    mov w4, w0
    ldr x11, [sp, #16]
    ldp x0, x1, [sp], #32
.endm

/* ARM MUL / MLA: Rd = Rm * Rs (+ Rn); not PC as Rd. Like a data access: the next fetch is non-sequential. */
    .global a64_arm_mul
    .type a64_arm_mul, %function
a64_arm_mul:
    ubfx w1, w10, #16, #4
    cmp w1, #15
    b.eq a64_arm_slow
    ARM_FETCHED
    and w5, w10, #0xf
    ldr w2, [x23, w5, uxtw #2] /* m */
    ubfx w5, w10, #8, #4
    ldr w3, [x23, w5, uxtw #2] /* s */
    mov w5, #0
    tst w10, #(1 << 21)
    b.eq 1f
    ubfx w5, w10, #12, #4
    ldr w5, [x23, w5, uxtw #2] /* acc: read before Rd is written */
1:
    madd w0, w2, w3, w5
    MUL_CYCLES
    tst w10, #(1 << 21)
    cinc w7, w7, ne
    add x21, x21, x7 /* bus_idle(cycles) */
    mov w11, w7
    str w0, [x23, w1, uxtw #2]
    tst w10, #(1 << 20)
    b.eq 2f
    CALL_MUL_CARRY
    FLAGS_LOGIC /* N, Z; C from the Booth array; V kept */
2:
    add w20, w20, #4
    mov w12, #0 /* the bus was left free: a "RAM" access for the prefetch buffer */
    AFTER_DATA ARM_DISPATCH

/* THUMB 4 MUL: Rd = Rs * Rd (Rd is the multiplier); C from the Booth array, V kept */
a64_thumb_mul:
    THUMB_FETCHED
    and w1, w10, #7
    ldr w3, [x23, w1, uxtw #2] /* a: the multiplier */
    ubfx w5, w10, #3, #3
    ldr w2, [x23, w5, uxtw #2] /* b */
    mul w0, w2, w3
    MUL_CYCLES
    add x21, x21, x7
    mov w11, w7
    str w0, [x23, w1, uxtw #2]
    mov w5, #0
    CALL_MUL_CARRY
    FLAGS_LOGIC
    add w20, w20, #2
    mov w12, #0
    AFTER_DATA THUMB_DISPATCH

/* The rest of an ARM halfword / signed transfer: w5 the address, w4 the moved base, w1 Rd. Kinds as TMEM. */
.macro ARM_HALF_TAIL kind, size
.if \size == 2
    and w6, w5, #~1
.else
    mov w6, w5
.endif
    DATA_PTR (\kind == 2), \size, a64_arm_slow
    add x13, x24, w12, uxtw #2
    ldrb w13, [x13, #BUS_CYCLES]
    FETCH_CYCLES
    add x14, x21, x9
    cmp x14, x22
    b.hs a64_arm_slow
    ARM_FETCHED_W9
    add x21, x21, x13
    cmp w12, #8
    csel w11, w13, wzr, lo
.if \kind == 2
    ldr w0, [x23, w1, uxtw #2]
    strh w0, [x8]
    WRITEBACK
.else
  .if \kind == 5
    ldrh w0, [x8]
    and w9, w5, #1
    lsl w9, w9, #3
    ror w0, w0, w9
  .elseif \kind == 6
    ldrsb w0, [x8]
  .else
    ldrsh w0, [x8]
  .endif
    add x21, x21, #1 /* bus_idle(1) */
    add w11, w11, #1
    WRITEBACK
    str w0, [x23, w1, uxtw #2]
.endif
    add w20, w20, #4
    AFTER_DATA ARM_DISPATCH
.endm

/* LDRH / STRH / LDRSB / LDRSH; not PC as Rd, not the ARMv5 doubleword forms. */
    .global a64_arm_half
    .type a64_arm_half, %function
a64_arm_half:
    ubfx w1, w10, #12, #4
    cmp w1, #15
    b.eq a64_arm_slow
    ubfx w7, w10, #5, #2 /* sh */
    tst w10, #(1 << 20)
    b.ne 1f
    cmp w7, #1
    b.ne a64_arm_slow /* L = 0 with sh 2 / 3: no effect on the ARM7TDMI */
1:
    add w9, w20, #8
    str w9, [x23, #R_PC] /* the base or the offset register may be PC */
    tst w10, #(1 << 22)
    b.eq 2f
    lsr w3, w10, #4 /* (op >> 4) & 0xF0 | (op & 0xF) */
    and w3, w3, #0xf0
    and w9, w10, #0xf
    orr w3, w3, w9
    b 3f
2:
    and w9, w10, #0xf
    ldr w3, [x23, w9, uxtw #2]
3:
    ubfx w2, w10, #16, #4
    ldr w2, [x23, w2, uxtw #2]
    add w4, w2, w3
    sub w5, w2, w3
    tst w10, #(1 << 23)
    csel w4, w4, w5, ne
    tst w10, #(1 << 24)
    csel w5, w4, w2, ne
    tst w10, #(1 << 20)
    b.ne 4f
    ARM_HALF_TAIL 2, 2
4:
    cmp w7, #2
    b.eq 5f
    b.hi 6f
    ARM_HALF_TAIL 5, 2
5:
    ARM_HALF_TAIL 6, 1
6:
    tst w5, #1
    b.ne 5b /* a misaligned LDRSH loads a signed byte */
    ARM_HALF_TAIL 7, 2

/* BX Rm: on in ARM state, or out to the Thumb loop. */
    .global a64_arm_bx
    .type a64_arm_bx, %function
a64_arm_bx:
    ARM_FETCHED
    and w5, w10, #0xf
    ldr w2, [x23, w5, uxtw #2]
    tbnz w2, #0, 1f
    and w20, w2, #~3
    REFILL 4, 1
    mov w26, #1
    ARM_DISPATCH
1:
    ldr w7, [x23, #R_CPSR]
    orr w7, w7, #0x20
    str w7, [x23, #R_CPSR]
    and w20, w2, #~1
    REFILL 2, 0
    mov w26, #1
    b .Larm_switch

/* THUMB 5 BX Rs: on in Thumb state, or out to the ARM loop. */
a64_thumb_bx:
    THUMB_FETCHED
    ubfx w5, w10, #3, #4
    ldr w2, [x23, w5, uxtw #2]
    tbz w2, #0, 1f
    and w20, w2, #~1
    REFILL 2, 0
    mov w26, #1
    THUMB_DISPATCH
1:
    ldr w7, [x23, #R_CPSR]
    and w7, w7, #~0x20
    str w7, [x23, #R_CPSR]
    and w20, w2, #~3
    REFILL 4, 1
    mov w26, #1
    b .Lthumb_switch

    .section .data.rel.ro, "aw"
    .p2align 3
    .global a64_arm_single_handlers
.macro SGL_ENTRY hi
    .quad a64_arm_sgl_\hi
.endm
a64_arm_single_handlers: /* [bits 25-20] */
.irp hi, 64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,96,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,115,116,117,118,119,120,121,122,123,124,125,126,127
    SGL_ENTRY \hi
.endr
    .text

/* The handlers by class and variant, for cpu_a64.c's table (the order of the THUMB_CLASS_* enum is not used). */
    .section .data.rel.ro, "aw"
    .p2align 3
    .global a64_thumb_handlers
a64_thumb_handlers:
    .quad a64_thumb_shift_0, a64_thumb_shift_1, a64_thumb_shift_2, a64_thumb_slow                /* 0: shift by imm */
    .quad a64_thumb_addsub_0, a64_thumb_addsub_1, a64_thumb_addsub_2, a64_thumb_addsub_3         /* 4: add/sub */
    .quad a64_thumb_imm_0, a64_thumb_imm_1, a64_thumb_imm_2, a64_thumb_imm_3                     /* 8: imm8 */
    .quad a64_thumb_alu_0, a64_thumb_alu_1, a64_thumb_alu_2, a64_thumb_alu_3                     /* 12: ALU */
    .quad a64_thumb_alu_4, a64_thumb_alu_5, a64_thumb_alu_6, a64_thumb_alu_7
    .quad a64_thumb_alu_8, a64_thumb_alu_9, a64_thumb_alu_10, a64_thumb_alu_11
    .quad a64_thumb_alu_12, a64_thumb_mul, a64_thumb_alu_14, a64_thumb_alu_15
    .quad a64_thumb_hi_0, a64_thumb_hi_1, a64_thumb_hi_2, a64_thumb_bx                           /* 28: hi registers */
    .quad a64_thumb_regoff_0, a64_thumb_regoff_1, a64_thumb_regoff_2, a64_thumb_regoff_3         /* 32: [Rb, Ro] */
    .quad a64_thumb_regoffs_0, a64_thumb_regoffs_1, a64_thumb_regoffs_2, a64_thumb_regoffs_3     /* 36: signed */
    .quad a64_thumb_immoff_0, a64_thumb_immoff_1, a64_thumb_immoff_2, a64_thumb_immoff_3         /* 40: [Rb, #imm] */
    .quad a64_thumb_half_0, a64_thumb_half_1, a64_thumb_sprel_0, a64_thumb_sprel_1               /* 44 */
    .quad a64_thumb_ldaddr_0, a64_thumb_ldaddr_1, a64_thumb_pcload, a64_thumb_spadj              /* 48 */
    .quad a64_thumb_bcond, a64_thumb_b, a64_thumb_blhi, a64_thumb_bllo                           /* 52 */
    .quad a64_thumb_pushpop, a64_thumb_block                                                     /* 56 */
    .text

/* ---- the table of data-processing handlers, [opcode][S][kind] ---- */
    .section .data.rel.ro, "aw"
    .p2align 3
    .global a64_arm_dp_handlers
.macro DP_ENTRY opc, s, kind
    .quad a64_arm_dp_\opc\()_\s\()_\kind
.endm
a64_arm_dp_handlers:
.irp opc, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
  .irp s, 0,1
    .irp kind, 0,1,2,3,4
      .if ((\opc < 8) || (\opc > 11)) || \s
        DP_ENTRY \opc, \s, \kind
      .else
        .quad a64_arm_slow
      .endif
    .endr
  .endr
.endr

    .section .note.GNU-stack, "", %progbits
```

- [x] **Step 3: The twin tests, all unit tests and the scenes on phone B**

```bash
timeout 550 bash tools/dev.sh phone-unit twins 2>&1 | grep -E "PASS|FAIL|program|tests,"
timeout 550 bash tools/dev.sh phone-unit 2>&1 | tail -1
bash tools/dev.sh phone-bench --runs 3 --check
bash tools/dev.sh unit 2>&1 | tail -1
```

Expected: `[ PASS ]` for the four twin tests (`twins_asm_loop_matches_c_loop_arm` / `_thumb` too), `4 tests, 0 failed`; `414 tests, 0 failed`; 6 scenes `ok ok`, faster than Task 2's (most in game B and game D; the drawing is still C, so the numbers are not the finish line's); on the PC still `412 tests, 0 failed` (the asm is not built there). (In the prototype these planted bugs each made a twin test fail: the logical ops' C flag, the stall's off-ROM count, Thumb ADC's carry, POP's idle cycle, STM's stored base, a VRAM mirror, MLA's extra cycle, a misaligned LDRSH, Thumb BX's refill, a 7-halfword prefetch buffer, LDR's up/down. An endless loop was found and fixed: the window for Thumb code in RAM's last halfword.)

- [x] **Step 4: Commit**

```bash
git add core/arch/arm64/cpu_a64.c core/arch/arm64/cpu_a64.S docs/PROGRESS.md docs/superpowers/plans/2026-10-03-milestone-7b-asm.md
git commit -m "perf(arm64): the CPU loop in assembly (cpu_a64.S), the C loop its twin" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 4: NEON drawing

**Files:**
- Create: `core/arch/arm64/ppu_neon.h` (`ppu_tile_row4_neon`: a whole 4bpp tile row, the palette by table lookup), `core/arch/arm64/ppu_neon.c` (`ppu_compose_neon`: 16 pixels at a time without windows or effects, 8 with them)
- Modify: `core/src/ppu/ppu_compose.c` (`ppu_compose_generic` the twin; `ppu_compose` → NEON on arm64), `core/src/ppu/ppu_internal.h`, `core/src/ppu/ppu_bg.c` (whole 4bpp rows through NEON on arm64)

- [x] **Step 1: The drawing tests on phone B before** (7a's `test_speedups.c` compares the drawing with the pre-7a code)

```bash
timeout 550 bash tools/dev.sh phone-unit speedup 2>&1 | tail -1
```

Expected: `6 tests, 0 failed` (today's C).

- [x] **Step 2: Write the code**

Create `core/arch/arm64/ppu_neon.h`:

```c
#ifndef GBA_PPU_NEON_H
#define GBA_PPU_NEON_H

/* NEON drawing for arm64 (milestone 7b); the plain-C twins are in core/src/ppu. */
#include <arm_neon.h>

#include "ppu/ppu_internal.h"

/* A whole 4bpp tile row into o[0..7]: the opaque pixels (index != 0) get bank colour, prio and PX_OPAQUE; the rest of
 * o is kept. row holds the 8 indices (pixel i in bits 4i-4i+3), pal_bank the bank's 16 colours. As ppu_bg.c's loop. */
static inline void ppu_tile_row4_neon(layer_px_t *o, uint32_t row, bool hflip, const uint8_t *pal_bank, uint8_t prio) {
    uint8x8_t b = vcreate_u8(row);
    uint8x8_t idx = vzip1_u8(vand_u8(b, vdup_n_u8(0x0F)), vshr_n_u8(b, 4)); /* pixels 0-7 */
    if (hflip) idx = vrev64_u8(idx);
    uint8x16x2_t tbl = {{vld1q_u8(pal_bank), vld1q_u8(pal_bank + 16)}};
    uint8x8_t i2 = vshl_n_u8(idx, 1);
    uint8x8_t lo = vqtbl2_u8(tbl, i2);
    uint8x8_t hi = vand_u8(vqtbl2_u8(tbl, vadd_u8(i2, vdup_n_u8(1))), vdup_n_u8(0x7F));
    uint8x8_t opaque = vtst_u8(idx, idx);
    uint8x8x4_t px = vld4_u8((uint8_t *)o);
    px.val[0] = vbsl_u8(opaque, lo, px.val[0]);
    px.val[1] = vbsl_u8(opaque, hi, px.val[1]);
    px.val[2] = vbsl_u8(opaque, vdup_n_u8(prio), px.val[2]);
    px.val[3] = vbsl_u8(opaque, vdup_n_u8(PX_OPAQUE), px.val[3]);
    vst4_u8((uint8_t *)o, px);
}

/* ppu_compose with NEON (ppu_neon.c). */
void ppu_compose_neon(gba_t *g, int y, const line_buffers_t *lb, uint16_t *dst);

#endif
```

Create `core/arch/arm64/ppu_neon.c`:

```c
/*
 * The compositor with NEON (milestone 7b): ppu_compose's rules (core/src/ppu/ppu_compose.c, its plain-C twin) on
 * 16 pixels at a time without windows or effects, 8 at a time with them. Each layer's line buffer is read with
 * vld4, which splits the 4-byte pixels into colour-low, colour-high, priority and flags lanes.
 */
#include "arm64/ppu_neon.h"

/* WINxH / WINxV hold start << 8 | end (end exclusive); start > end wraps around: which of the x lanes are inside. */
static inline uint8x8_t in_span8(uint16_t reg, uint8x8_t x) {
    uint8x8_t start = vdup_n_u8((uint8_t)(reg >> 8)), end = vdup_n_u8((uint8_t)reg);
    uint8x8_t ge = vcge_u8(x, start), lt = vclt_u8(x, end);
    return (reg >> 8) <= (reg & 0xFF) ? vand_u8(ge, lt) : vorr_u8(ge, lt);
}

static inline uint16x8_t color8(uint8x8_t lo, uint8x8_t hi) {
    return vorrq_u16(vmovl_u8(lo), vshlq_n_u16(vmovl_u8(hi), 8));
}

static inline uint16x8_t wide8(uint8x8_t m) {
    return vreinterpretq_u16_s16(vmovl_s8(vreinterpret_s8_u8(m))); /* 0xFF -> 0xFFFF */
}

static inline uint16x8_t blend8(uint16x8_t a, uint16x8_t b, uint16x8_t eva, uint16x8_t evb) {
    uint16x8_t m = vdupq_n_u16(31), out = vdupq_n_u16(0);
    for (int s = 0; s < 15; s += 5) {
        uint16x8_t ca = vandq_u16(vshlq_u16(a, vdupq_n_s16((int16_t)-s)), m);
        uint16x8_t cb = vandq_u16(vshlq_u16(b, vdupq_n_s16((int16_t)-s)), m);
        uint16x8_t c = vshrq_n_u16(vmlaq_u16(vmulq_u16(ca, eva), cb, evb), 4);
        out = vorrq_u16(out, vshlq_u16(vminq_u16(c, m), vdupq_n_s16((int16_t)s)));
    }
    return out;
}

static inline uint16x8_t brighten8(uint16x8_t a, uint16x8_t evy) {
    uint16x8_t m = vdupq_n_u16(31), out = vdupq_n_u16(0);
    for (int s = 0; s < 15; s += 5) {
        uint16x8_t c = vandq_u16(vshlq_u16(a, vdupq_n_s16((int16_t)-s)), m);
        c = vaddq_u16(c, vshrq_n_u16(vmulq_u16(vsubq_u16(m, c), evy), 4));
        out = vorrq_u16(out, vshlq_u16(c, vdupq_n_s16((int16_t)s)));
    }
    return out;
}

static inline uint16x8_t darken8(uint16x8_t a, uint16x8_t evy) {
    uint16x8_t m = vdupq_n_u16(31), out = vdupq_n_u16(0);
    for (int s = 0; s < 15; s += 5) {
        uint16x8_t c = vandq_u16(vshlq_u16(a, vdupq_n_s16((int16_t)-s)), m);
        c = vsubq_u16(c, vshrq_n_u16(vmulq_u16(c, evy), 4));
        out = vorrq_u16(out, vshlq_u16(c, vdupq_n_s16((int16_t)s)));
    }
    return out;
}

static uint32_t cap16(uint32_t v) {
    return v > 16 ? 16 : v;
}

/* No windows, no effects, no semi-transparent sprite: the front-most opaque pixel, 16 at a time. Keys as in
 * find_top: priority * 8 + (OBJ 0, BGn n + 1); 64 for nothing. */
static void compose_plain(const line_buffers_t *lb, uint16_t backdrop, uint16_t *dst) {
    for (int x = 0; x < PPU_WIDTH; x += 16) {
        uint8x16x4_t o = vld4q_u8((const uint8_t *)&lb->obj[x]);
        uint8x16_t op = vtstq_u8(o.val[3], vdupq_n_u8(PX_OPAQUE));
        uint8x16_t best = vbslq_u8(op, vshlq_n_u8(o.val[2], 3), vdupq_n_u8(64));
        uint8x16_t lo = vbslq_u8(op, o.val[0], vdupq_n_u8((uint8_t)backdrop));
        uint8x16_t hi = vbslq_u8(op, o.val[1], vdupq_n_u8((uint8_t)(backdrop >> 8)));
        for (int bg = 0; bg < 4; bg++) {
            uint8x16x4_t p = vld4q_u8((const uint8_t *)&lb->bg[bg][x]);
            uint8x16_t key = vaddq_u8(vshlq_n_u8(p.val[2], 3), vdupq_n_u8((uint8_t)(bg + 1)));
            uint8x16_t take = vandq_u8(vtstq_u8(p.val[3], vdupq_n_u8(PX_OPAQUE)), vcltq_u8(key, best));
            best = vbslq_u8(take, key, best);
            lo = vbslq_u8(take, p.val[0], lo);
            hi = vbslq_u8(take, p.val[1], hi);
        }
        uint8x16x2_t out = {{lo, hi}};
        vst2q_u8((uint8_t *)&dst[x], out);
    }
}

void ppu_compose_neon(gba_t *g, int y, const line_buffers_t *lb, uint16_t *dst) {
    uint16_t dispcnt = g->ppu.dispcnt;
    uint16_t winin = ppu_reg(g, 0x048), winout = ppu_reg(g, 0x04A);
    uint16_t win0h = ppu_reg(g, 0x040), win1h = ppu_reg(g, 0x042);
    uint16_t win0v = ppu_reg(g, 0x044), win1v = ppu_reg(g, 0x046);
    int sv0 = win0v >> 8, ev0 = win0v & 0xFF, sv1 = win1v >> 8, ev1 = win1v & 0xFF;
    bool win0 = (dispcnt & 0x2000) && (sv0 <= ev0 ? y >= sv0 && y < ev0 : y >= sv0 || y < ev0);
    bool win1 = (dispcnt & 0x4000) && (sv1 <= ev1 ? y >= sv1 && y < ev1 : y >= sv1 || y < ev1);
    bool objwin = (dispcnt & 0x8000) != 0;
    bool windowing = (dispcnt & 0xE000) != 0;
    uint16_t bldcnt = ppu_reg(g, 0x050), bldalpha = ppu_reg(g, 0x052);
    uint32_t mode = (bldcnt >> 6) & 3;
    uint16_t backdrop = pal_color(g, 0);

    if (!windowing && mode == 0) {
        uint8x16_t semi = vdupq_n_u8(0);
        for (int x = 0; x < PPU_WIDTH; x += 16) semi = vorrq_u8(semi, vld4q_u8((const uint8_t *)&lb->obj[x]).val[3]);
        if (!(vmaxvq_u8(vandq_u8(semi, vdupq_n_u8(PX_SEMI))))) {
            compose_plain(lb, backdrop, dst);
            return;
        }
    }

    uint16x8_t eva = vdupq_n_u16((uint16_t)cap16(bldalpha & 0x1F)), evb = vdupq_n_u16((uint16_t)cap16((bldalpha >> 8) & 0x1F));
    uint16x8_t evy = vdupq_n_u16((uint16_t)cap16(ppu_reg(g, 0x054) & 0x1F));
    /* Which layers (0-3 BGs, 4 OBJ, 5 backdrop) are first and second targets: 0xFF / 0 by layer number. */
    uint8_t first_tab[16] = {0}, second_tab[16] = {0};
    for (int l = 0; l < 6; l++) {
        first_tab[l] = ((bldcnt >> l) & 1) ? 0xFF : 0;
        second_tab[l] = ((bldcnt >> (8 + l)) & 1) ? 0xFF : 0;
    }
    uint8x16_t first_t = vld1q_u8(first_tab), second_t = vld1q_u8(second_tab);
    static const uint8_t iota[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    uint8x8_t lanes = vld1_u8(iota);
    uint8x8_t m_in0 = vdup_n_u8(winin & 0x3F), m_in1 = vdup_n_u8((winin >> 8) & 0x3F);
    uint8x8_t m_objw = vdup_n_u8((winout >> 8) & 0x3F), m_out = vdup_n_u8(winout & 0x3F);
    uint8x8_t m_all = vdup_n_u8(64), bd = vdup_n_u8(LAYER_BD);
    uint16x8_t back = vdupq_n_u16(backdrop);

    for (int x = 0; x < PPU_WIDTH; x += 8) {
        uint8x8_t mask = vdup_n_u8(0x3F); /* bits 0-3 BG0-BG3, bit 4 OBJ, bit 5 colour effects */
        if (windowing) {
            uint8x8_t xv = vadd_u8(vdup_n_u8((uint8_t)x), lanes);
            uint8x8_t in0 = win0 ? in_span8(win0h, xv) : vdup_n_u8(0);
            uint8x8_t in1 = win1 ? in_span8(win1h, xv) : vdup_n_u8(0);
            uint8x8_t ow = objwin ? vtst_u8(vld1_u8((const uint8_t *)&lb->objwin[x]), vdup_n_u8(0xFF)) : vdup_n_u8(0);
            mask = vbsl_u8(in0, m_in0, vbsl_u8(in1, m_in1, vbsl_u8(ow, m_objw, m_out)));
        }
        /* The two front-most visible pixels (find_top_two): keys, layers and colours. */
        uint8x8x4_t o = vld4_u8((const uint8_t *)&lb->obj[x]);
        uint8x8_t vis = vand_u8(vtst_u8(mask, vdup_n_u8(0x10)), vtst_u8(o.val[3], vdup_n_u8(PX_OPAQUE)));
        uint8x8_t key0 = vbsl_u8(vis, vshl_n_u8(o.val[2], 3), m_all), key1 = m_all;
        uint8x8_t layer0 = vbsl_u8(vis, vdup_n_u8(LAYER_OBJ), bd), layer1 = bd;
        uint16x8_t c0 = vbslq_u16(wide8(vis), color8(o.val[0], o.val[1]), back), c1 = back;
        for (int bg = 0; bg < 4; bg++) {
            uint8x8x4_t p = vld4_u8((const uint8_t *)&lb->bg[bg][x]);
            uint8x8_t v = vand_u8(vtst_u8(mask, vdup_n_u8((uint8_t)(1 << bg))), vtst_u8(p.val[3], vdup_n_u8(PX_OPAQUE)));
            uint8x8_t key = vbsl_u8(v, vadd_u8(vshl_n_u8(p.val[2], 3), vdup_n_u8((uint8_t)(bg + 1))), m_all);
            uint8x8_t t0 = vclt_u8(key, key0), t1 = vbic_u8(vclt_u8(key, key1), t0);
            uint16x8_t col = color8(p.val[0], p.val[1]), w0 = wide8(t0), w1 = wide8(t1);
            key1 = vbsl_u8(t0, key0, vbsl_u8(t1, key, key1));
            layer1 = vbsl_u8(t0, layer0, vbsl_u8(t1, vdup_n_u8((uint8_t)bg), layer1));
            c1 = vbslq_u16(w0, c0, vbslq_u16(w1, col, c1));
            key0 = vbsl_u8(t0, key, key0);
            layer0 = vbsl_u8(t0, vdup_n_u8((uint8_t)bg), layer0);
            c0 = vbslq_u16(w0, col, c0);
        }
        uint8x8_t semi = vand_u8(vceq_u8(layer0, vdup_n_u8(LAYER_OBJ)), vtst_u8(o.val[3], vdup_n_u8(PX_SEMI)));
        /* The colour effect (ppu_compose's order): a semi-transparent sprite blends with a second target; otherwise a
         * first target takes the mode's effect (alpha blending only with a second target). */
        uint8x8_t eff = vtst_u8(mask, vdup_n_u8(0x20));
        uint8x8_t first = vqtbl1_u8(first_t, layer0), second = vqtbl1_u8(second_t, layer1);
        uint8x8_t semi_second = vand_u8(semi, second);
        uint8x8_t by_mode = vbic_u8(vand_u8(eff, first), semi_second);
        uint8x8_t use_blend = vand_u8(eff, vorr_u8(semi_second, mode == 1 ? vand_u8(by_mode, second) : vdup_n_u8(0)));
        uint16x8_t out = c0;
        if (vmaxv_u8(use_blend)) out = vbslq_u16(wide8(use_blend), blend8(c0, c1, eva, evb), out);
        if (mode == 2 && vmaxv_u8(by_mode)) out = vbslq_u16(wide8(by_mode), brighten8(c0, evy), out);
        if (mode == 3 && vmaxv_u8(by_mode)) out = vbslq_u16(wide8(by_mode), darken8(c0, evy), out);
        vst1q_u16(&dst[x], out);
    }
}
```

Replace `core/src/ppu/ppu_internal.h`:

```c
#ifndef GBA_PPU_INTERNAL_H
#define GBA_PPU_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "gba_internal.h"

enum { LAYER_BG0, LAYER_BG1, LAYER_BG2, LAYER_BG3, LAYER_OBJ, LAYER_BD };

enum {
    PX_OPAQUE = 1 << 0, /* the layer drew a visible pixel here */
    PX_SEMI = 1 << 1,   /* semi-transparent sprite pixel */
};

typedef struct {
    uint16_t color; /* BGR555 */
    uint8_t prio;   /* 0 (front) .. 3 */
    uint8_t flags;  /* PX_* */
} layer_px_t;

typedef struct {
    layer_px_t bg[4][PPU_WIDTH];
    layer_px_t obj[PPU_WIDTH];
    bool objwin[PPU_WIDTH]; /* covered by an OBJ-window sprite */
} line_buffers_t;

/* Little-endian reads from the I/O backing store, VRAM, OAM and palette RAM. */
static inline uint16_t ppu_reg(const gba_t *g, uint32_t off) {
    return (uint16_t)(g->io.raw[off] | g->io.raw[off + 1] << 8);
}

static inline uint32_t ppu_reg32(const gba_t *g, uint32_t off) {
    return ppu_reg(g, off) | (uint32_t)ppu_reg(g, off + 2) << 16;
}

static inline uint16_t vram16(const gba_t *g, uint32_t off) {
    return (uint16_t)(g->bus.vram[off] | g->bus.vram[off + 1] << 8);
}

static inline uint16_t oam16(const gba_t *g, uint32_t off) {
    return (uint16_t)(g->bus.oam[off] | g->bus.oam[off + 1] << 8);
}

static inline uint16_t pal_color(const gba_t *g, uint32_t index) {
    return (uint16_t)((g->bus.pal[index * 2] | g->bus.pal[index * 2 + 1] << 8) & 0x7FFF);
}

static inline void put_px(layer_px_t *px, uint16_t color, uint8_t prio, uint8_t flags) {
    px->color = color;
    px->prio = prio;
    px->flags = flags;
}

/* ppu_bg.c */
void ppu_render_text_bg(gba_t *g, int bg, int y, layer_px_t *out);
void ppu_render_affine_bg(gba_t *g, int bg, int y, layer_px_t *out);
void ppu_render_bitmap_bg(gba_t *g, int mode, int y, layer_px_t *out);
/* ppu_obj.c */
void ppu_render_obj(gba_t *g, int y, layer_px_t *out, bool *objwin);
/* ppu_compose.c: on arm64 the NEON compositor (core/arch/arm64/ppu_neon.c), elsewhere its plain-C twin */
void ppu_compose(gba_t *g, int y, const line_buffers_t *lb, uint16_t *dst);
void ppu_compose_generic(gba_t *g, int y, const line_buffers_t *lb, uint16_t *dst);

#endif
```

Replace `core/src/ppu/ppu_compose.c`:

```c
#include "ppu/ppu_internal.h"
#ifdef GBA_ARM64
#include "arm64/ppu_neon.h"
#endif

/*
 * The two front-most visible pixels at x. Front to back: lower priority value first;
 * at equal priority OBJ is in front of BG0, BG0 of BG1, and so on. The backdrop is last.
 * Each layer is looked at once; its place in that order is the key prio * 8 + (OBJ 0, BGn n + 1).
 */
static void find_top_two(const line_buffers_t *lb, int x, uint32_t visible, uint16_t backdrop, int layer[2],
                         uint16_t color[2], bool *semi) {
    unsigned key0 = 64, key1 = 64;
    layer[0] = layer[1] = LAYER_BD;
    color[0] = color[1] = backdrop;
    const layer_px_t *o = &lb->obj[x];
    bool obj_first = false;
    if ((visible & (1u << LAYER_OBJ)) && (o->flags & PX_OPAQUE)) {
        key0 = (unsigned)o->prio * 8;
        layer[0] = LAYER_OBJ;
        color[0] = o->color;
        obj_first = true;
    }
    for (int bg = 0; bg < 4; bg++) {
        const layer_px_t *p = &lb->bg[bg][x];
        if (!(visible & (1u << bg)) || !(p->flags & PX_OPAQUE)) continue;
        unsigned key = (unsigned)p->prio * 8 + (unsigned)bg + 1;
        if (key < key0) {
            key1 = key0;
            layer[1] = layer[0];
            color[1] = color[0];
            key0 = key;
            layer[0] = bg;
            color[0] = p->color;
            obj_first = false;
        } else if (key < key1) {
            key1 = key;
            layer[1] = bg;
            color[1] = p->color;
        }
    }
    *semi = obj_first && (o->flags & PX_SEMI) != 0;
}

/* The front-most pixel at x when nothing blends or windows: every layer is visible. */
static uint16_t find_top(const line_buffers_t *lb, int x, uint16_t backdrop) {
    unsigned best = 64;
    uint16_t color = backdrop;
    const layer_px_t *o = &lb->obj[x];
    if (o->flags & PX_OPAQUE) {
        best = (unsigned)o->prio * 8;
        color = o->color;
    }
    for (int bg = 0; bg < 4; bg++) {
        const layer_px_t *p = &lb->bg[bg][x];
        unsigned key = (unsigned)p->prio * 8 + (unsigned)bg + 1;
        if ((p->flags & PX_OPAQUE) && key < best) {
            best = key;
            color = p->color;
        }
    }
    return color;
}

/* WINxH / WINxV hold start << 8 | end (end exclusive); start > end wraps around. */
static bool in_span(uint16_t reg, int v) {
    int start = reg >> 8, end = reg & 0xFF;
    if (start <= end) return v >= start && v < end;
    return v >= start || v < end;
}

static uint32_t cap16(uint32_t v) {
    return v > 16 ? 16 : v;
}

static uint16_t blend(uint16_t a, uint16_t b, uint32_t eva, uint32_t evb) {
    uint16_t out = 0;
    for (int s = 0; s < 15; s += 5) {
        uint32_t c = (((a >> s) & 31u) * eva + ((b >> s) & 31u) * evb) >> 4;
        out |= (uint16_t)((c > 31 ? 31 : c) << s);
    }
    return out;
}

static uint16_t brighten(uint16_t a, uint32_t evy) {
    uint16_t out = 0;
    for (int s = 0; s < 15; s += 5) {
        uint32_t c = (a >> s) & 31u;
        out |= (uint16_t)((c + (((31 - c) * evy) >> 4)) << s);
    }
    return out;
}

static uint16_t darken(uint16_t a, uint32_t evy) {
    uint16_t out = 0;
    for (int s = 0; s < 15; s += 5) {
        uint32_t c = (a >> s) & 31u;
        out |= (uint16_t)((c - ((c * evy) >> 4)) << s);
    }
    return out;
}

void ppu_compose_generic(gba_t *g, int y, const line_buffers_t *lb, uint16_t *dst) {
    uint16_t dispcnt = g->ppu.dispcnt;
    uint16_t winin = ppu_reg(g, 0x048), winout = ppu_reg(g, 0x04A);
    uint16_t win0h = ppu_reg(g, 0x040), win1h = ppu_reg(g, 0x042);
    bool win0 = (dispcnt & 0x2000) && in_span(ppu_reg(g, 0x044), y);
    bool win1 = (dispcnt & 0x4000) && in_span(ppu_reg(g, 0x046), y);
    bool objwin = (dispcnt & 0x8000) != 0;
    bool windowing = (dispcnt & 0xE000) != 0;
    uint16_t bldcnt = ppu_reg(g, 0x050), bldalpha = ppu_reg(g, 0x052);
    uint32_t mode = (bldcnt >> 6) & 3;
    uint32_t eva = cap16(bldalpha & 0x1F), evb = cap16((bldalpha >> 8) & 0x1F);
    uint32_t evy = cap16(ppu_reg(g, 0x054) & 0x1F);
    uint16_t backdrop = pal_color(g, 0);

    /* No windows and no colour effects: only semi-transparent sprites could still blend. */
    if (!windowing && mode == 0) {
        bool semi_obj = false;
        for (int x = 0; x < PPU_WIDTH && !semi_obj; x++) semi_obj = (lb->obj[x].flags & PX_SEMI) != 0;
        if (!semi_obj) {
            for (int x = 0; x < PPU_WIDTH; x++) dst[x] = find_top(lb, x, backdrop);
            return;
        }
    }

    for (int x = 0; x < PPU_WIDTH; x++) {
        uint32_t mask = 0x3F; /* bits 0-3 BG0-BG3, bit 4 OBJ, bit 5 colour effects */
        if (windowing) {
            if (win0 && in_span(win0h, x)) mask = winin & 0x3F;
            else if (win1 && in_span(win1h, x)) mask = (winin >> 8) & 0x3F;
            else if (objwin && lb->objwin[x]) mask = (winout >> 8) & 0x3F;
            else mask = winout & 0x3F;
        }

        int layer[2];
        uint16_t color[2];
        bool semi;
        find_top_two(lb, x, mask, backdrop, layer, color, &semi);

        uint16_t out = color[0];
        if (mask & 0x20) {
            bool first = (bldcnt >> layer[0]) & 1;
            bool second = (bldcnt >> (8 + layer[1])) & 1;
            if (semi && second) {
                out = blend(color[0], color[1], eva, evb); /* semi-transparent sprites always blend */
            } else if (first) {
                if (mode == 1 && second) out = blend(color[0], color[1], eva, evb);
                else if (mode == 2) out = brighten(color[0], evy);
                else if (mode == 3) out = darken(color[0], evy);
            }
        }
        dst[x] = out;
    }
}

void ppu_compose(gba_t *g, int y, const line_buffers_t *lb, uint16_t *dst) {
#ifdef GBA_ARM64
    ppu_compose_neon(g, y, lb, dst);
#else
    ppu_compose_generic(g, y, lb, dst);
#endif
}
```

Replace `core/src/ppu/ppu_bg.c`:

```c
#include "ppu/ppu_internal.h"
#ifdef GBA_ARM64
#include "arm64/ppu_neon.h"
#endif

/* BG mosaic block size (1 = off) from MOSAIC when the BG's mosaic bit is set. */
static void bg_mosaic(const gba_t *g, uint16_t cnt, int *mh, int *mv) {
    uint16_t m = ppu_reg(g, 0x04C);
    bool on = (cnt & 0x40) != 0;
    *mh = on ? (m & 0xF) + 1 : 1;
    *mv = on ? ((m >> 4) & 0xF) + 1 : 1;
}

/* Line y of a text BG with horizontal mosaic: pixel by pixel (each block repeats its first pixel). */
static void text_bg_mosaic(gba_t *g, uint16_t cnt, uint32_t hofs, uint32_t py, int mh, layer_px_t *out) {
    uint8_t prio = cnt & 3;
    uint32_t char_base = ((cnt >> 2) & 3) * 0x4000u;
    bool bpp8 = (cnt & 0x80) != 0;
    uint32_t screen_base = ((cnt >> 8) & 0x1F) * 0x800u;
    uint32_t width = (cnt & 0x4000) ? 512 : 256;
    for (int x = 0; x < PPU_WIDTH; x++) {
        uint32_t px = ((uint32_t)(x - x % mh) + hofs) & (width - 1);
        /* Maps wider or taller than 256 are 32x32-entry screen blocks: left to right, then top to bottom. */
        uint32_t block = (px >> 8) + (py >> 8) * (width >> 8);
        uint16_t entry = vram16(g, screen_base + block * 0x800 + ((py >> 3) & 31) * 64 + ((px >> 3) & 31) * 2);
        uint32_t tile = entry & 0x3FF;
        uint32_t tx = (entry & 0x400) ? 7 - (px & 7) : px & 7;
        uint32_t ty = (entry & 0x800) ? 7 - (py & 7) : py & 7;
        uint32_t index;
        if (bpp8) {
            uint32_t addr = char_base + tile * 64 + ty * 8 + tx;
            if (addr >= 0x10000) continue; /* backgrounds cannot read sprite VRAM */
            index = g->bus.vram[addr];
        } else {
            uint32_t addr = char_base + tile * 32 + ty * 4 + tx / 2;
            if (addr >= 0x10000) continue;
            index = (tx & 1) ? g->bus.vram[addr] >> 4 : g->bus.vram[addr] & 0xFu;
            if (index) index |= (uint32_t)(entry >> 12) << 4;
        }
        if (!index) continue;
        put_px(&out[x], pal_color(g, index), prio, PX_OPAQUE);
    }
}

void ppu_render_text_bg(gba_t *g, int bg, int y, layer_px_t *out) {
    uint16_t cnt = ppu_reg(g, 0x008 + (uint32_t)bg * 2);
    uint32_t hofs = ppu_reg(g, 0x010 + (uint32_t)bg * 4) & 0x1FF;
    uint32_t vofs = ppu_reg(g, 0x012 + (uint32_t)bg * 4) & 0x1FF;
    uint32_t height = (cnt & 0x8000) ? 512 : 256;
    int mh, mv;
    bg_mosaic(g, cnt, &mh, &mv);
    uint32_t py = ((uint32_t)(y - y % mv) + vofs) & (height - 1);
    if (mh > 1) {
        text_bg_mosaic(g, cnt, hofs, py, mh, out);
        return;
    }

    /* A tile row at a time: its map entry and its row of pixel data are read once. */
    uint8_t prio = cnt & 3;
    uint32_t char_base = ((cnt >> 2) & 3) * 0x4000u;
    bool bpp8 = (cnt & 0x80) != 0;
    uint32_t screen_base = ((cnt >> 8) & 0x1F) * 0x800u;
    uint32_t width = (cnt & 0x4000) ? 512 : 256;
    uint32_t row_entries = screen_base + (py >> 8) * (width >> 8) * 0x800 + ((py >> 3) & 31) * 64;
    for (int x = 0; x < PPU_WIDTH;) {
        uint32_t px = ((uint32_t)x + hofs) & (width - 1);
        uint32_t first = px & 7;
        int n = 8 - (int)first;
        if (n > PPU_WIDTH - x) n = PPU_WIDTH - x;
        uint16_t entry = vram16(g, row_entries + (px >> 8) * 0x800 + ((px >> 3) & 31) * 2);
        uint32_t tile = entry & 0x3FF;
        bool hflip = (entry & 0x400) != 0;
        uint32_t ty = (entry & 0x800) ? 7 - (py & 7) : py & 7;
        layer_px_t *o = &out[x];
        x += n;
        if (bpp8) {
            uint32_t addr = char_base + tile * 64 + ty * 8;
            if (addr >= 0x10000) continue; /* backgrounds cannot read sprite VRAM (a row never straddles it) */
            const uint8_t *row = &g->bus.vram[addr];
            for (int i = 0; i < n; i++) {
                uint32_t tx = first + (uint32_t)i;
                uint32_t index = row[hflip ? 7 - tx : tx];
                if (index) put_px(&o[i], pal_color(g, index), prio, PX_OPAQUE);
            }
        } else {
            uint32_t addr = char_base + tile * 32 + ty * 4;
            if (addr >= 0x10000) continue;
            const uint8_t *b = &g->bus.vram[addr];
            uint32_t row = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
            if (!row) continue; /* a transparent tile row */
            uint32_t bank = (uint32_t)(entry >> 12) << 4;
#ifdef GBA_ARM64
            if (n == 8) { /* a whole row: NEON (its twin is the loop below) */
                ppu_tile_row4_neon(o, row, hflip, &g->bus.pal[bank * 2], prio);
                continue;
            }
#endif
            for (int i = 0; i < n; i++) {
                uint32_t tx = first + (uint32_t)i;
                uint32_t index = (row >> ((hflip ? 7 - tx : tx) * 4)) & 0xF;
                if (index) put_px(&o[i], pal_color(g, bank | index), prio, PX_OPAQUE);
            }
        }
    }
}

typedef struct {
    int32_t pa, pc; /* texture step per screen pixel (8.8) */
    int32_t x0, y0; /* texture position of screen pixel 0 (20.8) */
    int mh;         /* horizontal mosaic block */
} affine_line_t;

/* Texture walk for line y of BG2/BG3. Vertical mosaic reuses the first line of the block by
 * stepping the reference back (exact while the parameters stay constant over the block). */
static affine_line_t affine_line(const gba_t *g, int bg, int y, uint16_t cnt) {
    uint32_t params = bg == 2 ? 0x020 : 0x030;
    affine_line_t a;
    int mv;
    bg_mosaic(g, cnt, &a.mh, &mv);
    int back = y % mv;
    a.pa = (int16_t)ppu_reg(g, params);
    a.pc = (int16_t)ppu_reg(g, params + 4);
    a.x0 = g->ppu.ref_x[bg - 2] - back * (int16_t)ppu_reg(g, params + 2);
    a.y0 = g->ppu.ref_y[bg - 2] - back * (int16_t)ppu_reg(g, params + 6);
    return a;
}

void ppu_render_affine_bg(gba_t *g, int bg, int y, layer_px_t *out) {
    uint16_t cnt = ppu_reg(g, 0x008 + (uint32_t)bg * 2);
    uint8_t prio = cnt & 3;
    uint32_t char_base = ((cnt >> 2) & 3) * 0x4000u;
    uint32_t screen_base = ((cnt >> 8) & 0x1F) * 0x800u;
    int32_t size = 128 << ((cnt >> 14) & 3);
    bool wrap = (cnt & 0x2000) != 0;
    affine_line_t a = affine_line(g, bg, y, cnt);

    for (int x = 0; x < PPU_WIDTH; x++) {
        int32_t sx = x - x % a.mh;
        int32_t tx = (a.x0 + a.pa * sx) >> 8, ty = (a.y0 + a.pc * sx) >> 8;
        if (wrap) {
            tx &= size - 1;
            ty &= size - 1;
        } else if (tx < 0 || ty < 0 || tx >= size || ty >= size) {
            continue;
        }
        uint32_t map = screen_base + (uint32_t)(ty >> 3) * (uint32_t)(size >> 3) + (uint32_t)(tx >> 3);
        uint32_t tile = g->bus.vram[map & 0xFFFF];
        uint32_t addr = char_base + tile * 64 + (uint32_t)(ty & 7) * 8 + (uint32_t)(tx & 7);
        if (addr >= 0x10000) continue;
        uint32_t index = g->bus.vram[addr];
        if (!index) continue;
        put_px(&out[x], pal_color(g, index), prio, PX_OPAQUE);
    }
}

/* Modes 3-5 draw BG2 as a bitmap sampled through the BG2 affine transform. */
void ppu_render_bitmap_bg(gba_t *g, int mode, int y, layer_px_t *out) {
    uint16_t cnt = ppu_reg(g, 0x00C);
    uint8_t prio = cnt & 3;
    uint32_t page = (mode != 3 && (g->ppu.dispcnt & 0x10)) ? 0xA000 : 0;
    int32_t w = mode == 5 ? 160 : 240, h = mode == 5 ? 128 : 160;
    affine_line_t a = affine_line(g, 2, y, cnt);

    if (a.mh == 1 && a.pc == 0) { /* not rotated: the whole line is one row of the bitmap */
        int32_t ty = a.y0 >> 8;
        if (ty < 0 || ty >= h) return;
        for (int x = 0; x < PPU_WIDTH; x++) {
            int32_t tx = (a.x0 + a.pa * x) >> 8;
            if (tx < 0 || tx >= w) continue;
            uint32_t pos = (uint32_t)(ty * w + tx);
            if (mode == 4) {
                uint32_t index = g->bus.vram[page + pos];
                if (index) put_px(&out[x], pal_color(g, index), prio, PX_OPAQUE);
            } else {
                put_px(&out[x], vram16(g, page + pos * 2) & 0x7FFF, prio, PX_OPAQUE);
            }
        }
        return;
    }

    for (int x = 0; x < PPU_WIDTH; x++) {
        int32_t sx = x - x % a.mh;
        int32_t tx = (a.x0 + a.pa * sx) >> 8, ty = (a.y0 + a.pc * sx) >> 8;
        if (tx < 0 || ty < 0 || tx >= w || ty >= h) continue;
        uint32_t pos = (uint32_t)(ty * w + tx);
        uint16_t color;
        if (mode == 4) {
            uint32_t index = g->bus.vram[page + pos];
            if (!index) continue;
            color = pal_color(g, index);
        } else {
            color = vram16(g, page + pos * 2) & 0x7FFF;
        }
        put_px(&out[x], color, prio, PX_OPAQUE);
    }
}
```

- [x] **Step 3: The drawing tests, all tests and the scenes**

```bash
timeout 550 bash tools/dev.sh phone-unit speedup 2>&1 | tail -1
timeout 550 bash tools/dev.sh phone-unit 2>&1 | tail -1
timeout 550 env GENERIC=1 bash tools/dev.sh phone-unit 2>&1 | tail -1
bash tools/dev.sh phone-bench --runs 3 --check
```
```bash
bash tools/dev.sh unit 2>&1 | tail -1
bash tools/dev.sh test 2>&1 | grep -E "tests passed|tests failed"
bash tools/dev.sh bench --runs 1 --check
```

Expected: `6 tests, 0 failed` (now the NEON); `414 tests, 0 failed`; `412 tests, 0 failed`; 6 scenes `ok ok` on phone B; on the PC `412 tests, 0 failed`, CTest 120/120, 6 scenes `ok ok`. (In the prototype a darken that brightened and a dropped tile flip each made a drawing test fail.)

- [x] **Step 4: Commit**

```bash
git add core/arch/arm64/ppu_neon.h core/arch/arm64/ppu_neon.c core/src/ppu/ppu_internal.h core/src/ppu/ppu_compose.c core/src/ppu/ppu_bg.c docs/PROGRESS.md docs/superpowers/plans/2026-10-03-milestone-7b-asm.md
git commit -m "perf(arm64): NEON compositor and 4bpp tile rows" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 5: The sensor test on a real phone, and the speed on phone B

**Files:**
- Modify: `app/src/androidTest/java/com/tengen/gbaemu/MotionTest.kt` (`sensorValuesReachTheCoreWithinAFrame` waits up to 2 s for the first frame — `seenSoon` —, then 100 ms for the second values: it gave the game 100 ms after `start()`, and run alone it failed 8-9 times in 10 on phone B, **with `main`'s code too**; in the full suite an earlier test has the sound running already)

- [x] **Step 1: Write the code**

Replace `app/src/androidTest/java/com/tengen/gbaemu/MotionTest.kt`:

```kotlin
package com.tengen.gbaemu

import android.os.SystemClock
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.After
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Cartridge extras on the phone: which ones a cartridge has, sensor values reaching the core, the rumble motor. */
@RunWith(AndroidJUnit4::class)
class MotionTest {
    @After
    fun stopSession() = NativeBridge.stop()

    private fun load(rom: String) = assertEquals(0, NativeBridge.loadRom(TestRoms.rom(rom)))

    @Test
    fun cartridgesReportTheirExtras() {
        load("../own_roms/hw_gyro.gba")
        assertEquals(NativeBridge.HW_GYRO or NativeBridge.HW_RUMBLE, NativeBridge.cartHardware())
        load("../own_roms/hw_tilt.gba")
        assertEquals(NativeBridge.HW_TILT, NativeBridge.cartHardware())
        load("../own_roms/hw_solar.gba")
        assertEquals(NativeBridge.HW_RTC or NativeBridge.HW_SOLAR, NativeBridge.cartHardware())
        load("tonc/m7_demo.gba")
        assertEquals(0, NativeBridge.cartHardware())
    }

    /** What the core last saw, once it is [expected] or after 2 s (the first frames wait for the sound to start). */
    private fun seenSoon(expected: IntArray): IntArray {
        val until = SystemClock.elapsedRealtime() + 2000
        var seen = NativeBridge.debugMotionSeen()
        while (!seen.contentEquals(expected) && SystemClock.elapsedRealtime() < until) {
            Thread.sleep(5)
            seen = NativeBridge.debugMotionSeen()
        }
        return seen
    }

    @Test
    fun sensorValuesReachTheCoreWithinAFrame() {
        load("../own_roms/hw_tilt.gba")
        NativeBridge.start()
        NativeBridge.setMotion(12000, -5000, 16000)
        NativeBridge.setLight(200)
        assertArrayEquals(intArrayOf(12000, -5000, 16000, 200), seenSoon(intArrayOf(12000, -5000, 16000, 200)))
        NativeBridge.setMotion(99999, -99999, 0) // out of range: clamped
        NativeBridge.setLight(300)
        Thread.sleep(100) // the game is running now: one frame is 17 ms
        assertArrayEquals(intArrayOf(32767, -32767, 0, 255), NativeBridge.debugMotionSeen())
    }

    @Test
    fun theRumbleMotorGoesOnAndOffHalfTheTime() {
        load("../own_roms/hw_gyro.gba") // on 30 frames, off 30 frames
        NativeBridge.start()
        var on = 0
        var off = 0
        val until = SystemClock.elapsedRealtime() + 2000
        while (SystemClock.elapsedRealtime() < until) {
            if (NativeBridge.rumbleActive()) on++ else off++
            Thread.sleep(5)
        }
        val share = on.toDouble() / (on + off)
        assertTrue("motor on in $on of ${on + off} samples", share in 0.35..0.65)
    }

    @Test
    fun noRumbleWithoutAMotor() {
        load("tonc/m7_demo.gba")
        NativeBridge.start()
        val until = SystemClock.elapsedRealtime() + 500
        while (SystemClock.elapsedRealtime() < until) {
            assertEquals(false, NativeBridge.rumbleActive())
            Thread.sleep(20)
        }
    }
}
```

- [x] **Step 2: On-device tests on the emulator and on phone B (both builds), the sensor test 10 times**

```bash
rm -rf app/build/outputs/androidTest-results; bash tools/dev.sh androidtest 2>&1 | grep -E "^e: |Starting|BUILD FAILED"; grep -h -o 'tests="[0-9]*" failures="[0-9]*"' app/build/outputs/androidTest-results/connected/debug/*.xml
rm -rf app/build/outputs/androidTest-results; bash tools/dev.sh phone-androidtest 2>&1 | grep -E "^e: |Starting|BUILD FAILED"; grep -h -o 'tests="[0-9]*" failures="[0-9]*"' app/build/outputs/androidTest-results/connected/debug/*.xml
ADB_EXE="$(cygpath -u "$LOCALAPPDATA")/Android/Sdk/platform-tools/adb"
for i in $(seq 1 10); do MSYS_NO_PATHCONV=1 "$ADB_EXE" -s its serial shell am instrument -w -e class com.tengen.gbaemu.MotionTest#sensorValuesReachTheCoreWithinAFrame com.tengen.gbaemu.test/androidx.test.runner.AndroidJUnitRunner | tr -d '\r' | grep -E "^OK|expected" | head -1; done | sort | uniq -c
rm -rf app/build/outputs/androidTest-results; GENERIC=1 bash tools/dev.sh phone-androidtest 2>&1 | grep -E "^e: |Starting|BUILD FAILED"; grep -h -o 'tests="[0-9]*" failures="[0-9]*"' app/build/outputs/androidTest-results/connected/debug/*.xml
echo "C warnings: $(bash tools/dev.sh apk --rerun-tasks 2>&1 | grep -c 'warning:.*\.[chS]:')"
```

Expected: `tests="53" failures="0"` on the emulator (`Pixel_9_API_36_1`), on phone B and on phone B with `GENERIC=1`; `10 OK (1 test)`; `C warnings: 0`.

- [x] **Step 3: The speed on phone B (the finish line)**

```bash
bash tools/dev.sh phone-bench --runs 3 --check
GENERIC=1 bash tools/dev.sh phone-bench --runs 1 --check
```

Expected: 6 scenes `ok ok` both ways, and with the fast build at least (spec §1): game A ≥ 4.42×, game C ≥ 6.08×, game B ≥ 3.34×, game D ≥ 3.19×, `m7_demo` ≥ 5.60×, `snd1_demo` ≥ 23.1× (the prototype: 4.65 / 6.40 / 3.52 / 3.36 / 5.89 / 24.33). A scene below its line: run once more (a warm or busy phone); still below → stop and tell the user. Record the before/after table in `docs/PROGRESS.md`.

- [x] **Step 4: Commit**

```bash
git add app/src/androidTest/java/com/tengen/gbaemu/MotionTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-03-milestone-7b-asm.md
git commit -m "test(android): the sensor test waits for the first frame (real phones)" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 6: User check and milestone 7b exit

- [x] **Step 1: Full run**

```bash
bash tools/dev.sh test && bash tools/dev.sh unit && bash tools/dev.sh jvmtest && bash tools/dev.sh androidtest && bash tools/dev.sh bench --check
```

Expected: CTest 120/120 (sanitizers on, no reports), 412 unit tests, Kotlin 84, on-device 53 (emulator), 6 scenes `ok`. Then `bash tools/dev.sh phone-install` (the branch's app on phone B, its data kept).

- [x] **Step 2: The user's check** (on phone B)

Settings → Picture → Show speed On; game A, game C and game B with sound look and sound as before, no glitches; fast-forward at Max faster than after 7a.

- [x] **Step 3: Docs and the phone**

`docs/PROGRESS.md` (7b ✅ with the before/after table and the user's check; milestone 7 ✅ — the project's milestones are all done; Next = ask the user what comes next); spec status → `Implemented — milestone 7b complete <date> (tag milestone-7b)`; parent spec status line and §10 row 7 → 7b ✅, milestone 7 ✅; `CLAUDE.md` key files → no current milestone; tick every box here. On phone B: `bash tools/dev.sh phone-clean` (removes `/data/local/tmp/gbabench`) and remove the test package — the app and the user's `Download/GBA` stay:

```bash
MSYS_NO_PATHCONV=1 "$(cygpath -u "$LOCALAPPDATA")/Android/Sdk/platform-tools/adb" -s its serial uninstall com.tengen.gbaemu.test
```

- [x] **Step 4: Commit, tag, merge** — ask the user how to integrate (so far always a fast-forward merge into `main`); then superpowers:finishing-a-development-branch, `bash tools/dev.sh test` and `bash tools/dev.sh androidtest` on `main`, delete the branch, record the merge in PROGRESS.md; delete the prototype and replay worktrees (`rm -rf` the folders in Git Bash, `git worktree prune`, `git branch -D proto-7b replay-7b`) with the user's OK.

```bash
git add -A docs CLAUDE.md
git commit -m "docs: milestone 7b complete - ARM64 assembly and NEON" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git tag milestone-7b
```
