# Milestone 4a — Android App + Picture — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. (This project always executes plans inline with superpowers:executing-plans — see `CLAUDE.md`.)

**Goal:** An Android app (`com.tengen.gbaemu`) that opens a GBA ROM with Android's file picker and shows it full-screen at the right speed and colours on the Pixel 9 phone emulator, playable with the PC keyboard; the Android-built core reproduces all 32 PC fingerprints.

**Architecture:** Thin Kotlin shell (Compose start screen, `SurfaceView` game screen, keys, lifecycle) over a C library `libgbaemu.so` built from `core/` + `platform/android/`: an emulation thread paced at 59.7275 Hz publishes frames through a lock-free triple buffer to a render thread that owns EGL/OpenGL ES 3.0 and draws on every display refresh (`AChoreographer`). JNI carries control only.

**Tech Stack:** C11, Android NDK 27.2 (CMake 3.31.6), OpenGL ES 3.0 + EGL, Kotlin 2.2.21, Jetpack Compose (UI 1.11.4, Material3 1.4.0), Android Gradle Plugin 8.13.0, Gradle 8.14.3, JUnit 4 + androidx.test.

**Spec:** `docs/superpowers/specs/2026-09-26-milestone-4a-app-picture-design.md` (§9: the prototype this plan's code comes from)

---

## Conventions

- Git Bash from the repo root. PC side as before: `bash tools/dev.sh <configure|build|unit [filter]|test|run|view|release>`. Android side (added in Task 2): `bash tools/dev.sh <apk|jvmtest|emu|install|launch|androidtest|push <file>>`. Gradle always runs `--offline` with Android Studio's Java (`/c/Program Files/Android/Android Studio/jbr`); if it ever reports a missing artifact, **stop and ask the user before allowing a download** (project rule).
- `emu`/`install`/`androidtest`/`push` start the Pixel 9 phone emulator (AVD `Pixel_9_API_36_1`) if it is not running — its window opens on the user's screen; leave it open between tasks.
- **Only the phone emulator, never a USB phone:** the user sometimes has a real phone plugged in, and Gradle's on-device tests and `adb` would otherwise use every connected device (this happened while the plan was checked: Gradle installed, ran and uninstalled the test app on the user's phone). `dev.sh` exports `ANDROID_SERIAL=<the emulator's serial>` for every Android command; never run `./gradlew connected...`/`installDebug` or `adb` directly without it (Task 8's helpers pass `-s` explicitly).
- `adb` from Git Bash: always with `MSYS_NO_PATHCONV=1` (dev.sh does this), else `/sdcard/...` becomes a Windows path — and then local paths must be given to adb in Windows form (`cygpath -w`; fixed in `dev.sh push` during Task 9: an absolute `/c/...` path failed).
- **PC keyboard in the phone emulator:** the AVD must have `hw.keyboard=yes` (Android Studio: 'Enable keyboard input'). With `hw.keyboard=no` (how the user's AVD was set) no key from the PC keyboard reaches Android at all; `adb shell input keyevent` still works, which is why Task 8's check could not catch it. Changed with the user's OK during Task 9 (backup `config.ini.bak-before-keyboard`).
- On-device tests read the test ROMs from `tests/roms/` (gitignored). If `tests/roms/tonc` or `tests/roms/jsmolka` is missing, run `python tools/fetch_test_roms.py` first.
- Unit tests: `TEST(name)` in `tests/unit/*.c`; re-run `bash tools/dev.sh configure` after adding a test file. Test names must not equal any function name.
- One commit per task; messages end with `-m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"`. Each commit also includes `docs/PROGRESS.md` and this plan (ticked boxes) — project rule in `CLAUDE.md`.
- Branch `milestone-4a` (already created; the spec commits are on it).
- Long Python edit scripts inside a Bash heredoc sometimes fail: write the script to the scratchpad with the Write tool and run it.
- Checkpoints with the user after Tasks 3, 6 and 8; Task 9 is the user's check and the milestone exit.

## File map

```
tests/unit/test_platform.c, tests/unit/CMakeLists.txt     PC tests of the plain-C platform pieces (Task 1)
platform/android/fit_rect, frame_pacer, triple_buffer      plain C, also built on the PC (Task 1)
settings.gradle.kts, build.gradle.kts, gradle.properties,
gradlew(.bat), gradle/wrapper/, app/build.gradle.kts,
app/src/main/AndroidManifest.xml, platform/android/CMakeLists.txt, tools/dev.sh   the Android build (Task 2)
app/src/main/java/com/tengen/gbaemu/NativeBridge.kt        JNI declarations (grows Tasks 2, 4-7)
app/src/main/java/com/tengen/gbaemu/KeyMap.kt              keys → GBA buttons (Task 3)
platform/android/jni_bridge.c                              JNI entry points (grows Tasks 4-7)
platform/android/egl_ctx, video_gl, gl_readback            OpenGL ES drawing (Task 5)
platform/android/emu_thread                                emulation thread (Task 6)
platform/android/render_thread                             render thread (Task 7)
app/src/main/java/com/tengen/gbaemu/Screens.kt, MainActivity.kt   the app (Task 8; placeholder MainActivity in Task 2)
app/src/test/.../KeyMapTest.kt                             JVM tests (Task 3)
app/src/androidTest/.../NativeLibraryTest, TestRoms, FingerprintTest, PictureTest, SessionTest   on-device tests
```

---
### Task 1: Plain-C platform pieces (aspect fit, frame pacer, triple buffer)

**Files:**
- Create: `tests/unit/test_platform.c`, `platform/android/fit_rect.h`, `platform/android/fit_rect.c`, `platform/android/frame_pacer.h`, `platform/android/frame_pacer.c`, `platform/android/triple_buffer.h`, `platform/android/triple_buffer.c`
- Modify: `tests/unit/CMakeLists.txt`

- [x] **Step 1: Write the failing tests**

`tests/unit/test_platform.c`:

```c
/* The Android platform layer's plain-C pieces (platform/android), which also build on the PC. */
#include <pthread.h>
#include <stdlib.h>

#include "fit_rect.h"
#include "frame_pacer.h"
#include "test.h"
#include "triple_buffer.h"

/* ---- fit_rect ---- */

TEST(fit_rect_exact_3_to_2_fills_the_surface) {
    fit_rect_t r = fit_rect(480, 320);
    CHECK_EQ(r.x, 0);
    CHECK_EQ(r.y, 0);
    CHECK_EQ(r.w, 480);
    CHECK_EQ(r.h, 320);
}

TEST(fit_rect_wide_surface_gets_bars_left_and_right) {
    fit_rect_t r = fit_rect(720, 320);
    CHECK_EQ(r.x, 120);
    CHECK_EQ(r.y, 0);
    CHECK_EQ(r.w, 480);
    CHECK_EQ(r.h, 320);
}

TEST(fit_rect_tall_surface_gets_bars_above_and_below) {
    fit_rect_t r = fit_rect(1080, 2424); /* Pixel 9 portrait */
    CHECK_EQ(r.x, 0);
    CHECK_EQ(r.y, 852);
    CHECK_EQ(r.w, 1080);
    CHECK_EQ(r.h, 720);
}

TEST(fit_rect_odd_sizes_round_down_and_centre) {
    fit_rect_t r = fit_rect(301, 200);
    CHECK_EQ(r.w, 300);
    CHECK_EQ(r.h, 200);
    CHECK_EQ(r.x, 0);
    CHECK_EQ(r.y, 0);
    r = fit_rect(0, 100);
    CHECK_EQ(r.w, 0);
    CHECK_EQ(r.h, 0);
}

/* ---- frame pacer ---- */

TEST(pacer_first_frame_is_due_one_period_after_now) {
    frame_pacer_t p;
    pacer_reset(&p);
    CHECK_EQ(pacer_next(&p, 1000), 1000 + PACER_PERIOD_NS);
}

TEST(pacer_keeps_exact_time_over_600_frames) {
    frame_pacer_t p;
    pacer_reset(&p);
    int64_t start = 5000, deadline = pacer_next(&p, start);
    for (int i = 1; i < 600; i++) deadline = pacer_next(&p, deadline + 100); /* woke a little late each time */
    /* 600 x 16,742,706.2988 ns = 10,045,623,779.3 ns */
    CHECK_EQ(deadline - start, 10045623779LL);
}

TEST(pacer_restarts_instead_of_catching_up) {
    frame_pacer_t p;
    pacer_reset(&p);
    int64_t d = pacer_next(&p, 0);
    int64_t late = d + 4LL * PACER_PERIOD_NS; /* more than 3 frames behind */
    CHECK_EQ(pacer_next(&p, late), late + PACER_PERIOD_NS);
    int64_t d2 = pacer_next(&p, late + PACER_PERIOD_NS + 10); /* back on time: normal step */
    CHECK_EQ(d2, late + 2LL * PACER_PERIOD_NS);
}

TEST(pacer_reset_starts_again_from_now) {
    frame_pacer_t p;
    pacer_reset(&p);
    pacer_next(&p, 0);
    pacer_reset(&p);
    CHECK_EQ(pacer_next(&p, 7), 7 + PACER_PERIOD_NS);
}

/* ---- triple buffer ---- */

static triple_buffer_t *tb_new(void) {
    triple_buffer_t *tb = malloc(sizeof *tb);
    tb_init(tb);
    return tb;
}

TEST(tb_nothing_published_is_not_fresh) {
    triple_buffer_t *tb = tb_new();
    bool fresh = true;
    tb_take(tb, &fresh);
    CHECK(!fresh);
    free(tb);
}

TEST(tb_consumer_gets_the_published_frame_once) {
    triple_buffer_t *tb = tb_new();
    tb_back(tb)[0] = 0x1234;
    tb_publish(tb);
    bool fresh = false;
    const uint16_t *f = tb_take(tb, &fresh);
    CHECK(fresh);
    CHECK_EQ(f[0], 0x1234);
    f = tb_take(tb, &fresh);
    CHECK(!fresh);
    CHECK_EQ(f[0], 0x1234); /* the same frame again */
    free(tb);
}

TEST(tb_newest_frame_wins) {
    triple_buffer_t *tb = tb_new();
    tb_back(tb)[0] = 1;
    tb_publish(tb);
    tb_back(tb)[0] = 2;
    tb_publish(tb);
    bool fresh;
    CHECK_EQ(tb_take(tb, &fresh)[0], 2);
    CHECK(fresh);
    free(tb);
}

TEST(tb_producer_never_writes_the_consumers_frame) {
    triple_buffer_t *tb = tb_new();
    bool fresh;
    for (int i = 0; i < 20; i++) {
        tb_publish(tb);
        if (i % 3 == 0) tb_take(tb, &fresh);
        CHECK(tb_back(tb) != tb->slot[tb->front]);
    }
    free(tb);
}

/* Two real threads: every frame the consumer sees is whole (all pixels equal) and never older. */
static void *tb_producer(void *arg) {
    triple_buffer_t *tb = arg;
    for (uint16_t n = 1; n <= 20000; n++) {
        uint16_t *px = tb_back(tb);
        for (int i = 0; i < TB_PIXELS; i++) px[i] = n;
        tb_publish(tb);
    }
    return NULL;
}

TEST(tb_threads_never_see_a_torn_or_older_frame) {
    triple_buffer_t *tb = tb_new();
    pthread_t t;
    pthread_create(&t, NULL, tb_producer, tb);
    uint16_t last = 0;
    bool torn = false, older = false;
    while (last < 20000) {
        bool fresh;
        const uint16_t *f = tb_take(tb, &fresh);
        if (!fresh) continue;
        for (int i = 1; i < TB_PIXELS; i++) torn |= f[i] != f[0];
        older |= f[0] < last;
        last = f[0];
    }
    pthread_join(t, NULL);
    CHECK(!torn);
    CHECK(!older);
    free(tb);
}
```

Append to `tests/unit/CMakeLists.txt`:

```cmake

# The Android platform layer's plain-C pieces are tested here too (they use no Android API).
set(PLATFORM_DIR ${PROJECT_SOURCE_DIR}/platform/android)
target_sources(unit_tests PRIVATE ${PLATFORM_DIR}/fit_rect.c ${PLATFORM_DIR}/frame_pacer.c ${PLATFORM_DIR}/triple_buffer.c)
target_include_directories(unit_tests PRIVATE ${PLATFORM_DIR})
find_package(Threads REQUIRED)
target_link_libraries(unit_tests PRIVATE Threads::Threads)
```

- [x] **Step 2: Run to see them fail**

```bash
bash tools/dev.sh configure && bash tools/dev.sh unit
```

Expected: build error (`fit_rect.h`, `frame_pacer.h`, `triple_buffer.h` not found).

- [x] **Step 3: Write the code**

`platform/android/fit_rect.h`:

```c
#ifndef GBAEMU_FIT_RECT_H
#define GBAEMU_FIT_RECT_H

typedef struct {
    int x, y, w, h;
} fit_rect_t;

/* The largest 3:2 rectangle inside a width x height surface, centred, in whole pixels
 * (x and y are the offsets of the free space on each side; all zero for an empty surface). */
fit_rect_t fit_rect(int width, int height);

#endif
```

`platform/android/fit_rect.c`:

```c
#include "fit_rect.h"

fit_rect_t fit_rect(int width, int height) {
    fit_rect_t r = {0, 0, 0, 0};
    if (width <= 0 || height <= 0) return r;
    if ((long)width * 2 <= (long)height * 3) { /* the width is the limit: bars above and below */
        r.w = width;
        r.h = width * 2 / 3;
    } else { /* the height is the limit: bars left and right */
        r.h = height;
        r.w = height * 3 / 2;
    }
    r.x = (width - r.w) / 2;
    r.y = (height - r.h) / 2;
    return r;
}
```

`platform/android/frame_pacer.h`:

```c
#ifndef GBAEMU_FRAME_PACER_H
#define GBAEMU_FRAME_PACER_H

#include <stdbool.h>
#include <stdint.h>

/* One GBA frame = 280,896 cycles at 16,777,216 Hz = 16,742,706 ns + 5,013,504 / 2^24 ns. */
#define PACER_PERIOD_NS 16742706
#define PACER_FRAC_STEP 5013504u
#define PACER_FRAC_ONE (1u << 24)
#define PACER_MAX_LATE 3 /* frames behind before the pacer stops trying to catch up */

typedef struct {
    int64_t next_ns; /* the deadline handed out last */
    uint32_t frac;   /* sub-nanosecond remainder, in 1/2^24 ns */
    bool started;
} frame_pacer_t;

/* The next pacer_next call starts again from its 'now' (first frame, or after a pause). */
void pacer_reset(frame_pacer_t *p);
/* Called after each frame: the absolute time (ns) to sleep until before the next frame. */
int64_t pacer_next(frame_pacer_t *p, int64_t now_ns);

#endif
```

`platform/android/frame_pacer.c`:

```c
#include "frame_pacer.h"

void pacer_reset(frame_pacer_t *p) {
    p->started = false;
}

int64_t pacer_next(frame_pacer_t *p, int64_t now_ns) {
    if (!p->started || now_ns - p->next_ns > (int64_t)PACER_MAX_LATE * PACER_PERIOD_NS) {
        p->next_ns = now_ns + PACER_PERIOD_NS; /* start over: no burst of catch-up frames */
        p->frac = PACER_FRAC_STEP; /* this first period's sub-nanosecond part */
        p->started = true;
        return p->next_ns;
    }
    p->next_ns += PACER_PERIOD_NS;
    p->frac += PACER_FRAC_STEP;
    if (p->frac >= PACER_FRAC_ONE) {
        p->frac -= PACER_FRAC_ONE;
        p->next_ns += 1;
    }
    return p->next_ns;
}
```

`platform/android/triple_buffer.h`:

```c
#ifndef GBAEMU_TRIPLE_BUFFER_H
#define GBAEMU_TRIPLE_BUFFER_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#define TB_WIDTH 240
#define TB_HEIGHT 160
#define TB_PIXELS (TB_WIDTH * TB_HEIGHT)

/* Three frame slots shared by one producer (the emulation thread) and one consumer (the render
 * thread) without locks: the producer fills its back slot and swaps it into the middle; the
 * consumer swaps the middle out when it holds a frame it has not taken yet. */
typedef struct {
    uint16_t slot[3][TB_PIXELS];
    atomic_uint middle; /* slot index, | TB_FRESH when it holds a frame the consumer has not taken */
    unsigned back;      /* producer's slot */
    unsigned front;     /* consumer's slot */
} triple_buffer_t;

#define TB_FRESH 4u

void tb_init(triple_buffer_t *tb);
uint16_t *tb_back(triple_buffer_t *tb); /* producer: the slot to fill */
void tb_publish(triple_buffer_t *tb);   /* producer: hand the filled slot over as the newest frame */
/* Consumer: the newest complete frame; *fresh tells whether it is new since the last call. */
const uint16_t *tb_take(triple_buffer_t *tb, bool *fresh);

#endif
```

`platform/android/triple_buffer.c`:

```c
#include "triple_buffer.h"

#include <string.h>

void tb_init(triple_buffer_t *tb) {
    memset(tb->slot, 0, sizeof tb->slot);
    tb->back = 0;
    atomic_init(&tb->middle, 1u);
    tb->front = 2;
}

uint16_t *tb_back(triple_buffer_t *tb) {
    return tb->slot[tb->back];
}

void tb_publish(triple_buffer_t *tb) {
    unsigned old = atomic_exchange(&tb->middle, tb->back | TB_FRESH);
    tb->back = old & ~TB_FRESH;
}

const uint16_t *tb_take(triple_buffer_t *tb, bool *fresh) {
    *fresh = (atomic_load(&tb->middle) & TB_FRESH) != 0;
    if (*fresh) tb->front = atomic_exchange(&tb->middle, tb->front) & ~TB_FRESH;
    return tb->slot[tb->front];
}
```

(The pacer counts the first period's 0.2988 ns too — without it the prototype's 600-frame test was 1 ns short.)

- [x] **Step 4: Run the tests**

```bash
bash tools/dev.sh configure && bash tools/dev.sh unit
bash tools/dev.sh test
```

Expected: `336 tests, 0 failed` (323 + 13 new; the threaded triple-buffer test takes a moment); CTest 56/56.

- [x] **Step 5: Commit**

```bash
git add tests/unit/test_platform.c tests/unit/CMakeLists.txt platform/android/fit_rect.h platform/android/fit_rect.c platform/android/frame_pacer.h platform/android/frame_pacer.c platform/android/triple_buffer.h platform/android/triple_buffer.c docs/PROGRESS.md docs/superpowers/plans/2026-09-26-milestone-4a-app-picture.md
git commit -m "feat(android): plain-C aspect fit, frame pacer and triple buffer" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 2: The Android project (Gradle, native library, dev.sh commands)

**Files:**
- Create: `settings.gradle.kts`, `build.gradle.kts`, `gradle.properties`, `app/build.gradle.kts`, `app/src/main/AndroidManifest.xml`, `app/src/main/java/com/tengen/gbaemu/MainActivity.kt` (placeholder), `app/src/main/java/com/tengen/gbaemu/NativeBridge.kt`, `app/src/androidTest/java/com/tengen/gbaemu/NativeLibraryTest.kt`, `platform/android/CMakeLists.txt`, Gradle wrapper (`gradlew`, `gradlew.bat`, `gradle/wrapper/*`)
- Modify: `tools/dev.sh`, `.gitignore`

- [x] **Step 1: Gradle files**

`settings.gradle.kts`:

```kotlin
// The Gradle side of the project: the Android app in app/. The emulator core and PC tools are
// built with CMake (see CMakeLists.txt and tools/dev.sh); the app builds the core through
// platform/android/CMakeLists.txt.
pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
    // Map plugin ids straight to their artifacts, so no plugin-marker lookup is needed.
    resolutionStrategy {
        eachPlugin {
            when (requested.id.id) {
                "com.android.application" -> useModule("com.android.tools.build:gradle:8.13.0")
                "org.jetbrains.kotlin.android" -> useModule("org.jetbrains.kotlin:kotlin-gradle-plugin:2.2.21")
                "org.jetbrains.kotlin.plugin.compose" -> useModule("org.jetbrains.kotlin:compose-compiler-gradle-plugin:2.2.21")
            }
        }
    }
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "gba_emulator"
include(":app")
```

`build.gradle.kts`:

```kotlin
plugins {
    id("com.android.application") version "8.13.0" apply false
    id("org.jetbrains.kotlin.android") version "2.2.21" apply false
    id("org.jetbrains.kotlin.plugin.compose") version "2.2.21" apply false
}
```

`gradle.properties`:

```properties
android.useAndroidX=true
android.nonTransitiveRClass=true
org.gradle.jvmargs=-Xmx2g -Dfile.encoding=UTF-8
kotlin.code.style=official
```

`app/build.gradle.kts`:

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

(`copyTestRoms` feeds the on-device tests of Task 4 onwards; it is harmless before then.)

- [x] **Step 2: Native build file**

`platform/android/CMakeLists.txt`:

```cmake
# Native library for the Android app (built by Gradle's externalNativeBuild from app/build.gradle.kts).
cmake_minimum_required(VERSION 3.22)
project(gbaemu LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# The emulator must run at full speed in the debug APK too: optimise, keep debug symbols.
set(CMAKE_C_FLAGS_DEBUG "-O2 -g")

set(REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
add_subdirectory(${REPO_ROOT}/core ${CMAKE_BINARY_DIR}/core)

file(GLOB PLATFORM_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/*.c)
add_library(gbaemu SHARED ${PLATFORM_SOURCES})
target_include_directories(gbaemu PRIVATE ${CMAKE_CURRENT_LIST_DIR} ${REPO_ROOT}/tools/common)
target_compile_options(gbaemu PRIVATE -Wall -Wextra -Wno-unused-parameter -Werror=unguarded-availability)
# Newer-API calls (e.g. AChoreographer_postFrameCallback64, Android 10+) link weakly and must sit
# behind __builtin_available checks; minSdk is 26.
target_compile_definitions(gbaemu PRIVATE __ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__)
target_link_libraries(gbaemu PRIVATE gbacore android log EGL GLESv3)
```

It compiles every `platform/android/*.c` (Task 1's files now; later tasks just add files). The top-level `CMakeLists.txt` already skips the PC tools when `ANDROID` is set — it is not used by the app.

- [x] **Step 3: App sources and the first on-device test**

`app/src/main/AndroidManifest.xml`:

```xml
<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android">

    <application
        android:label="GBA Emulator"
        android:theme="@android:style/Theme.Material.NoActionBar">

        <!-- configChanges: rotation and keyboard changes resize the surface instead of restarting the game. -->
        <activity
            android:name=".MainActivity"
            android:configChanges="orientation|screenSize|screenLayout|smallestScreenSize|keyboard|keyboardHidden|navigation|density|uiMode"
            android:exported="true">
            <intent-filter>
                <action android:name="android.intent.action.MAIN" />
                <category android:name="android.intent.category.LAUNCHER" />
            </intent-filter>
        </activity>
    </application>
</manifest>
```

`app/src/main/java/com/tengen/gbaemu/MainActivity.kt` (placeholder, replaced in Task 8):

```kotlin
package com.tengen.gbaemu

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.material3.Text

/** Placeholder until Task 8 builds the real screens. */
class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent { Text("GBA Emulator") }
    }
}
```

`app/src/main/java/com/tengen/gbaemu/NativeBridge.kt`:

```kotlin
package com.tengen.gbaemu

