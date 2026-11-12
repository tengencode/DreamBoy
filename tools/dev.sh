#!/usr/bin/env bash
# Host build helper (Git Bash on Windows).
#   tools/dev.sh configure        configure build/host (Debug; sanitizers unless GBA_SANITIZE=OFF; the warning log)
#   tools/dev.sh build            build everything
#   tools/dev.sh unit [filter]    build, then run unit tests whose name contains filter
#   tools/dev.sh test             build, then run every CTest test
#   tools/dev.sh run <args>       build, then run tools/testrunner with args
#   tools/dev.sh view <args>      build the optimised build/release, then run its gbaview (Windows)
#   tools/dev.sh release          configure (if needed) and build build/release (Release, no sanitizers)
#   tools/dev.sh bench <args>     build/release, then gbabench (speed scenes, tests/bench/scenes.txt + scenes.local.txt) with the
#                                 games from $GBA_GAMES (default ~/Downloads); e.g. bench --check, bench --only m7_demo
#                                 bench --ra 300: with 300 made-up achievements checked every frame (milestone 14)
#   tools/dev.sh games <args>     build/check (release + the warning log), then tools/game_check.py on the games in
#                                 tests/games/games.txt (the user's from $GBA_GAMES, default ~/Downloads): a report
#                                 in build/check/report/; games --check compares with the locked hashes and logs,
#                                 games --lock <name|all> locks them, games --only <name> plays one game
# Android (Gradle project in app/; always the Pixel 9 phone emulator, never a USB phone; Gradle runs --offline):
#   tools/dev.sh apk              build the debug app (app/build/outputs/apk/debug/app-debug.apk)
#   tools/dev.sh apk-release      build the release app (app/build/outputs/apk/release/; signed with keystore.properties' key, else unsigned)
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
CHECK="$ROOT/build/check"
# build/check: like build/release plus the warning log (GBA_NOTES), for the game check only.
build_check() {
    if [ ! -f "$CHECK/build.ninja" ]; then
        cmake -S "$ROOT" -B "$CHECK" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang \
            -DGBA_SANITIZE=OFF -DGBA_NOTES=ON
    fi
    cmake --build "$CHECK" --target testrunner
}
# The speed scenes: the public list plus your own (tests/bench/scenes.local.txt, gitignored), joined into build/.
SCENES="$ROOT/build/scenes.txt"
join_scenes() { mkdir -p "$ROOT/build" && cat "$ROOT/tests/bench/scenes.txt" $( [ -f "$ROOT/tests/bench/scenes.local.txt" ] && echo "$ROOT/tests/bench/scenes.local.txt") > "$SCENES"; }
# ---- Android ----
JBR="${JBR:-/c/Program Files/Android/Android Studio/jbr}" # Android Studio's own Java, which runs Gradle (JBR=… to change)
AVD="${GBA_AVD:-Pixel_9_API_36_1}" # the phone emulator (GBA_AVD=… to use another)
APP_ID="com.tengen.dreamboy.debug"
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
    join_scenes
    adb -s "$PHONE" push "$(cygpath -w "$SCENES")" "$PHONE_DIR/scenes.txt" >/dev/null 2>&1
    local rom save
    while IFS='|' read -r _ rom save _; do
        rom=$(echo "$rom" | xargs); save=$(echo "$save" | xargs)
        for f in "$rom" "$save"; do
            [ -z "$f" ] || [ "$f" = "-" ] && continue
            case "$f" in tonc/*) phone_copy "$ROOT/tests/roms/$f" "$PHONE_DIR/roms/$f" ;;
                *) phone_copy "$GAMES/$f" "$PHONE_DIR/roms/$f" ;; esac
        done
    done < <(grep -v '^#' "$SCENES" | grep '|')
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
            -DCMAKE_C_COMPILER=clang -DGBA_SANITIZE="${GBA_SANITIZE:-ON}" -DGBA_NOTES=ON ;;
    build) cmake --build "$BUILD" ;;
    unit)  cmake --build "$BUILD" && "$BUILD/tests/unit/unit_tests" "$@" ;;
    test)  cmake --build "$BUILD" && ctest --test-dir "$BUILD" --output-on-failure "$@" ;;
    run)   cmake --build "$BUILD" && "$BUILD/tools/testrunner/testrunner" "$@" ;;
    view)  build_release && "$RELEASE/tools/viewer/gbaview" "$@" ;;
    release) build_release ;;
    apk) gradle_run :app:assembleDebug ;;
    apk-release) gradle_run :app:assembleRelease ;;
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
    bench) build_release && join_scenes && (cd "$ROOT" && "$RELEASE/tools/bench/gbabench" --scenes "$(cygpath -m "$SCENES")" --roms "$GAMES" "$@") ;;
    games) build_check && (cd "$ROOT" && python tools/game_check.py \
        --runner "$(cygpath -w "$CHECK/tools/testrunner/testrunner.exe")" --games "$(cygpath -w "$GAMES")" "$@") ;;
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
    *) echo "usage: tools/dev.sh configure|build|unit [filter]|test|run <args>|view <args>|release|apk|jvmtest|emu|install|launch|androidtest|push <file>|bench <args>|games <args>|phone-install|phone-uninstall|phone-push <file>|phone-bench <args>|phone-profile <scene>|phone-unit [filter]|phone-androidtest|phone-clean" >&2; exit 2 ;;
esac