/** The C side (platform/android/jni_bridge.c). One game session at a time; call from one thread. */
object NativeBridge {
    init {
        System.loadLibrary("gbaemu")
    }
}
```

`app/src/androidTest/java/com/tengen/gbaemu/NativeLibraryTest.kt`:

```kotlin
package com.tengen.gbaemu

import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertNotNull
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class NativeLibraryTest {
    @Test
    fun nativeLibraryLoads() {
        assertNotNull(NativeBridge) /* first use runs System.loadLibrary("gbaemu") */
    }
}
```

- [x] **Step 4: Gradle wrapper (from the cached Gradle 8.14.3)**

```bash
printf 'sdk.dir=%s\n' "$(cygpath -m "$LOCALAPPDATA/Android/Sdk")" > local.properties   # where the Android SDK is (gitignored)
JAVA_HOME="$(cygpath -m "/c/Program Files/Android/Android Studio/jbr")" "$(ls -d ~/.gradle/wrapper/dists/gradle-8.14.3-all/*/gradle-8.14.3)/bin/gradle" --offline --no-daemon -q wrapper --gradle-version 8.14.3 --distribution-type all
ls gradlew gradlew.bat gradle/wrapper/gradle-wrapper.jar gradle/wrapper/gradle-wrapper.properties
```

(Gradle reads `local.properties` while configuring the project, so it must exist first.) Expected: the four files exist; `gradle-wrapper.properties` has `distributionUrl=https\://services.gradle.org/distributions/gradle-8.14.3-all.zip` (the cached one).

- [x] **Step 5: `tools/dev.sh` Android commands**

In the header comment, after the `#   tools/dev.sh release ...` line, insert:

```bash
# Android (Gradle project in app/; always the Pixel 9 phone emulator, never a USB phone; Gradle runs --offline):
#   tools/dev.sh apk              build the debug app (app/build/outputs/apk/debug/app-debug.apk)
#   tools/dev.sh jvmtest          Kotlin unit tests on the PC
#   tools/dev.sh emu              start the phone emulator if it is not running; wait until it has booted
#   tools/dev.sh install          emu + install the debug app
#   tools/dev.sh launch           emu + start the app
#   tools/dev.sh androidtest      emu + run the on-device tests
#   tools/dev.sh push <file>      emu + copy a file into the phone emulator's Download folder
# The Debug + sanitizer build is for tests; it runs real games at only ~30-45 fps, so the viewer
# always uses the release build (about 9x faster than real time).
```

Before the line `cmd="${1:-}"`, insert:

```bash
# ---- Android ----
JBR="/c/Program Files/Android/Android Studio/jbr" # Android Studio's own Java, which runs Gradle
AVD="Pixel_9_API_36_1"
APP_ID="com.tengen.gbaemu"
# MSYS_NO_PATHCONV: stop Git Bash rewriting phone paths such as /sdcard/... into Windows paths.
adb() { MSYS_NO_PATHCONV=1 "$SDK/platform-tools/adb" "$@"; }
gradle_run() {
    [ -f "$ROOT/local.properties" ] || printf 'sdk.dir=%s\n' "$(cygpath -m "$SDK")" > "$ROOT/local.properties"
    (cd "$ROOT" && JAVA_HOME="$(cygpath -m "$JBR")" ./gradlew --offline "$@")
}
emu_serial() { adb devices | awk '$1 ~ /^emulator-/ && $2 == "device" { print $1; exit }'; }
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
```

In the `case`, after `    release) build_release ;;`, insert:

```bash
    apk) gradle_run :app:assembleDebug ;;
    jvmtest) gradle_run :app:testDebugUnitTest ;;
    emu) emu_up ;;
    install) emu_up && gradle_run :app:installDebug ;;
    launch) emu_up && adb shell am start -n "$APP_ID/.MainActivity" ;;
    androidtest) emu_up && gradle_run :app:connectedDebugAndroidTest ;;
    push) emu_up && adb push "$(cygpath -w "$1")" /sdcard/Download/ ;; # adb is a Windows program: give it a Windows path
```

and replace the usage line with:

```bash
    *) echo "usage: tools/dev.sh configure|build|unit [filter]|test|run <args>|view <args>|release|apk|jvmtest|emu|install|launch|androidtest|push <file>" >&2; exit 2 ;;
```

Append to `.gitignore` under `# Build outputs`: `.kotlin/` (Kotlin 2's per-project cache).

- [x] **Step 6: Build and run the first on-device test**

```bash
bash tools/dev.sh apk
bash tools/dev.sh jvmtest
bash tools/dev.sh androidtest
bash tools/dev.sh test
```

Expected: `BUILD SUCCESSFUL` each time (the first `apk` also writes `local.properties`, gitignored); `androidtest` starts the phone emulator if needed and reports `Starting 1 tests on Pixel_9_API_36_1(AVD)` — only the emulator, even if a phone is plugged in — 1 test, 0 failures (`NativeLibraryTest`); `jvmtest` has no tests yet (still successful); CTest 56/56. The native build must show no C warnings: `bash tools/dev.sh apk --rerun-tasks 2>&1 | grep -c "warning:.*\.[ch]:"` → `0`. `bash tools/dev.sh bogus` prints the new usage and exits 2.

- [x] **Step 7: Commit**

```bash
git add settings.gradle.kts build.gradle.kts gradle.properties gradlew gradlew.bat gradle/wrapper app/build.gradle.kts app/src/main/AndroidManifest.xml app/src/main/java/com/tengen/gbaemu/MainActivity.kt app/src/main/java/com/tengen/gbaemu/NativeBridge.kt app/src/androidTest/java/com/tengen/gbaemu/NativeLibraryTest.kt platform/android/CMakeLists.txt tools/dev.sh .gitignore docs/PROGRESS.md docs/superpowers/plans/2026-09-26-milestone-4a-app-picture.md
git commit -m "build(android): Gradle project, native library and dev.sh Android commands" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 3: Keyboard and gamepad keys

**Files:**
- Create: `app/src/test/java/com/tengen/gbaemu/KeyMapTest.kt`, `app/src/main/java/com/tengen/gbaemu/KeyMap.kt`

- [x] **Step 1: Write the failing test**

`app/src/test/java/com/tengen/gbaemu/KeyMapTest.kt`:

```kotlin
package com.tengen.gbaemu

import android.view.KeyEvent
import org.junit.Assert.assertEquals
import org.junit.Test

class KeyMapTest {
    @Test
    fun everyMappedKeySetsItsButton() {
        val expected = mapOf(
            KeyEvent.KEYCODE_DPAD_UP to KeyMap.UP,
            KeyEvent.KEYCODE_DPAD_DOWN to KeyMap.DOWN,
            KeyEvent.KEYCODE_DPAD_LEFT to KeyMap.LEFT,
            KeyEvent.KEYCODE_DPAD_RIGHT to KeyMap.RIGHT,
            KeyEvent.KEYCODE_X to KeyMap.A,
            KeyEvent.KEYCODE_BUTTON_A to KeyMap.A,
            KeyEvent.KEYCODE_Z to KeyMap.B,
            KeyEvent.KEYCODE_BUTTON_B to KeyMap.B,
            KeyEvent.KEYCODE_A to KeyMap.L,
            KeyEvent.KEYCODE_BUTTON_L1 to KeyMap.L,
            KeyEvent.KEYCODE_S to KeyMap.R,
            KeyEvent.KEYCODE_BUTTON_R1 to KeyMap.R,
            KeyEvent.KEYCODE_ENTER to KeyMap.START,
            KeyEvent.KEYCODE_BUTTON_START to KeyMap.START,
            KeyEvent.KEYCODE_DEL to KeyMap.SELECT,
            KeyEvent.KEYCODE_BUTTON_SELECT to KeyMap.SELECT,
        )
        for ((key, bit) in expected) assertEquals("key $key", bit, KeyMap.bitFor(key))
    }

    @Test
    fun buttonBitsMatchTheCore() { /* GBA_KEY_* in core/include/gba.h */
        assertEquals(listOf(1, 2, 4, 8, 16, 32, 64, 128, 256, 512),
            listOf(KeyMap.A, KeyMap.B, KeyMap.SELECT, KeyMap.START, KeyMap.RIGHT, KeyMap.LEFT, KeyMap.UP, KeyMap.DOWN,
                KeyMap.R, KeyMap.L))
    }

    @Test
    fun pressAndReleaseChangeOnlyThatButton() {
        var mask = KeyMap.update(0, KeyEvent.KEYCODE_X, true)
        mask = KeyMap.update(mask, KeyEvent.KEYCODE_DPAD_UP, true)
        assertEquals(KeyMap.A or KeyMap.UP, mask)
        mask = KeyMap.update(mask, KeyEvent.KEYCODE_X, false)
        assertEquals(KeyMap.UP, mask)
    }

    @Test
    fun unmappedKeysChangeNothing() {
        assertEquals(0, KeyMap.bitFor(KeyEvent.KEYCODE_Q))
        assertEquals(KeyMap.UP, KeyMap.update(KeyMap.UP, KeyEvent.KEYCODE_Q, true))
    }
}
```

- [x] **Step 2: Run to see it fail**

```bash
bash tools/dev.sh jvmtest
```

Expected: FAIL — Kotlin compile error `Unresolved reference 'KeyMap'`.

- [x] **Step 3: Write the code**

`app/src/main/java/com/tengen/gbaemu/KeyMap.kt`:

```kotlin
package com.tengen.gbaemu

import android.view.KeyEvent

/** Keyboard and gamepad keys → GBA buttons (the same bits as GBA_KEY_* in core/include/gba.h). */
object KeyMap {
    const val A = 1 shl 0
    const val B = 1 shl 1
    const val SELECT = 1 shl 2
    const val START = 1 shl 3
    const val RIGHT = 1 shl 4
    const val LEFT = 1 shl 5
    const val UP = 1 shl 6
    const val DOWN = 1 shl 7
    const val R = 1 shl 8
    const val L = 1 shl 9

    /** The GBA button for an Android key code, or 0 if the key is not mapped. */
    fun bitFor(keyCode: Int): Int = when (keyCode) {
        KeyEvent.KEYCODE_DPAD_UP -> UP
        KeyEvent.KEYCODE_DPAD_DOWN -> DOWN
        KeyEvent.KEYCODE_DPAD_LEFT -> LEFT
        KeyEvent.KEYCODE_DPAD_RIGHT -> RIGHT
        KeyEvent.KEYCODE_X, KeyEvent.KEYCODE_BUTTON_A -> A
        KeyEvent.KEYCODE_Z, KeyEvent.KEYCODE_BUTTON_B -> B
        KeyEvent.KEYCODE_A, KeyEvent.KEYCODE_BUTTON_L1 -> L
        KeyEvent.KEYCODE_S, KeyEvent.KEYCODE_BUTTON_R1 -> R
        KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_BUTTON_START -> START
        KeyEvent.KEYCODE_DEL, KeyEvent.KEYCODE_BUTTON_SELECT -> SELECT
        else -> 0
    }

    /** The held-buttons mask after a key goes down or up. */
    fun update(mask: Int, keyCode: Int, down: Boolean): Int {
        val bit = bitFor(keyCode)
        return if (down) mask or bit else mask and bit.inv()
    }
}
```

- [x] **Step 4: Run the test**

```bash
bash tools/dev.sh jvmtest
```

Expected: `BUILD SUCCESSFUL`; `app/build/test-results/testDebugUnitTest/TEST-com.tengen.gbaemu.KeyMapTest.xml` shows `tests="4" ... failures="0"`.

- [x] **Step 5: Commit**

```bash
git add app/src/test/java/com/tengen/gbaemu/KeyMapTest.kt app/src/main/java/com/tengen/gbaemu/KeyMap.kt docs/PROGRESS.md docs/superpowers/plans/2026-09-26-milestone-4a-app-picture.md
git commit -m "feat(android): keyboard and gamepad keys to GBA buttons" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

**Batch 1 (Tasks 1-3) done — checkpoint with the user.**

---
### Task 4: Same core, same results on Android

**Files:**
- Create: `app/src/androidTest/java/com/tengen/gbaemu/TestRoms.kt`, `app/src/androidTest/java/com/tengen/gbaemu/FingerprintTest.kt`, `platform/android/jni_bridge.c`
- Modify: `app/src/main/java/com/tengen/gbaemu/NativeBridge.kt`

- [x] **Step 1: Write the failing test**

`app/src/androidTest/java/com/tengen/gbaemu/TestRoms.kt`:

```kotlin
package com.tengen.gbaemu

import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import java.nio.ByteBuffer

/** The local test ROMs and tests/expected_frames.json, packed into the test APK's assets by the build. */
object TestRoms {
    private val assets get() = InstrumentationRegistry.getInstrumentation().context.assets

    fun text(path: String): String = assets.open(path).bufferedReader().use { it.readText() }

    /** A ROM by its path in expected_frames.json (relative to tests/roms, e.g. "tonc/m7_demo.gba"). */
    fun rom(romPath: String): ByteBuffer {
        val path = File("roms", romPath).normalize().path.replace('\\', '/')
        val bytes = assets.open(path).use { it.readBytes() }
        return ByteBuffer.allocateDirect(bytes.size).put(bytes).also { it.flip() }
    }
}
```

`app/src/androidTest/java/com/tengen/gbaemu/FingerprintTest.kt`:

```kotlin
package com.tengen.gbaemu

import androidx.test.ext.junit.runners.AndroidJUnit4
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** The core built for Android must give exactly the PC's approved picture and sound fingerprints. */
@RunWith(AndroidJUnit4::class)
class FingerprintTest {
    @Test
    fun androidCoreReproducesEveryApprovedFingerprint() {
        val entries = JSONObject(TestRoms.text("expected_frames.json")).getJSONArray("frames")
        assertEquals("approved entries", 32, entries.length())
        val failures = mutableListOf<String>()
        for (i in 0 until entries.length()) {
            val e = entries.getJSONObject(i)
            val got = NativeBridge.debugRunFrames(TestRoms.rom(e.getString("rom")), e.getInt("frames"))
            val parts = got?.split(" ") ?: listOf("load-failed", "")
            val frameOk = parts[0] == e.getString("hash")
            val audioOk = !e.has("audio") || parts[1] == e.getString("audio")
            if (!frameOk || !audioOk) failures += "${e.getString("name")}: got $got"
        }
        assertTrue(failures.joinToString("\n"), failures.isEmpty())
    }
}
```

- [x] **Step 2: Run to see it fail**

```bash
bash tools/dev.sh androidtest
```

Expected: FAIL — `Unresolved reference 'debugRunFrames'`.

- [x] **Step 3: The JNI function**

`platform/android/jni_bridge.c` (it grows in Tasks 5-7):

```c
/* JNI entry points for com.tengen.gbaemu.NativeBridge. Called from one thread at a time (the UI
 * thread in the app, the test thread in on-device tests); they keep one game session. */
#include <inttypes.h>
#include <jni.h>
#include <stdio.h>

#include "frame_hash.h"
#include "gba.h"

#define JNI_FN(name) Java_com_tengen_gbaemu_NativeBridge_##name

/* Headless run on the calling thread, exactly like `testrunner --frames n`: "<frame hash> <audio hash>". */
JNIEXPORT jstring JNICALL JNI_FN(debugRunFrames)(JNIEnv *env, jobject self, jobject rom, jint frames) {
    (void)self;
    const uint8_t *data = (*env)->GetDirectBufferAddress(env, rom);
    jlong size = (*env)->GetDirectBufferCapacity(env, rom);
    gba_t *g = gba_create(NULL);
    if (!g || !data || gba_load_rom(g, data, (size_t)size) != GBA_OK) {
        gba_destroy(g);
        return NULL;
    }
    uint64_t audio_hash = AUDIO_HASH_INIT;
    int16_t buf[1024 * 2];
    for (jint f = 0; f < frames; f++) {
        gba_set_keys(g, 0);
        gba_run_frame(g);
        size_t n;
        while ((n = gba_audio_read(g, buf, 1024)) > 0) audio_hash = audio_hash_update(audio_hash, buf, n * 2);
    }
    char out[40];
    snprintf(out, sizeof out, "%016" PRIx64 " %016" PRIx64, frame_hash(gba_framebuffer(g)), audio_hash);
    gba_destroy(g);
    return (*env)->NewStringUTF(env, out);
}
```

It runs exactly like `testrunner --frames n`: keys 0 every frame, all sound drained and hashed after each frame (`tools/common/frame_hash.h`).

Replace `app/src/main/java/com/tengen/gbaemu/NativeBridge.kt` with:

```kotlin
package com.tengen.gbaemu

import java.nio.ByteBuffer

/** The C side (platform/android/jni_bridge.c). One game session at a time; call from one thread. */
object NativeBridge {
    init {
        System.loadLibrary("gbaemu")
    }

    // ---- test-only ----

    /** Runs a ROM headless for n frames on this thread: "<frame hash> <audio hash>" like the PC test runner. */
    external fun debugRunFrames(rom: ByteBuffer, frames: Int): String?
}
```

- [x] **Step 4: Run the tests**

```bash
bash tools/dev.sh androidtest
```

Expected: 2 tests, 0 failures — all 32 approved entries (26 frames, 4 with audio, 2 save-chip ROMs) give the PC's hashes (the prototype matched every one). If an entry fails, the message lists `<name>: got <frame> <audio>`: compare with `bash tools/dev.sh run tests/roms/<rom> --frames <n> --hash --audio-hash` on the PC before changing anything.

- [x] **Step 5: Commit**

```bash
git add app/src/androidTest/java/com/tengen/gbaemu/TestRoms.kt app/src/androidTest/java/com/tengen/gbaemu/FingerprintTest.kt platform/android/jni_bridge.c app/src/main/java/com/tengen/gbaemu/NativeBridge.kt docs/PROGRESS.md docs/superpowers/plans/2026-09-26-milestone-4a-app-picture.md
git commit -m "test(android): the Android-built core reproduces all 32 PC fingerprints" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 5: OpenGL ES drawing

**Files:**
- Create: `app/src/androidTest/java/com/tengen/gbaemu/PictureTest.kt`, `platform/android/egl_ctx.h`, `platform/android/egl_ctx.c`, `platform/android/video_gl.h`, `platform/android/video_gl.c`, `platform/android/gl_readback.h`, `platform/android/gl_readback.c`
- Modify: `platform/android/jni_bridge.c`, `app/src/main/java/com/tengen/gbaemu/NativeBridge.kt`

- [x] **Step 1: Write the failing test**

`app/src/androidTest/java/com/tengen/gbaemu/PictureTest.kt`:

```kotlin
package com.tengen.gbaemu

import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Test
import org.junit.runner.RunWith

/** The real OpenGL drawing code, read back pixel by pixel. */
@RunWith(AndroidJUnit4::class)
class PictureTest {
    /** A frame where neighbouring pixels differ in every channel; white and black included. */
    private val frame = ShortArray(240 * 160) { i ->
        val x = i % 240
        val y = i / 240
        ((x * 7 + y * 3) and 31 or (((x + y * 5) and 31) shl 5) or (((x * 3 + y) and 31) shl 10)).toShort()
    }.also {
        it[0] = 0x7FFF
        it[1] = 0
    }

    /** BGR555 → the RGBA int glReadPixels gives: each 5-bit channel c becomes (c << 3) | (c >> 2). */
    private fun rgba(p: Short): Int {
        val v = p.toInt() and 0x7FFF
        fun ch(c: Int) = (c shl 3) or (c shr 2)
        return ch(v and 31) or (ch((v shr 5) and 31) shl 8) or (ch((v shr 10) and 31) shl 16) or (0xFF shl 24)
    }

    private val black = 0xFF shl 24

    @Test
    fun doubleSizeGivesEachGbaPixelA2x2Block() {
        val out = NativeBridge.debugDrawFrame(frame, 480, 320)
        assertNotNull("offscreen OpenGL ES 3 drawing failed", out)
        for (y in 0 until 320) for (x in 0 until 480) {
            assertEquals("pixel ($x, $y)", rgba(frame[(y / 2) * 240 + x / 2]), out!![y * 480 + x])
        }
    }

    @Test
    fun wideSurfaceGetsBlackBarsAndACentredPicture() {
        val out = NativeBridge.debugDrawFrame(frame, 720, 320)
        assertNotNull("offscreen OpenGL ES 3 drawing failed", out)
        for (y in 0 until 320) for (x in 0 until 720) {
            val want = if (x < 120 || x >= 600) black else rgba(frame[(y / 2) * 240 + (x - 120) / 2])
            assertEquals("pixel ($x, $y)", want, out!![y * 720 + x])
        }
    }
}
```

- [x] **Step 2: Run to see it fail**

```bash
bash tools/dev.sh androidtest
```

Expected: FAIL — `Unresolved reference 'debugDrawFrame'`.

- [x] **Step 3: The drawing code**

`platform/android/egl_ctx.h`:

```c
#ifndef GBAEMU_EGL_CTX_H
#define GBAEMU_EGL_CTX_H

#include <EGL/egl.h>
#include <stdbool.h>

/* An EGL display + RGBA8888 config + OpenGL ES 3 context. */
typedef struct {
    EGLDisplay display;
    EGLConfig config;
    EGLContext context;
} egl_ctx_t;

/* for_pbuffer: the config must support offscreen pbuffer surfaces (tests), else window surfaces. */
bool egl_ctx_init(egl_ctx_t *e, bool for_pbuffer);
void egl_ctx_destroy(egl_ctx_t *e);

#endif
```

`platform/android/egl_ctx.c`:

```c
#include "egl_ctx.h"

#include <EGL/eglext.h>
#include <android/log.h>

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "gbaemu", __VA_ARGS__)

bool egl_ctx_init(egl_ctx_t *e, bool for_pbuffer) {
    e->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    e->context = EGL_NO_CONTEXT;
    if (e->display == EGL_NO_DISPLAY || !eglInitialize(e->display, NULL, NULL)) {
        LOGE("eglInitialize failed: 0x%x", eglGetError());
        return false;
    }
    const EGLint attrs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
                            EGL_SURFACE_TYPE, for_pbuffer ? EGL_PBUFFER_BIT : EGL_WINDOW_BIT,
                            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                            EGL_NONE};
    EGLint count = 0;
    if (!eglChooseConfig(e->display, attrs, &e->config, 1, &count) || count < 1) {
        LOGE("no RGBA8888 OpenGL ES 3 config: 0x%x", eglGetError());
        return false;
    }
    const EGLint ctx_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    e->context = eglCreateContext(e->display, e->config, EGL_NO_CONTEXT, ctx_attrs);
    if (e->context == EGL_NO_CONTEXT) {
        LOGE("eglCreateContext (ES 3) failed: 0x%x", eglGetError());
        return false;
    }
    return true;
}

void egl_ctx_destroy(egl_ctx_t *e) {
    if (e->display == EGL_NO_DISPLAY) return;
    eglMakeCurrent(e->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (e->context != EGL_NO_CONTEXT) eglDestroyContext(e->display, e->context);
    e->context = EGL_NO_CONTEXT;
}
```

`platform/android/video_gl.h`:

```c
#ifndef GBAEMU_VIDEO_GL_H
#define GBAEMU_VIDEO_GL_H

#include <GLES3/gl3.h>
#include <stdbool.h>
#include <stdint.h>

/* Draws 240x160 BGR555 frames with OpenGL ES 3.0: one integer texture, one quad, a shader that
 * unpacks the 15-bit colour. Every call needs the same current GLES 3 context. */
typedef struct {
    GLuint program, texture, vao;
} video_gl_t;

bool video_gl_init(video_gl_t *v);                          /* false if the shader will not build */
void video_gl_upload(video_gl_t *v, const uint16_t *pixels); /* a new frame */
void video_gl_draw(video_gl_t *v, int width, int height);    /* black surface + the frame, aspect-fit */
void video_gl_destroy(video_gl_t *v);

#endif
```

`platform/android/video_gl.c`:

```c
#include "video_gl.h"

#include <android/log.h>

#include "fit_rect.h"

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "gbaemu", __VA_ARGS__)

/* A quad from gl_VertexID (triangle strip, no vertex buffer); uv (0,0) is the bottom left. */
static const char *VERTEX_SRC =
    "#version 300 es\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "    v_uv = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));\n"
    "    gl_Position = vec4(v_uv * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

/* Nearest texel (row 0 of the frame at the top); each 5-bit channel c becomes (c << 3) | (c >> 2). */
static const char *FRAGMENT_SRC =
    "#version 300 es\n"
    "precision highp float;\n"
    "precision highp int;\n"
    "precision highp usampler2D;\n"
    "uniform usampler2D u_frame;\n"
    "in vec2 v_uv;\n"
    "out vec4 o_color;\n"
    "void main() {\n"
    "    ivec2 t = ivec2(min(v_uv.x * 240.0, 239.0), min((1.0 - v_uv.y) * 160.0, 159.0));\n"
    "    uint p = texelFetch(u_frame, t, 0).r;\n"
    "    uvec3 c = uvec3(p & 31u, (p >> 5) & 31u, (p >> 10) & 31u);\n"
    "    c = (c << 3) | (c >> 2);\n"
    "    o_color = vec4(vec3(c) / 255.0, 1.0);\n"
    "}\n";

static GLuint compile(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        LOGE("shader: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

bool video_gl_init(video_gl_t *v) {
    GLuint vs = compile(GL_VERTEX_SHADER, VERTEX_SRC), fs = compile(GL_FRAGMENT_SHADER, FRAGMENT_SRC);
    if (!vs || !fs) return false;
    v->program = glCreateProgram();
    glAttachShader(v->program, vs);
    glAttachShader(v->program, fs);
    glLinkProgram(v->program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(v->program, GL_LINK_STATUS, &ok);
    if (!ok) {
        LOGE("shader program did not link");
        return false;
    }
    glUseProgram(v->program);
    glUniform1i(glGetUniformLocation(v->program, "u_frame"), 0);

    glGenTextures(1, &v->texture);
    glBindTexture(GL_TEXTURE_2D, v->texture);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_R16UI, 240, 160);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); /* integer textures need NEAREST */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenVertexArrays(1, &v->vao);
    return glGetError() == GL_NO_ERROR;
}

void video_gl_upload(video_gl_t *v, const uint16_t *pixels) {
    glBindTexture(GL_TEXTURE_2D, v->texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 240, 160, GL_RED_INTEGER, GL_UNSIGNED_SHORT, pixels);
}

void video_gl_draw(video_gl_t *v, int width, int height) {
    glViewport(0, 0, width, height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    fit_rect_t r = fit_rect(width, height);
    if (r.w == 0) return;
    glViewport(r.x, r.y, r.w, r.h);
    glUseProgram(v->program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, v->texture);
    glBindVertexArray(v->vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void video_gl_destroy(video_gl_t *v) {
    if (v->vao) glDeleteVertexArrays(1, &v->vao);
    if (v->texture) glDeleteTextures(1, &v->texture);
    if (v->program) glDeleteProgram(v->program);
    v->vao = v->texture = v->program = 0;
}
```

`platform/android/gl_readback.h`:

```c
#ifndef GBAEMU_GL_READBACK_H
#define GBAEMU_GL_READBACK_H

#include <stdbool.h>
#include <stdint.h>

/* Test helper: draws one 240x160 BGR555 frame with the real video_gl code into an offscreen
 * width x height surface on the calling thread and reads it back, top row first, one
 * little-endian RGBA pixel per uint32_t (r | g << 8 | b << 16 | a << 24). */
bool gl_readback(const uint16_t *pixels, int width, int height, uint32_t *out);

#endif
```

`platform/android/gl_readback.c`:

```c
#include "gl_readback.h"

#include <string.h>

#include "egl_ctx.h"
#include "video_gl.h"

bool gl_readback(const uint16_t *pixels, int width, int height, uint32_t *out) {
    egl_ctx_t e;
    if (!egl_ctx_init(&e, true)) return false;
    const EGLint pb_attrs[] = {EGL_WIDTH, width, EGL_HEIGHT, height, EGL_NONE};
    EGLSurface s = eglCreatePbufferSurface(e.display, e.config, pb_attrs);
    bool ok = s != EGL_NO_SURFACE && eglMakeCurrent(e.display, s, s, e.context);
    video_gl_t v = {0, 0, 0};
    if (ok) ok = video_gl_init(&v);
    if (ok) {
        video_gl_upload(&v, pixels);
        video_gl_draw(&v, width, height);
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, out);
        ok = glGetError() == GL_NO_ERROR;
        for (int y = 0; y < height / 2; y++) { /* GL rows start at the bottom */
            uint32_t *a = out + (size_t)y * width, *b = out + (size_t)(height - 1 - y) * width;
            for (int x = 0; x < width; x++) {
                uint32_t t = a[x];
                a[x] = b[x];
                b[x] = t;
            }
        }
        video_gl_destroy(&v);
    }
    if (s != EGL_NO_SURFACE) {
        eglMakeCurrent(e.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(e.display, s);
    }
    egl_ctx_destroy(&e);
    return ok;
}
```

Replace `platform/android/jni_bridge.c` with:

```c
/* JNI entry points for com.tengen.gbaemu.NativeBridge. Called from one thread at a time (the UI
 * thread in the app, the test thread in on-device tests); they keep one game session. */
#include <inttypes.h>
#include <jni.h>
#include <stdio.h>
#include <stdlib.h>

#include "frame_hash.h"
#include "gba.h"
#include "gl_readback.h"

#define JNI_FN(name) Java_com_tengen_gbaemu_NativeBridge_##name

/* Headless run on the calling thread, exactly like `testrunner --frames n`: "<frame hash> <audio hash>". */
JNIEXPORT jstring JNICALL JNI_FN(debugRunFrames)(JNIEnv *env, jobject self, jobject rom, jint frames) {
    (void)self;
    const uint8_t *data = (*env)->GetDirectBufferAddress(env, rom);
    jlong size = (*env)->GetDirectBufferCapacity(env, rom);
    gba_t *g = gba_create(NULL);
    if (!g || !data || gba_load_rom(g, data, (size_t)size) != GBA_OK) {
        gba_destroy(g);
        return NULL;
    }
    uint64_t audio_hash = AUDIO_HASH_INIT;
    int16_t buf[1024 * 2];
    for (jint f = 0; f < frames; f++) {
        gba_set_keys(g, 0);
        gba_run_frame(g);
        size_t n;
        while ((n = gba_audio_read(g, buf, 1024)) > 0) audio_hash = audio_hash_update(audio_hash, buf, n * 2);
    }
    char out[40];
    snprintf(out, sizeof out, "%016" PRIx64 " %016" PRIx64, frame_hash(gba_framebuffer(g)), audio_hash);
    gba_destroy(g);
    return (*env)->NewStringUTF(env, out);
}

/* Draws one frame offscreen with the real OpenGL code; RGBA pixels top row first, or null. */
JNIEXPORT jintArray JNICALL JNI_FN(debugDrawFrame)(JNIEnv *env, jobject self, jshortArray pixels, jint width,
                                                   jint height) {
    (void)self;
    if ((*env)->GetArrayLength(env, pixels) != 240 * 160 || width <= 0 || height <= 0) return NULL;
    jshort *px = (*env)->GetShortArrayElements(env, pixels, NULL);
    uint32_t *out = malloc((size_t)width * (size_t)height * 4);
    bool ok = out && gl_readback((const uint16_t *)px, width, height, out);
    (*env)->ReleaseShortArrayElements(env, pixels, px, JNI_ABORT);
    jintArray result = NULL;
    if (ok) {
        result = (*env)->NewIntArray(env, width * height);
        (*env)->SetIntArrayRegion(env, result, 0, width * height, (const jint *)out);
    }
    free(out);
    return result;
}
```

Replace `app/src/main/java/com/tengen/gbaemu/NativeBridge.kt` with:

```kotlin
package com.tengen.gbaemu

import java.nio.ByteBuffer

/** The C side (platform/android/jni_bridge.c). One game session at a time; call from one thread. */
object NativeBridge {
    init {
        System.loadLibrary("gbaemu")
    }

    // ---- test-only ----

    /** Runs a ROM headless for n frames on this thread: "<frame hash> <audio hash>" like the PC test runner. */
    external fun debugRunFrames(rom: ByteBuffer, frames: Int): String?

    /** Draws one 240x160 BGR555 frame offscreen; RGBA pixels (r | g<<8 | b<<16 | a<<24), top row first. */
    external fun debugDrawFrame(pixels: ShortArray, width: Int, height: Int): IntArray?
}
```

- [x] **Step 4: Run the tests**

```bash
bash tools/dev.sh androidtest
```

Expected: 4 tests, 0 failures (both pictures pixel-exact).

- [x] **Step 5: Commit**

```bash
git add app/src/androidTest/java/com/tengen/gbaemu/PictureTest.kt platform/android/egl_ctx.h platform/android/egl_ctx.c platform/android/video_gl.h platform/android/video_gl.c platform/android/gl_readback.h platform/android/gl_readback.c platform/android/jni_bridge.c app/src/main/java/com/tengen/gbaemu/NativeBridge.kt docs/PROGRESS.md docs/superpowers/plans/2026-09-26-milestone-4a-app-picture.md
git commit -m "feat(android): OpenGL ES 3 drawing of BGR555 frames, aspect-fit" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 6: The emulation thread

**Files:**
- Create: `app/src/androidTest/java/com/tengen/gbaemu/SessionTest.kt`, `platform/android/emu_thread.h`, `platform/android/emu_thread.c`
- Modify: `platform/android/jni_bridge.c`, `app/src/main/java/com/tengen/gbaemu/NativeBridge.kt`

- [x] **Step 1: Write the failing tests**

`app/src/androidTest/java/com/tengen/gbaemu/SessionTest.kt` (Task 7 adds a window test):

```kotlin
package com.tengen.gbaemu

import android.os.SystemClock
import androidx.test.ext.junit.runners.AndroidJUnit4
import java.nio.ByteBuffer
import kotlin.math.abs
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** The emulation thread: speed, pause/resume, bad ROMs (the window hand-over comes with the render thread). */
@RunWith(AndroidJUnit4::class)
class SessionTest {
    @After
    fun stopSession() = NativeBridge.stop()

    @Test
    fun runsAtGbaSpeed() {
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba")))
        NativeBridge.start()
        Thread.sleep(500)
        val startFrames = NativeBridge.debugFrameCount()
        val startMs = SystemClock.elapsedRealtime()
        Thread.sleep(5000)
        val frames = NativeBridge.debugFrameCount() - startFrames
        val expected = (SystemClock.elapsedRealtime() - startMs) * 59.7275 / 1000.0
        assertTrue("ran $frames frames, expected about $expected", abs(frames - expected) <= 3.0)
    }

    @Test
    fun survivesTwentyStartPauseResumeStopRounds() {
        repeat(20) { round ->
            assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba")))
            NativeBridge.start()
            Thread.sleep(40)
            NativeBridge.pause()
            Thread.sleep(20) /* a frame already running when pause came may still finish */
            val paused = NativeBridge.debugFrameCount()
            Thread.sleep(60)
            assertEquals("round $round: frames while paused", paused, NativeBridge.debugFrameCount())
            NativeBridge.resume()
            Thread.sleep(60)
            assertTrue("round $round: no frames after resume", NativeBridge.debugFrameCount() > paused)
            NativeBridge.stop()
        }
    }

    @Test
    fun rejectsAnEmptyRom() {
        assertNotEquals(0, NativeBridge.loadRom(ByteBuffer.allocateDirect(0)))
    }
}
```

- [x] **Step 2: Run to see them fail**

```bash
bash tools/dev.sh androidtest
```

Expected: FAIL — `Unresolved reference 'loadRom'` (and the other session functions).

- [x] **Step 3: The thread**

`platform/android/emu_thread.h`:

```c
#ifndef GBAEMU_EMU_THREAD_H
#define GBAEMU_EMU_THREAD_H

#include <stddef.h>
#include <stdint.h>

#include "gba.h"
#include "triple_buffer.h"

/* One running game: the core, its frames, and the thread that runs it at 59.7275 frames/s.
 * Once started, only the emulation thread touches the core. */
typedef struct emu emu_t;

/* Creates the core, loads the ROM (copied), sets the clock from the phone; NULL + *err on failure. */
emu_t *emu_create(const uint8_t *rom, size_t size, gba_err_t *err);
void emu_start(emu_t *e);
void emu_pause(emu_t *e);
void emu_resume(emu_t *e); /* timing starts afresh: no rush of catch-up frames */
void emu_set_keys(emu_t *e, uint16_t pressed); /* GBA_KEY_* mask */
triple_buffer_t *emu_frames(emu_t *e);
uint64_t emu_frame_count(const emu_t *e);
void emu_destroy(emu_t *e); /* stops the thread (if started) and frees everything */

#endif
```

`platform/android/emu_thread.c`:

```c
#include "emu_thread.h"

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "frame_pacer.h"

struct emu {
    gba_t *g;
    triple_buffer_t tb;
    pthread_t thread;
    bool started;
    pthread_mutex_t lock; /* guards paused/quit */
    pthread_cond_t wake;
    bool paused, quit;
    atomic_uint keys;
    atomic_ullong frames;
};

static int64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

static void *emu_main(void *arg) {
    emu_t *e = arg;
    frame_pacer_t pacer;
    pacer_reset(&pacer);
    int16_t audio[1024 * 2];
    for (;;) {
        pthread_mutex_lock(&e->lock);
        if (e->paused && !e->quit) {
            while (e->paused && !e->quit) pthread_cond_wait(&e->wake, &e->lock);
            pacer_reset(&pacer);
        }
        bool quit = e->quit;
        pthread_mutex_unlock(&e->lock);
        if (quit) break;

        gba_set_keys(e->g, (uint16_t)atomic_load(&e->keys));
        gba_run_frame(e->g);
        memcpy(tb_back(&e->tb), gba_framebuffer(e->g), sizeof(uint16_t) * TB_PIXELS);
        tb_publish(&e->tb);
        while (gba_audio_read(e->g, audio, 1024) > 0) {
        } /* no sound output until milestone 4b */
        atomic_fetch_add(&e->frames, 1);

        int64_t due = pacer_next(&pacer, now_ns());
        struct timespec ts = {(time_t)(due / 1000000000), (long)(due % 1000000000)};
        while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) == EINTR) {
        }
    }
    return NULL;
}

emu_t *emu_create(const uint8_t *rom, size_t size, gba_err_t *err) {
    emu_t *e = calloc(1, sizeof *e);
    if (!e || !(e->g = gba_create(NULL))) {
        free(e);
        *err = GBA_ERR_NOMEM;
        return NULL;
    }
    *err = gba_load_rom(e->g, rom, size);
    if (*err != GBA_OK) {
        gba_destroy(e->g);
        free(e);
        return NULL;
    }
    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    gba_set_rtc(e->g, (int64_t)now, (int32_t)local.tm_gmtoff);
    tb_init(&e->tb);
    pthread_mutex_init(&e->lock, NULL);
    pthread_cond_init(&e->wake, NULL);
    atomic_init(&e->keys, 0u);
    atomic_init(&e->frames, 0ull);
    return e;
}

void emu_start(emu_t *e) {
    if (e->started) return;
    e->started = pthread_create(&e->thread, NULL, emu_main, e) == 0;
}

static void set_paused(emu_t *e, bool paused) {
    pthread_mutex_lock(&e->lock);
    e->paused = paused;
    pthread_cond_broadcast(&e->wake);
    pthread_mutex_unlock(&e->lock);
}

void emu_pause(emu_t *e) {
    set_paused(e, true);
}

void emu_resume(emu_t *e) {
    set_paused(e, false);
}

void emu_set_keys(emu_t *e, uint16_t pressed) {
    atomic_store(&e->keys, pressed);
}

triple_buffer_t *emu_frames(emu_t *e) {
    return &e->tb;
}

uint64_t emu_frame_count(const emu_t *e) {
    return atomic_load(&((emu_t *)e)->frames);
}

void emu_destroy(emu_t *e) {
    if (!e) return;
    pthread_mutex_lock(&e->lock);
    e->quit = true;
    pthread_cond_broadcast(&e->wake);
    pthread_mutex_unlock(&e->lock);
    if (e->started) pthread_join(e->thread, NULL);
    gba_destroy(e->g);
    pthread_mutex_destroy(&e->lock);
    pthread_cond_destroy(&e->wake);
    free(e);
}
```

Replace `platform/android/jni_bridge.c` with:

```c
/* JNI entry points for com.tengen.gbaemu.NativeBridge. Called from one thread at a time (the UI
 * thread in the app, the test thread in on-device tests); they keep one game session. */
#include <inttypes.h>
#include <jni.h>
#include <stdio.h>
#include <stdlib.h>

#include "emu_thread.h"
#include "frame_hash.h"
#include "gl_readback.h"

static emu_t *s_emu;

#define JNI_FN(name) Java_com_tengen_gbaemu_NativeBridge_##name

static void stop_session(void) {
    emu_destroy(s_emu);
    s_emu = NULL;
}

JNIEXPORT jint JNICALL JNI_FN(loadRom)(JNIEnv *env, jobject self, jobject rom) {
    (void)self;
    stop_session();
    const uint8_t *data = (*env)->GetDirectBufferAddress(env, rom);
    jlong size = (*env)->GetDirectBufferCapacity(env, rom);
    if (!data || size <= 0) return GBA_ERR_ROM_SIZE;
    gba_err_t err;
    s_emu = emu_create(data, (size_t)size, &err);
    return err;
}

JNIEXPORT void JNICALL JNI_FN(start)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    if (s_emu) emu_start(s_emu);
}

JNIEXPORT void JNICALL JNI_FN(stop)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    stop_session();
}

JNIEXPORT void JNICALL JNI_FN(pause)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    if (s_emu) emu_pause(s_emu);
}

JNIEXPORT void JNICALL JNI_FN(resume)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    if (s_emu) emu_resume(s_emu);
}

JNIEXPORT void JNICALL JNI_FN(setKeys)(JNIEnv *env, jobject self, jint pressed) {
    (void)env;
    (void)self;
    if (s_emu) emu_set_keys(s_emu, (uint16_t)(pressed & 0x3FF));
}

JNIEXPORT jlong JNICALL JNI_FN(debugFrameCount)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    return s_emu ? (jlong)emu_frame_count(s_emu) : 0;
}

/* Headless run on the calling thread, exactly like `testrunner --frames n`: "<frame hash> <audio hash>". */
JNIEXPORT jstring JNICALL JNI_FN(debugRunFrames)(JNIEnv *env, jobject self, jobject rom, jint frames) {
    (void)self;
    const uint8_t *data = (*env)->GetDirectBufferAddress(env, rom);
    jlong size = (*env)->GetDirectBufferCapacity(env, rom);
    gba_t *g = gba_create(NULL);
    if (!g || !data || gba_load_rom(g, data, (size_t)size) != GBA_OK) {
        gba_destroy(g);
        return NULL;
    }
    uint64_t audio_hash = AUDIO_HASH_INIT;
    int16_t buf[1024 * 2];
    for (jint f = 0; f < frames; f++) {
        gba_set_keys(g, 0);
        gba_run_frame(g);
        size_t n;
        while ((n = gba_audio_read(g, buf, 1024)) > 0) audio_hash = audio_hash_update(audio_hash, buf, n * 2);
    }
    char out[40];
    snprintf(out, sizeof out, "%016" PRIx64 " %016" PRIx64, frame_hash(gba_framebuffer(g)), audio_hash);
    gba_destroy(g);
    return (*env)->NewStringUTF(env, out);
}

/* Draws one frame offscreen with the real OpenGL code; RGBA pixels top row first, or null. */
JNIEXPORT jintArray JNICALL JNI_FN(debugDrawFrame)(JNIEnv *env, jobject self, jshortArray pixels, jint width,
                                                   jint height) {
    (void)self;
    if ((*env)->GetArrayLength(env, pixels) != 240 * 160 || width <= 0 || height <= 0) return NULL;
    jshort *px = (*env)->GetShortArrayElements(env, pixels, NULL);
    uint32_t *out = malloc((size_t)width * (size_t)height * 4);
    bool ok = out && gl_readback((const uint16_t *)px, width, height, out);
    (*env)->ReleaseShortArrayElements(env, pixels, px, JNI_ABORT);
    jintArray result = NULL;
    if (ok) {
        result = (*env)->NewIntArray(env, width * height);
        (*env)->SetIntArrayRegion(env, result, 0, width * height, (const jint *)out);
    }
    free(out);
    return result;
}
```

Replace `app/src/main/java/com/tengen/gbaemu/NativeBridge.kt` with:

```kotlin
package com.tengen.gbaemu

import java.nio.ByteBuffer

/** The C side (platform/android/jni_bridge.c). One game session at a time; call from one thread. */
object NativeBridge {
    init {
        System.loadLibrary("gbaemu")
    }

    /** Creates the emulator with this ROM (a direct buffer; the core copies it). 0 = OK, else a gba_err_t. */
    external fun loadRom(rom: ByteBuffer): Int

    /** Starts the emulation and render threads for the loaded ROM. */
    external fun start()

    /** Stops both threads and frees the emulator. */
    external fun stop()

    external fun pause()

    external fun resume()

    /** Held GBA buttons (KeyMap bits). */
    external fun setKeys(pressed: Int)

    // ---- test-only ----

    /** Frames the emulation thread has run since loadRom. */
    external fun debugFrameCount(): Long

    /** Runs a ROM headless for n frames on this thread: "<frame hash> <audio hash>" like the PC test runner. */
    external fun debugRunFrames(rom: ByteBuffer, frames: Int): String?

    /** Draws one 240x160 BGR555 frame offscreen; RGBA pixels (r | g<<8 | b<<16 | a<<24), top row first. */
    external fun debugDrawFrame(pixels: ShortArray, width: Int, height: Int): IntArray?
}
```

- [x] **Step 4: Run the tests (three times — they involve real time)**

```bash
for i in 1 2 3; do bash tools/dev.sh androidtest 2>&1 | tail -1; grep -h -o 'tests="[0-9]*" failures="[0-9]*"' app/build/outputs/androidTest-results/connected/debug/*.xml; done
```

Expected: `tests="7" failures="0"` all three times. (If `runsAtGbaSpeed` reports ~250 frames instead of ~299, the native code is not optimised: check `CMAKE_C_FLAGS_DEBUG` in `platform/android/CMakeLists.txt` — the prototype measured 49 fps without it.)

- [x] **Step 5: Commit**

```bash
git add app/src/androidTest/java/com/tengen/gbaemu/SessionTest.kt platform/android/emu_thread.h platform/android/emu_thread.c platform/android/jni_bridge.c app/src/main/java/com/tengen/gbaemu/NativeBridge.kt docs/PROGRESS.md docs/superpowers/plans/2026-09-26-milestone-4a-app-picture.md
git commit -m "feat(android): emulation thread at 59.7275 frames/s with pause and resume" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

**Batch 2 (Tasks 4-6) done — checkpoint with the user.**

---
### Task 7: The render thread

**Files:**
- Create: `platform/android/render_thread.h`, `platform/android/render_thread.c`
- Modify: `app/src/androidTest/java/com/tengen/gbaemu/SessionTest.kt`, `platform/android/jni_bridge.c`, `app/src/main/java/com/tengen/gbaemu/NativeBridge.kt`

- [x] **Step 1: Write the failing test**

In `SessionTest.kt` add the imports `android.graphics.PixelFormat`, `android.media.ImageReader` and `org.junit.Assert.assertNotNull`, change the class comment to `/** The emulation and render threads: speed, pause/resume, window hand-over, bad ROMs. */`, and add before `rejectsAnEmptyRom`:

```kotlin
    @Test
    fun drawsIntoAWindowAndLetsGoOfIt() {
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("jsmolka/stripes.gba"))) /* coloured from the first frames */
        NativeBridge.start()
        val reader = ImageReader.newInstance(480, 320, PixelFormat.RGBA_8888, 3)
        assertTrue("render thread could not draw into the window", NativeBridge.setSurface(reader.surface))
        Thread.sleep(500)
        val image = reader.acquireLatestImage()
        assertNotNull("nothing was drawn", image)
        val row = image!!.planes[0].buffer
        var lit = 0
        for (i in 0 until 480 * 4 step 4) if ((row.get(160 * image.planes[0].rowStride + i).toInt() and 0xFF) != 0) lit++
        image.close()
        assertTrue("the middle row is all black", lit > 0)
        assertTrue(NativeBridge.setSurface(null))
        reader.close()
    }
```

The whole file is then:

```kotlin
package com.tengen.gbaemu

import android.graphics.PixelFormat
import android.media.ImageReader
import android.os.SystemClock
import androidx.test.ext.junit.runners.AndroidJUnit4
import java.nio.ByteBuffer
import kotlin.math.abs
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** The emulation and render threads: speed, pause/resume, window hand-over, bad ROMs. */
@RunWith(AndroidJUnit4::class)
class SessionTest {
    @After
    fun stopSession() = NativeBridge.stop()

    @Test
    fun runsAtGbaSpeed() {
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba")))
        NativeBridge.start()
        Thread.sleep(500)
        val startFrames = NativeBridge.debugFrameCount()
        val startMs = SystemClock.elapsedRealtime()
        Thread.sleep(5000)
        val frames = NativeBridge.debugFrameCount() - startFrames
        val expected = (SystemClock.elapsedRealtime() - startMs) * 59.7275 / 1000.0
        assertTrue("ran $frames frames, expected about $expected", abs(frames - expected) <= 3.0)
    }

    @Test
    fun survivesTwentyStartPauseResumeStopRounds() {
        repeat(20) { round ->
            assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba")))
            NativeBridge.start()
            Thread.sleep(40)
            NativeBridge.pause()
            Thread.sleep(20) /* a frame already running when pause came may still finish */
            val paused = NativeBridge.debugFrameCount()
            Thread.sleep(60)
            assertEquals("round $round: frames while paused", paused, NativeBridge.debugFrameCount())
            NativeBridge.resume()
            Thread.sleep(60)
            assertTrue("round $round: no frames after resume", NativeBridge.debugFrameCount() > paused)
            NativeBridge.stop()
        }
    }

    @Test
    fun drawsIntoAWindowAndLetsGoOfIt() {
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("jsmolka/stripes.gba"))) /* coloured from the first frames */
        NativeBridge.start()
        val reader = ImageReader.newInstance(480, 320, PixelFormat.RGBA_8888, 3)
        assertTrue("render thread could not draw into the window", NativeBridge.setSurface(reader.surface))
        Thread.sleep(500)
        val image = reader.acquireLatestImage()
        assertNotNull("nothing was drawn", image)
        val row = image!!.planes[0].buffer
        var lit = 0
        for (i in 0 until 480 * 4 step 4) if ((row.get(160 * image.planes[0].rowStride + i).toInt() and 0xFF) != 0) lit++
        image.close()
        assertTrue("the middle row is all black", lit > 0)
        assertTrue(NativeBridge.setSurface(null))
        reader.close()
    }

    @Test
    fun rejectsAnEmptyRom() {
        assertNotEquals(0, NativeBridge.loadRom(ByteBuffer.allocateDirect(0)))
    }
}
```

- [x] **Step 2: Run to see it fail**

```bash
bash tools/dev.sh androidtest
```

Expected: FAIL — `Unresolved reference 'setSurface'`.

- [x] **Step 3: The render thread**

`platform/android/render_thread.h`:

```c
#ifndef GBAEMU_RENDER_THREAD_H
#define GBAEMU_RENDER_THREAD_H

#include <android/native_window.h>
#include <stdbool.h>

#include "triple_buffer.h"

/* The thread that owns OpenGL: once per display refresh (AChoreographer) it draws the newest
 * frame from the triple buffer into the current window. */
typedef struct render_thread render_thread_t;

render_thread_t *render_create(triple_buffer_t *tb);
/* The window appeared, changed (w) or went away (NULL). Returns only after the render thread has
 * switched over (and let go of the old window); false if OpenGL ES 3 could not draw into w. */
bool render_set_window(render_thread_t *r, ANativeWindow *w);
void render_destroy(render_thread_t *r);

#endif
```

`platform/android/render_thread.c`:

```c
#include "render_thread.h"

#include <android/choreographer.h>
#include <android/log.h>
#include <android/looper.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

#include "egl_ctx.h"
#include "video_gl.h"

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "gbaemu", __VA_ARGS__)

struct render_thread {
    pthread_t thread;
    triple_buffer_t *tb;
    int pipe_fd[2]; /* messages to the thread: 'W' window changed, 'Q' quit */

    pthread_mutex_t lock; /* guards pending/requested/handled/last_ok */
    pthread_cond_t done;
    ANativeWindow *pending;
    unsigned requested, handled;
    bool last_ok;

    /* Only the render thread touches these. */
    AChoreographer *choreographer;
    egl_ctx_t egl;
    bool egl_ready, gl_ready, frame_posted, quit;
    EGLSurface surface;
    ANativeWindow *window;
    video_gl_t gl;
};

static void post_frame(render_thread_t *r);

static void drop_surface(render_thread_t *r) {
    if (r->surface != EGL_NO_SURFACE) {
        eglMakeCurrent(r->egl.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(r->egl.display, r->surface);
        r->surface = EGL_NO_SURFACE;
    }
    if (r->window) ANativeWindow_release(r->window);
    r->window = NULL;
}

/* Tear down the whole context (it was lost, or the thread is ending); GL objects go with it. */
static void drop_context(render_thread_t *r) {
    if (r->surface != EGL_NO_SURFACE) {
        eglMakeCurrent(r->egl.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(r->egl.display, r->surface);
        r->surface = EGL_NO_SURFACE;
    }
    if (r->egl_ready) egl_ctx_destroy(&r->egl);
    r->egl_ready = r->gl_ready = false;
}

/* Surface for r->window; context and GL objects made on first use. */
static bool make_surface(render_thread_t *r) {
    if (!r->egl_ready) {
        r->egl_ready = egl_ctx_init(&r->egl, false);
        if (!r->egl_ready) return false;
    }
    r->surface = eglCreateWindowSurface(r->egl.display, r->egl.config, r->window, NULL);
    if (r->surface == EGL_NO_SURFACE || !eglMakeCurrent(r->egl.display, r->surface, r->surface, r->egl.context)) {
        LOGE("window surface failed: 0x%x", eglGetError());
        return false;
    }
    if (!r->gl_ready) {
        r->gl_ready = video_gl_init(&r->gl);
        if (!r->gl_ready) return false;
        bool fresh;
        video_gl_upload(&r->gl, tb_take(r->tb, &fresh)); /* show the newest frame straight away */
    }
    post_frame(r);
    return true;
}

static bool apply_window(render_thread_t *r, ANativeWindow *w) {
    if (w && w == r->window) { /* same window, new size: drawing reads the size every frame */
        ANativeWindow_release(w);
        return r->gl_ready && r->surface != EGL_NO_SURFACE;
    }
    drop_surface(r);
    if (!w) return true;
    r->window = w;
    return make_surface(r);
}

static void on_frame(render_thread_t *r) {
    r->frame_posted = false;
    if (r->quit || r->surface == EGL_NO_SURFACE || !r->gl_ready) return;
    bool fresh;
    const uint16_t *frame = tb_take(r->tb, &fresh);
    if (fresh) video_gl_upload(&r->gl, frame);
    EGLint w = 0, h = 0;
    eglQuerySurface(r->egl.display, r->surface, EGL_WIDTH, &w);
    eglQuerySurface(r->egl.display, r->surface, EGL_HEIGHT, &h);
    video_gl_draw(&r->gl, w, h);
    if (!eglSwapBuffers(r->egl.display, r->surface) && eglGetError() == EGL_CONTEXT_LOST) {
        LOGE("EGL context lost: rebuilding");
        drop_context(r);
        if (!make_surface(r)) return;
    }
    post_frame(r);
}

static void frame_cb64(int64_t frame_time_ns, void *data) {
    (void)frame_time_ns;
    on_frame(data);
}

static void frame_cb(long frame_time_ns, void *data) {
    (void)frame_time_ns;
    on_frame(data);
}

static void post_frame(render_thread_t *r) {
    if (r->frame_posted) return;
    r->frame_posted = true;
    if (__builtin_available(android 29, *)) {
        AChoreographer_postFrameCallback64(r->choreographer, frame_cb64, r);
    } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        AChoreographer_postFrameCallback(r->choreographer, frame_cb, r);
#pragma clang diagnostic pop
    }
}

static int on_message(int fd, int events, void *data) {
    (void)events;
    render_thread_t *r = data;
    char msg;
    if (read(fd, &msg, 1) != 1) return 1;
    if (msg == 'Q') {
        r->quit = true;
        return 1;
    }
    pthread_mutex_lock(&r->lock);
    ANativeWindow *w = r->pending;
    r->pending = NULL;
    unsigned seq = r->requested;
    pthread_mutex_unlock(&r->lock);
    bool ok = apply_window(r, w);
    pthread_mutex_lock(&r->lock);
    r->handled = seq;
    r->last_ok = ok;
    pthread_cond_broadcast(&r->done);
    pthread_mutex_unlock(&r->lock);
    return 1;
}

static void *render_main(void *arg) {
    render_thread_t *r = arg;
    ALooper *looper = ALooper_prepare(0);
    ALooper_addFd(looper, r->pipe_fd[0], 1, ALOOPER_EVENT_INPUT, on_message, r);
    r->choreographer = AChoreographer_getInstance();
    while (!r->quit) ALooper_pollOnce(-1, NULL, NULL, NULL);
    ALooper_removeFd(looper, r->pipe_fd[0]);
    drop_surface(r);
    drop_context(r);
    return NULL;
}

render_thread_t *render_create(triple_buffer_t *tb) {
    render_thread_t *r = calloc(1, sizeof *r);
    if (!r) return NULL;
    r->tb = tb;
    r->surface = EGL_NO_SURFACE;
    if (pipe(r->pipe_fd) != 0) {
        free(r);
        return NULL;
    }
    pthread_mutex_init(&r->lock, NULL);
    pthread_cond_init(&r->done, NULL);
    pthread_create(&r->thread, NULL, render_main, r);
    return r;
}

bool render_set_window(render_thread_t *r, ANativeWindow *w) {
    if (w) ANativeWindow_acquire(w); /* the render thread owns this reference now */
    pthread_mutex_lock(&r->lock);
    r->pending = w;
    unsigned seq = ++r->requested;
    pthread_mutex_unlock(&r->lock);
    char msg = 'W';
    write(r->pipe_fd[1], &msg, 1);
    pthread_mutex_lock(&r->lock);
    while (r->handled != seq) pthread_cond_wait(&r->done, &r->lock);
    bool ok = r->last_ok;
    pthread_mutex_unlock(&r->lock);
    return ok;
}

void render_destroy(render_thread_t *r) {
    if (!r) return;
    char msg = 'Q';
    write(r->pipe_fd[1], &msg, 1);
    pthread_join(r->thread, NULL);
    close(r->pipe_fd[0]);
    close(r->pipe_fd[1]);
    pthread_mutex_destroy(&r->lock);
    pthread_cond_destroy(&r->done);
    free(r);
}
```

Replace `platform/android/jni_bridge.c` with the final version:

```c
/* JNI entry points for com.tengen.gbaemu.NativeBridge. Called from one thread at a time (the UI
 * thread in the app, the test thread in on-device tests); they keep one game session. */
#include <android/native_window_jni.h>
#include <inttypes.h>
#include <jni.h>
#include <stdio.h>
#include <stdlib.h>

#include "emu_thread.h"
#include "frame_hash.h"
#include "gl_readback.h"
#include "render_thread.h"

static emu_t *s_emu;
static render_thread_t *s_render;
static ANativeWindow *s_window; /* a window that arrived before start() */

#define JNI_FN(name) Java_com_tengen_gbaemu_NativeBridge_##name

static void stop_session(void) {
    render_destroy(s_render); /* first: it reads the emulator's frames */
    s_render = NULL;
    emu_destroy(s_emu);
    s_emu = NULL;
}

JNIEXPORT jint JNICALL JNI_FN(loadRom)(JNIEnv *env, jobject self, jobject rom) {
    (void)self;
    stop_session();
    const uint8_t *data = (*env)->GetDirectBufferAddress(env, rom);
    jlong size = (*env)->GetDirectBufferCapacity(env, rom);
    if (!data || size <= 0) return GBA_ERR_ROM_SIZE;
    gba_err_t err;
    s_emu = emu_create(data, (size_t)size, &err);
    return err;
}

JNIEXPORT void JNICALL JNI_FN(start)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    if (!s_emu) return;
    if (!s_render) s_render = render_create(emu_frames(s_emu));
    if (s_render && s_window) {
        render_set_window(s_render, s_window);
        ANativeWindow_release(s_window);
        s_window = NULL;
    }
    emu_start(s_emu);
}

JNIEXPORT jboolean JNICALL JNI_FN(setSurface)(JNIEnv *env, jobject self, jobject surface) {
    (void)self;
    ANativeWindow *w = surface ? ANativeWindow_fromSurface(env, surface) : NULL;
    if (!s_render) { /* not started yet: hand it over in start() */
        if (s_window) ANativeWindow_release(s_window);
        s_window = w;
        return JNI_TRUE;
    }
    bool ok = render_set_window(s_render, w);
    if (w) ANativeWindow_release(w);
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL JNI_FN(stop)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    stop_session();
    if (s_window) ANativeWindow_release(s_window);
    s_window = NULL;
}

JNIEXPORT void JNICALL JNI_FN(pause)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    if (s_emu) emu_pause(s_emu);
}

JNIEXPORT void JNICALL JNI_FN(resume)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    if (s_emu) emu_resume(s_emu);
}

JNIEXPORT void JNICALL JNI_FN(setKeys)(JNIEnv *env, jobject self, jint pressed) {
    (void)env;
    (void)self;
    if (s_emu) emu_set_keys(s_emu, (uint16_t)(pressed & 0x3FF));
}

JNIEXPORT jlong JNICALL JNI_FN(debugFrameCount)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    return s_emu ? (jlong)emu_frame_count(s_emu) : 0;
}

/* Headless run on the calling thread, exactly like `testrunner --frames n`: "<frame hash> <audio hash>". */
JNIEXPORT jstring JNICALL JNI_FN(debugRunFrames)(JNIEnv *env, jobject self, jobject rom, jint frames) {
    (void)self;
    const uint8_t *data = (*env)->GetDirectBufferAddress(env, rom);
    jlong size = (*env)->GetDirectBufferCapacity(env, rom);
    gba_t *g = gba_create(NULL);
    if (!g || !data || gba_load_rom(g, data, (size_t)size) != GBA_OK) {
        gba_destroy(g);
        return NULL;
    }
    uint64_t audio_hash = AUDIO_HASH_INIT;
    int16_t buf[1024 * 2];
    for (jint f = 0; f < frames; f++) {
        gba_set_keys(g, 0);
        gba_run_frame(g);
        size_t n;
        while ((n = gba_audio_read(g, buf, 1024)) > 0) audio_hash = audio_hash_update(audio_hash, buf, n * 2);
    }
    char out[40];
    snprintf(out, sizeof out, "%016" PRIx64 " %016" PRIx64, frame_hash(gba_framebuffer(g)), audio_hash);
    gba_destroy(g);
    return (*env)->NewStringUTF(env, out);
}

/* Draws one frame offscreen with the real OpenGL code; RGBA pixels top row first, or null. */
JNIEXPORT jintArray JNICALL JNI_FN(debugDrawFrame)(JNIEnv *env, jobject self, jshortArray pixels, jint width,
                                                   jint height) {
    (void)self;
    if ((*env)->GetArrayLength(env, pixels) != 240 * 160 || width <= 0 || height <= 0) return NULL;
    jshort *px = (*env)->GetShortArrayElements(env, pixels, NULL);
    uint32_t *out = malloc((size_t)width * (size_t)height * 4);
    bool ok = out && gl_readback((const uint16_t *)px, width, height, out);
    (*env)->ReleaseShortArrayElements(env, pixels, px, JNI_ABORT);
    jintArray result = NULL;
    if (ok) {
        result = (*env)->NewIntArray(env, width * height);
        (*env)->SetIntArrayRegion(env, result, 0, width * height, (const jint *)out);
    }
    free(out);
    return result;
}
```

Replace `app/src/main/java/com/tengen/gbaemu/NativeBridge.kt` with the final version:

```kotlin
package com.tengen.gbaemu

import android.view.Surface
import java.nio.ByteBuffer

/** The C side (platform/android/jni_bridge.c). One game session at a time; call from one thread. */
object NativeBridge {
    init {
        System.loadLibrary("gbaemu")
    }

    /** Creates the emulator with this ROM (a direct buffer; the core copies it). 0 = OK, else a gba_err_t. */
    external fun loadRom(rom: ByteBuffer): Int

    /** Starts the emulation and render threads for the loaded ROM. */
    external fun start()

    /** Stops both threads and frees the emulator. */
    external fun stop()

    external fun pause()

    external fun resume()

    /** The game's window appeared/changed (or went away: null). False if OpenGL ES 3 cannot draw into it. */
    external fun setSurface(surface: Surface?): Boolean

    /** Held GBA buttons (KeyMap bits). */
    external fun setKeys(pressed: Int)

    // ---- test-only ----

    /** Frames the emulation thread has run since loadRom. */
    external fun debugFrameCount(): Long

    /** Runs a ROM headless for n frames on this thread: "<frame hash> <audio hash>" like the PC test runner. */
    external fun debugRunFrames(rom: ByteBuffer, frames: Int): String?

    /** Draws one 240x160 BGR555 frame offscreen; RGBA pixels (r | g<<8 | b<<16 | a<<24), top row first. */
    external fun debugDrawFrame(pixels: ShortArray, width: Int, height: Int): IntArray?
}
```

- [x] **Step 4: Run the tests (three times)**

```bash
for i in 1 2 3; do bash tools/dev.sh androidtest 2>&1 | tail -1; grep -h -o 'tests="[0-9]*" failures="[0-9]*"' app/build/outputs/androidTest-results/connected/debug/*.xml; done
bash tools/dev.sh apk --rerun-tasks 2>&1 | grep -c "warning:.*\.[ch]:"
```

Expected: `tests="8" failures="0"` three times; `0` C warnings.

- [x] **Step 5: Commit**

```bash
git add platform/android/render_thread.h platform/android/render_thread.c app/src/androidTest/java/com/tengen/gbaemu/SessionTest.kt platform/android/jni_bridge.c app/src/main/java/com/tengen/gbaemu/NativeBridge.kt docs/PROGRESS.md docs/superpowers/plans/2026-09-26-milestone-4a-app-picture.md
git commit -m "feat(android): render thread drawing the newest frame on every display refresh" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 8: The app (start screen, file picker, game screen, keys, lifecycle)

**Files:**
- Create: `app/src/main/java/com/tengen/gbaemu/Screens.kt`
- Modify: `app/src/main/java/com/tengen/gbaemu/MainActivity.kt` (replace the placeholder)

- [x] **Step 1: The screens**

`app/src/main/java/com/tengen/gbaemu/Screens.kt`:

```kotlin
package com.tengen.gbaemu

import android.view.SurfaceHolder
import android.view.SurfaceView
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView

/** "Open ROM" button, plus a message when the last attempt failed. */
@Composable
fun StartScreen(message: String?, onOpenRom: () -> Unit) {
    Surface(modifier = Modifier.fillMaxSize()) { /* the theme's background and text colours */
        Column(
            modifier = Modifier.fillMaxSize().padding(24.dp),
            verticalArrangement = Arrangement.Center,
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Text("GBA Emulator", style = MaterialTheme.typography.headlineMedium)
            Spacer(Modifier.height(24.dp))
            Button(onClick = onOpenRom) { Text("Open ROM") }
            if (message != null) {
                Spacer(Modifier.height(16.dp))
                Text(message, color = MaterialTheme.colorScheme.error)
            }
        }
    }
}

/** The game: a full-screen SurfaceView whose window is handed to the native render thread. */
@Composable
fun GameScreen(onDrawFailed: () -> Unit) {
    AndroidView(
        modifier = Modifier.fillMaxSize().background(Color.Black),
        factory = { context ->
            SurfaceView(context).apply {
                holder.addCallback(object : SurfaceHolder.Callback {
                    override fun surfaceCreated(holder: SurfaceHolder) {
                        if (!NativeBridge.setSurface(holder.surface)) onDrawFailed()
                    }

                    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
                        if (!NativeBridge.setSurface(holder.surface)) onDrawFailed()
                    }

                    override fun surfaceDestroyed(holder: SurfaceHolder) {
                        NativeBridge.setSurface(null)
                    }
                })
            }
        },
    )
}
```

`app/src/main/java/com/tengen/gbaemu/MainActivity.kt`:

```kotlin
package com.tengen.gbaemu

import android.net.Uri
import android.os.Bundle
import android.view.KeyEvent
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import java.nio.ByteBuffer

class MainActivity : ComponentActivity() {
    private var playing by mutableStateOf(false)
    private var message by mutableStateOf<String?>(null)
    private var keys = 0

    private val openRom = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) load(uri)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            MaterialTheme(colorScheme = darkColorScheme()) {
                if (playing) {
                    GameScreen(onDrawFailed = {
                        stopGame()
                        message = "This device cannot show the game: OpenGL ES 3.0 failed."
                    })
                } else {
                    StartScreen(message) { openRom.launch(arrayOf("*/*")) }
                }
                BackHandler(enabled = playing) { stopGame() }
            }
        }
    }

    private fun load(uri: Uri) {
        val bytes = try {
            contentResolver.openInputStream(uri)?.use { it.readBytes() }
        } catch (e: Exception) {
            null
        }
        if (bytes == null) {
            message = "Could not read that file."
            return
        }
        val rom = ByteBuffer.allocateDirect(bytes.size).put(bytes)
        rom.flip()
        if (NativeBridge.loadRom(rom) != 0) {
            message = "Not a GBA ROM (size 0 or over 32 MiB)."
            return
        }
        message = null
        keys = 0
        NativeBridge.start()
        playing = true
        setFullScreen(true)
    }

    private fun stopGame() {
        NativeBridge.stop()
        playing = false
        setFullScreen(false)
    }

    private fun setFullScreen(on: Boolean) {
        val bars = WindowCompat.getInsetsController(window, window.decorView)
        if (on) {
            bars.systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            bars.hide(WindowInsetsCompat.Type.systemBars())
            window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        } else {
            bars.show(WindowInsetsCompat.Type.systemBars())
            window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        }
    }

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (playing && KeyMap.bitFor(event.keyCode) != 0) {
            when (event.action) {
                KeyEvent.ACTION_DOWN -> keys = KeyMap.update(keys, event.keyCode, true)
                KeyEvent.ACTION_UP -> keys = KeyMap.update(keys, event.keyCode, false)
            }
            NativeBridge.setKeys(keys)
            return true
        }
        return super.dispatchKeyEvent(event)
    }

    override fun onPause() {
        super.onPause()
        NativeBridge.pause()
    }

    override fun onResume() {
        super.onResume()
        NativeBridge.resume()
    }

    override fun onDestroy() {
        if (isFinishing) NativeBridge.stop()
        super.onDestroy()
    }
}
```

(Dark colour scheme + `Surface`: without them the prototype's title was black on the dark window.)

- [x] **Step 2: Build and run every test**

```bash
bash tools/dev.sh apk && bash tools/dev.sh jvmtest && bash tools/dev.sh androidtest && bash tools/dev.sh unit && bash tools/dev.sh test
```

Expected: all green — Kotlin 4, on-device 8, PC 336, CTest 56/56.

- [x] **Step 3: Claude drives the app on the phone emulator (adb) and inspects screenshots**

Use a scratchpad folder `$SP` for screenshots. Helpers for this step:

```bash
ADB_EXE="$(cygpath -u "$LOCALAPPDATA")/Android/Sdk/platform-tools/adb"
EMU=$(MSYS_NO_PATHCONV=1 "$ADB_EXE" devices | awk '$1 ~ /^emulator-/ && $2 == "device" { print $1; exit }')
ADB() { MSYS_NO_PATHCONV=1 "$ADB_EXE" -s "$EMU" "$@"; }   # the emulator only, even with a phone plugged in
shot() { ADB exec-out screencap -p > "$SP/$1.png"; }
tap_on() {  # tap the centre of the first element whose $1 (text or content-desc) is exactly $2; waits up to 10 s
    for i in 1 2 3 4 5 6 7 8 9 10; do
        ADB shell uiautomator dump /sdcard/ui.xml >/dev/null
        b=$(ADB shell cat /sdcard/ui.xml | tr '>' '\n' | grep -F "$1=\"$2\"" | grep -oE 'bounds="\[[0-9]+,[0-9]+\]\[[0-9]+,[0-9]+\]"' | head -1)
        if [ -n "$b" ]; then
            set -- $(echo "$b" | grep -oE '[0-9]+')
            ADB shell input tap $((($1 + $3) / 2)) $((($2 + $4) / 2))
            return 0
        fi
        sleep 1
    done
    echo "not on screen: $1=$2" >&2
    return 1
}
pick_download() {  # in the open file picker: show Downloads and pick file $1 (retries while the picker settles)
    for try in 1 2 3; do
        if ADB shell uiautomator dump /sdcard/ui.xml >/dev/null && ADB shell cat /sdcard/ui.xml | grep -qF "text=\"$1\""; then
            tap_on text "$1"
            return
        fi
        tap_on content-desc "Show roots" && sleep 2 && tap_on text "Downloads" && sleep 2
    done
    echo "could not pick $1 from Downloads" >&2
    return 1
}
```

Then:

```bash
bash tools/dev.sh push tests/roms/tonc/m7_demo.gba
ADB shell input keyevent KEYCODE_HOME; ADB shell am force-stop com.tengen.gbaemu; ADB shell am force-stop com.google.android.documentsui   # a clean start
bash tools/dev.sh install && bash tools/dev.sh launch && sleep 3 && shot start
tap_on text "Open ROM" && pick_download m7_demo.gba && sleep 3 && shot game
ADB shell input keyevent --longpress KEYCODE_DPAD_UP; ADB shell input keyevent --longpress KEYCODE_DPAD_UP; sleep 1; shot key
ADB shell settings put system accelerometer_rotation 0; ADB shell settings put system user_rotation 1; sleep 3; shot landscape
ADB shell input keyevent KEYCODE_HOME; sleep 2; bash tools/dev.sh launch; sleep 2; shot resumed
ADB shell settings put system user_rotation 0
ADB logcat -d -s AndroidRuntime:E gbaemu:* | tail   # crashes and our own errors
```

Expected (look at every screenshot): `start` — "GBA Emulator" in light text on a dark background, an "Open ROM" button; `game` — the Mode 7 demo full width in portrait, centred, sharp pixels, black above and below (Android may show its one-time "Viewing full screen" tip — tap it away with `tap_on text "Got it"` and take `shot game` again); `key` — the picture changed (Up moves the camera); `landscape` — full height with black bars left and right; `resumed` — the game again, still running; the log shows no crash (`AndroidRuntime`) and no `gbaemu` errors. (If `tap_on` reports `not on screen`, the picker's layout differs: `ADB shell uiautomator dump /sdcard/ui.xml; ADB shell cat /sdcard/ui.xml` and read the texts.)

- [x] **Step 4: Commit**

```bash
git add app/src/main/java/com/tengen/gbaemu/Screens.kt app/src/main/java/com/tengen/gbaemu/MainActivity.kt docs/PROGRESS.md docs/superpowers/plans/2026-09-26-milestone-4a-app-picture.md
git commit -m "feat(android): start screen, file picker, full-screen game, keys and pause on leave" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

**Batch 3 (Tasks 7-8) done — checkpoint with the user.**

---
### Task 9: User check and milestone 4a exit

- [x] **Step 1: Full run**

```bash
bash tools/dev.sh test && bash tools/dev.sh unit && bash tools/dev.sh jvmtest && bash tools/dev.sh androidtest
```

Expected: CTest 56/56 (sanitizers on, no reports), 336 unit tests, Kotlin 4, on-device 8.

- [x] **Step 2: User check on the phone emulator**

1. `bash tools/dev.sh push tests/roms/tonc/obj_demo.gba` (PowerShell users: `& "C:\Program Files\Git\bin\bash.exe" tools/dev.sh push tests/roms/tonc/obj_demo.gba`), then `bash tools/dev.sh install` and `bash tools/dev.sh launch`.
2. The user taps **Open ROM** → (☰) → **Downloads** → `obj_demo.gba`; arrow keys on the PC keyboard move the game L; the picture looks right and runs smoothly; rotating the emulator (its side toolbar) keeps it running; Home and back pauses and resumes; the Back button returns to the start screen.
3. **Only with the user's OK:** copy their game A hack onto the emulator (`bash tools/dev.sh push "~/Downloads/hack-1.gba"`) and open it the same way: title screen and menus, walking with the arrow keys, X/Z as A/B, Enter as Start. There is no sound yet (4b) and in-game saving is not kept yet (4c) — say so before they try.

- [x] **Step 3: Docs**

`docs/PROGRESS.md` (4a ✅ with the numbers and the user's checks; Next = milestone 4b brainstorming — sound); spec status line → `Implemented — milestone 4a complete <date> (tag milestone-4a)`; parent spec §10 row 4 → 4a ✅; `CLAUDE.md` key files → 4b needs brainstorming; tick every box here.

- [x] **Step 4: Commit, tag, merge** — ask the user how to integrate (every earlier milestone: fast-forward merge into `main`); then superpowers:finishing-a-development-branch, `bash tools/dev.sh test` on `main`, delete the branch, record the merge in PROGRESS.md; delete the prototype (`git worktree remove --force <scratchpad>/p4; git branch -D proto-4a`) with the user's OK.

```bash
git add -A docs CLAUDE.md
git commit -m "docs: milestone 4a complete - Android app + picture" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git tag milestone-4a
```
