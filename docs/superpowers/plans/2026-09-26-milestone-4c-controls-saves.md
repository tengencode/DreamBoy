# Milestone 4c — Touch controls, save files, leaving and returning — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. (This project always executes plans inline with superpowers:executing-plans — see `CLAUDE.md`.)

**Goal:** On the Pixel 9 phone emulator the user plays with on-screen buttons only, the game's saves live in the app (written safely, soon after the game saves and whenever the app is left), a PC `.sav` can be imported, and leaving/returning loses nothing.

**Architecture:** Two plain-C pieces tested on the PC (`save_file` — load and write safely with a `.bak`; `save_timer` — write ~1 s after the game's last save, or at once on a flush). The emulation thread owns the save: it loads the file at create, writes it via the timer, flushes on pause/stop, and `emu_pause` waits for that flush (≤ 1 s). Kotlin: `TouchLayout` (pure maths, layout A), `TouchControlsView` (multi-touch → button mask, vibration, hide/show), `KeyMap.bindings` (keys + the Controls table from one list), `GameId` (CRC32 → save file name), `LastGame` (Continue), and the app flow in `Screens` + `MainActivity` (import question, Back question). `MainActivity` becomes `singleTask`.

**Tech Stack:** C11, Android NDK (JNI), Kotlin, Jetpack Compose + Material 3, a custom `View`, JUnit 4 + androidx.test.

**Spec:** `docs/superpowers/specs/2026-09-26-milestone-4c-controls-saves-design.md`

---

## Conventions (same as 4a/4b)

- Git Bash from the repo root. PC: `bash tools/dev.sh <configure|unit [filter]|test>`. Android: `bash tools/dev.sh <apk|jvmtest|emu|install|launch|androidtest|push <file>>` — Gradle `--offline` with Android Studio's JBR; a missing artifact → stop and ask the user before any download.
- **Only the phone emulator, never a USB phone:** `dev.sh` exports `ANDROID_SERIAL=<emulator>`; never run `./gradlew connected…`/`installDebug` or `adb` without it (the user's phone may be plugged in).
- The on-device tests read the test ROMs from `tests/roms/` and `tests/own_roms/` (packed into the test APK by `copyTestRoms`).
- Unit tests: `TEST(name)` in `tests/unit/*.c`; re-run `bash tools/dev.sh configure` after adding a test file. Test names must not equal any function name.
- One commit per task; messages end with `-m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"`. Each commit also includes `docs/PROGRESS.md` and this plan (ticked boxes) — project rule in `CLAUDE.md`.
- Branch `milestone-4c` (already created; the spec commits are on it).
- Checkpoints with the user after Tasks 2 and 4; Task 6 is the user's check and the milestone exit.
- **The app now keeps the user's saves on the emulator.** From Task 2 on, `androidtest` leaves the app installed (`gradle.properties`), so a test run no longer uninstalls it and wipes its data — found while replaying this plan: Gradle's on-device tests uninstall the app afterwards, which deleted the user's trial progress. Never uninstall the app, `pm clear` it or delete its `files/saves` without the user's OK.
- This plan's code is the prototype's (worktree `<scratchpad>/p4c`, branch `proto-4c`), which the user tried on the emulator (2026-09-26).

## File map

```
platform/android/save_file.{h,c}, save_timer.{h,c}     Task 1 (plain C; tests/unit/test_saves.c)
platform/android/emu_thread.{h,c}, jni_bridge.c          Task 2 (save path, load, timer, flush, waiting pause, import)
app/src/main/java/com/tengen/gbaemu/NativeBridge.kt      Task 2
app/src/androidTest/java/com/tengen/gbaemu/SaveTest.kt   Task 2
app/src/main/java/com/tengen/gbaemu/TouchLayout.kt, KeyMap.kt, GameId.kt   Task 3 (JVM tests in app/src/test)
app/src/main/java/com/tengen/gbaemu/TouchControlsView.kt Task 4
app/src/androidTest/java/com/tengen/gbaemu/TouchTest.kt  Task 4
app/src/main/java/com/tengen/gbaemu/LastGame.kt, Screens.kt, MainActivity.kt   Task 5
app/src/main/AndroidManifest.xml, tools/dev.sh           Task 5 (singleTask; launch like the icon)
tests/unit/CMakeLists.txt                                Task 1
```

---
### Task 1: Save files on disk and when to write them

**Files:**
- Create: `tests/unit/test_saves.c`, `platform/android/save_file.h`, `platform/android/save_file.c`, `platform/android/save_timer.h`, `platform/android/save_timer.c`
- Modify: `tests/unit/CMakeLists.txt`

- [x] **Step 1: Write the failing tests**

`tests/unit/test_saves.c`:

```c
/* The Android save files' plain-C pieces (platform/android): writing safely, and when to write. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define make_dir(p) _mkdir(p)
#define remove_dir(p) _rmdir(p)
#else
#include <sys/stat.h>
#include <unistd.h>
#define make_dir(p) mkdir(p, 0755)
#define remove_dir(p) rmdir(p)
#endif

#include "save_file.h"
#include "save_timer.h"
#include "test.h"

#define SAV "test_saves_game.sav"

static void clean(void) {
    remove(SAV);
    remove(SAV ".bak");
    remove(SAV ".tmp");
    remove_dir(SAV ".tmp");
}

static int same(const char *path, const uint8_t *want, size_t n) {
    size_t size = 0;
    uint8_t *got = save_file_load(path, &size);
    int ok = got && size == n && memcmp(got, want, n) == 0;
    free(got);
    return ok;
}

/* ---- save_file ---- */

TEST(save_file_write_then_load_gives_the_same_bytes) {
    clean();
    uint8_t data[32768];
    for (size_t i = 0; i < sizeof data; i++) data[i] = (uint8_t)(i * 7 + 3);
    CHECK(save_file_write(SAV, data, sizeof data));
    CHECK(same(SAV, data, sizeof data));
    clean();
}

TEST(save_file_second_write_keeps_the_first_as_bak) {
    clean();
    const uint8_t first[4] = {1, 2, 3, 4}, second[4] = {9, 8, 7, 6};
    CHECK(save_file_write(SAV, first, 4));
    CHECK(save_file_write(SAV, second, 4));
    CHECK(same(SAV, second, 4));
    CHECK(same(SAV ".bak", first, 4));
    clean();
}

TEST(save_file_failed_write_leaves_the_save_as_it_was) {
    clean();
    const uint8_t old[4] = {1, 2, 3, 4}, fresh[4] = {5, 6, 7, 8};
    CHECK(save_file_write(SAV, old, 4));
    CHECK(make_dir(SAV ".tmp") == 0); /* a folder where the temporary file must go: the write fails */
    CHECK(!save_file_write(SAV, fresh, 4));
    CHECK(same(SAV, old, 4));
    clean();
}

TEST(save_file_write_into_a_missing_folder_fails) {
    const uint8_t data[4] = {1, 2, 3, 4};
    CHECK(!save_file_write("test_saves_no_such_folder/game.sav", data, 4));
}

TEST(save_file_load_of_a_missing_file_is_null) {
    clean();
    size_t size = 123;
    CHECK(save_file_load(SAV, &size) == NULL);
}

/* ---- save_timer ---- */

TEST(save_timer_writes_sixty_quiet_frames_after_a_save) {
    save_timer_t t;
    save_timer_init(&t);
    CHECK(!save_timer_frame(&t, true));
    for (int i = 1; i < SAVE_TIMER_FRAMES; i++) CHECK(!save_timer_frame(&t, false));
    CHECK(save_timer_frame(&t, false)); /* the 60th quiet frame */
    CHECK(!save_timer_frame(&t, false)); /* once */
}

TEST(save_timer_saving_again_pushes_the_write_back) {
    save_timer_t t;
    save_timer_init(&t);
    save_timer_frame(&t, true);
    for (int i = 0; i < 30; i++) CHECK(!save_timer_frame(&t, false));
    CHECK(!save_timer_frame(&t, true));
    for (int i = 1; i < SAVE_TIMER_FRAMES; i++) CHECK(!save_timer_frame(&t, false));
    CHECK(save_timer_frame(&t, false));
}

TEST(save_timer_flush_writes_now_when_something_is_pending) {
    save_timer_t t;
    save_timer_init(&t);
    save_timer_frame(&t, true);
    CHECK(save_timer_flush(&t));
    for (int i = 0; i < 2 * SAVE_TIMER_FRAMES; i++) CHECK(!save_timer_frame(&t, false)); /* nothing left */
}

TEST(save_timer_flush_with_nothing_pending_does_nothing) {
    save_timer_t t;
    save_timer_init(&t);
    CHECK(!save_timer_flush(&t));
    for (int i = 0; i < 2 * SAVE_TIMER_FRAMES; i++) save_timer_frame(&t, false);
    CHECK(!save_timer_flush(&t));
}
```

Append to `tests/unit/CMakeLists.txt`:

```cmake
target_sources(unit_tests PRIVATE ${PLATFORM_DIR}/save_file.c ${PLATFORM_DIR}/save_timer.c)
```

- [x] **Step 2: Run to see them fail**

```bash
bash tools/dev.sh configure && bash tools/dev.sh unit
```

Expected: CMake configure fails — `Cannot find source file` (`save_file.c`, `save_timer.c`).

- [x] **Step 3: Write the code**

`platform/android/save_file.h`:

```c
#ifndef GBAEMU_SAVE_FILE_H
#define GBAEMU_SAVE_FILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A game's save file (plain C: the PC tests build it too). */

/* The whole file: malloc'd bytes and *size, or NULL if it does not exist or cannot be read. */
uint8_t *save_file_load(const char *path, size_t *size);

/* Writes safely: a copy of the current file goes to <path>.bak, the new bytes go to <path>.tmp
 * (flushed to the disk), then <path>.tmp replaces <path> in one step — so <path> is always either
 * the old or the new save, never half of one. On any failure <path> is left as it was: false. */
bool save_file_write(const char *path, const uint8_t *data, size_t size);

#endif
```

`platform/android/save_file.c`:

```c
#include "save_file.h"

#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

#define PATH_LEN 1024

uint8_t *save_file_load(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    uint8_t *data = NULL;
    long n = -1;
    if (fseek(f, 0, SEEK_END) == 0) n = ftell(f);
    if (n >= 0 && fseek(f, 0, SEEK_SET) == 0 && (data = malloc(n ? (size_t)n : 1)) != NULL &&
        fread(data, 1, (size_t)n, f) != (size_t)n) {
        free(data);
        data = NULL;
    }
    fclose(f);
    if (data) *size = (size_t)n;
    return data;
}

/* The whole buffer into a new file, flushed to the disk (not just to the system's cache). */
static bool write_synced(const char *path, const uint8_t *data, size_t size) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    bool ok = fwrite(data, 1, size, f) == size && fflush(f) == 0;
#ifdef _WIN32
    ok = ok && _commit(_fileno(f)) == 0;
#else
    ok = ok && fsync(fileno(f)) == 0;
#endif
    return fclose(f) == 0 && ok;
}

/* Puts `from` in place of `to` in one step (replacing it). */
static bool replace_file(const char *from, const char *to) {
#ifdef _WIN32
    return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING) != 0;
#else
    return rename(from, to) == 0;
#endif
}

bool save_file_write(const char *path, const uint8_t *data, size_t size) {
    char tmp[PATH_LEN], bak[PATH_LEN];
    if (snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp) return false;
    if (snprintf(bak, sizeof bak, "%s.bak", path) >= (int)sizeof bak) return false;
    size_t old_size = 0;
    uint8_t *old = save_file_load(path, &old_size);
    if (old) { /* keep the previous save */
        bool kept = write_synced(tmp, old, old_size) && replace_file(tmp, bak);
        free(old);
        if (!kept) {
            remove(tmp);
            return false;
        }
    }
    if (!write_synced(tmp, data, size) || !replace_file(tmp, path)) {
        remove(tmp);
        return false;
    }
    return true;
}
```

`platform/android/save_timer.h`:

```c
#ifndef GBAEMU_SAVE_TIMER_H
#define GBAEMU_SAVE_TIMER_H

#include <stdbool.h>

#define SAVE_TIMER_FRAMES 60 /* write about 1 s after the game's last save */

/* When to write the save file: a game often writes its save over several frames, so the file is
 * written once, SAVE_TIMER_FRAMES quiet frames after the last write — or at once on a flush. */
typedef struct {
    int countdown; /* frames until the write; 0 = nothing pending */
} save_timer_t;

void save_timer_init(save_timer_t *t);
/* Once per frame; dirty = the game saved during this frame. True: write the save file now. */
bool save_timer_frame(save_timer_t *t, bool dirty);
/* Pause, stop, another game: true if a write is pending (write now; it is no longer pending). */
bool save_timer_flush(save_timer_t *t);

#endif
```

`platform/android/save_timer.c`:

```c
#include "save_timer.h"

void save_timer_init(save_timer_t *t) {
    t->countdown = 0;
}

bool save_timer_frame(save_timer_t *t, bool dirty) {
    if (dirty) {
        t->countdown = SAVE_TIMER_FRAMES;
        return false;
    }
    return t->countdown > 0 && --t->countdown == 0;
}

bool save_timer_flush(save_timer_t *t) {
    bool pending = t->countdown > 0;
    t->countdown = 0;
    return pending;
}
```

- [x] **Step 4: Run the tests**

```bash
bash tools/dev.sh configure && bash tools/dev.sh unit
bash tools/dev.sh test
```

Expected: `363 tests, 0 failed` (354 + 9: save_file 5, save_timer 4); CTest 56/56.

- [x] **Step 5: Commit**

```bash
git add tests/unit/test_saves.c tests/unit/CMakeLists.txt platform/android/save_file.h platform/android/save_file.c platform/android/save_timer.h platform/android/save_timer.c docs/PROGRESS.md docs/superpowers/plans/2026-09-26-milestone-4c-controls-saves.md
git commit -m "feat(android): save files written safely, ~1 s after the game's last save" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 2: The emulation thread owns the save file

**Files:**
- Create: `app/src/androidTest/java/com/tengen/gbaemu/SaveTest.kt`
- Modify: `gradle.properties`, `platform/android/emu_thread.h`, `platform/android/emu_thread.c`, `platform/android/jni_bridge.c`, `app/src/main/java/com/tengen/gbaemu/NativeBridge.kt`

- [x] **Step 1: Keep the app installed, then write the failing tests**

Gradle's on-device tests uninstall the app when they finish, and uninstalling deletes its saves. Append to `gradle.properties`:

```properties
# On-device tests leave the app (and its saves) installed instead of uninstalling it afterwards.
android.injected.androidTest.leaveApksInstalledAfterRun=true
```

`app/src/androidTest/java/com/tengen/gbaemu/SaveTest.kt`:

```kotlin
package com.tengen.gbaemu

import android.os.SystemClock
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import org.junit.After
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/** Save files: written by the emulation thread after the game saves and on pause, loaded again, imported. */
@RunWith(AndroidJUnit4::class)
class SaveTest {
    private val GBA_ERR_SAVE_SIZE = 4
    private lateinit var dir: File
    private lateinit var sav: File

    @Before
    fun freshFolder() {
        dir = File(InstrumentationRegistry.getInstrumentation().targetContext.filesDir, "test_saves")
        dir.deleteRecursively()
        dir.mkdirs()
        sav = File(dir, "game.sav")
    }

    @After
    fun stopSession() = NativeBridge.stop()

    private fun loadSram() = assertEquals(0, NativeBridge.loadRom(TestRoms.rom("jsmolka/sram.gba"), sav.path))

    private fun waitForFile(ms: Long) {
        val until = SystemClock.elapsedRealtime() + ms
        while (!sav.exists() && SystemClock.elapsedRealtime() < until) Thread.sleep(20)
    }

    @Test
    fun aGameThatSavesWritesItsFileSoonAfter() {
        loadSram()
        assertEquals("status", NativeBridge.SAVE_EMPTY, NativeBridge.saveStatus())
        NativeBridge.start()
        waitForFile(1500)
        assertTrue("no save file 1.5 s after start", sav.exists())
        NativeBridge.pause()
        assertArrayEquals(NativeBridge.debugSaveRam(), sav.readBytes())
    }

    @Test
    fun pauseWritesAPendingSaveBeforeItReturns() {
        loadSram()
        NativeBridge.start()
        Thread.sleep(300) // the game has saved, the 1 s wait has not run out
        assertFalse("written too early", sav.exists())
        NativeBridge.pause()
        assertTrue("no save file when pause() returned", sav.exists())
        assertArrayEquals(NativeBridge.debugSaveRam(), sav.readBytes())
    }

    @Test
    fun reopeningTheGameLoadsItsSave() {
        loadSram()
        NativeBridge.start()
        Thread.sleep(300)
        NativeBridge.pause()
        val saved = sav.readBytes()
        NativeBridge.stop()
        loadSram()
        assertEquals("status", NativeBridge.SAVE_LOADED, NativeBridge.saveStatus())
        assertArrayEquals(saved, NativeBridge.debugSaveRam())
    }

    @Test
    fun aSaveFileOfTheWrongSizeIsLeftAlone() {
        val odd = ByteArray(1000) { 7 }
        sav.writeBytes(odd)
        loadSram()
        assertEquals("status", NativeBridge.SAVE_MISFIT, NativeBridge.saveStatus())
        assertArrayEquals(odd, sav.readBytes())
    }

    @Test
    fun importAcceptsAFittingSaveAndWritesIt() {
        loadSram()
        val good = ByteArray(32768) { (it * 3).toByte() }
        assertEquals(0, NativeBridge.importSave(good))
        assertEquals("status", NativeBridge.SAVE_LOADED, NativeBridge.saveStatus())
        assertArrayEquals(good, sav.readBytes())
        assertArrayEquals(good, NativeBridge.debugSaveRam())
    }

    @Test
    fun importRejectsASaveThatDoesNotFit() {
        loadSram()
        assertEquals(GBA_ERR_SAVE_SIZE, NativeBridge.importSave(ByteArray(1000)))
        assertEquals("status", NativeBridge.SAVE_EMPTY, NativeBridge.saveStatus())
        assertFalse("a file was written", sav.exists())
    }

    @Test
    fun aGameWithoutASaveChipOffersNothingAndWritesNothing() {
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba"), sav.path))
        assertEquals("status", NativeBridge.SAVE_NONE, NativeBridge.saveStatus())
        NativeBridge.start()
        Thread.sleep(500)
        NativeBridge.pause()
        assertFalse("a file was written", sav.exists())
    }
}
```

- [x] **Step 2: Run to see them fail**

```bash
bash tools/dev.sh androidtest
```

Expected: FAIL — Kotlin compile errors in `SaveTest.kt`: `Too many arguments for 'fun loadRom(rom: ByteBuffer): Int'`, `Unresolved reference 'SAVE_EMPTY'`, `'saveStatus'`, `'importSave'`, `'debugSaveRam'`.

- [x] **Step 3: The emulation thread**

Replace `platform/android/emu_thread.h`:

```c
#ifndef GBAEMU_EMU_THREAD_H
#define GBAEMU_EMU_THREAD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gba.h"
#include "triple_buffer.h"

/* One running game: the core, its frames, its sound, and the thread that runs it — paced by the sound
 * (AAudio), or at 59.7275 frames/s by the clock when there is no sound. Once started, only the
 * emulation thread touches the core and the sound stream. */
typedef struct emu emu_t;

typedef enum {
    EMU_SAVE_NONE,   /* no save chip (or one the game has not shown yet): nothing to offer */
    EMU_SAVE_LOADED, /* the save file was loaded */
    EMU_SAVE_EMPTY,  /* a known save chip and no save file yet: offer an import */
    EMU_SAVE_MISFIT, /* a save file whose size does not fit the chip: not loaded, left untouched */
} emu_save_status_t;

/* Creates the core, loads the ROM (copied), sets the clock from the phone and loads the save file
 * at save_path if there is one (save_path NULL: no save file); NULL + *err on failure. From then on
 * the game's saves are written to save_path about 1 s after the game saves, and on pause and stop. */
emu_t *emu_create(const uint8_t *rom, size_t size, const char *save_path, gba_err_t *err);
void emu_start(emu_t *e);
void emu_pause(emu_t *e); /* returns once the thread has stopped and written the save (at most 1 s) */
void emu_resume(emu_t *e); /* timing starts afresh: no rush of catch-up frames */
void emu_set_keys(emu_t *e, uint16_t pressed); /* GBA_KEY_* mask */
triple_buffer_t *emu_frames(emu_t *e);
uint64_t emu_frame_count(const emu_t *e);
void emu_destroy(emu_t *e); /* stops the thread (if started), writes the save, frees everything */

const char *emu_rom_title(const emu_t *e);
emu_save_status_t emu_save_status(const emu_t *e);
/* Before emu_start only: puts a save file's bytes in and writes them to the save file at once.
 * GBA_ERR_SAVE_SIZE (nothing changes) if they do not fit the chip. */
gba_err_t emu_import_save(emu_t *e, const uint8_t *data, size_t size);
bool emu_save_error(emu_t *e); /* true once after a failed save-file write */

/* ---- test hooks ---- */
void emu_set_audio_enabled(emu_t *e, bool enabled); /* before emu_start: false = no stream, clock pacing */
void emu_simulate_audio_disconnect(emu_t *e);        /* as if the sound device went away */
void emu_simulate_stuck_stream(emu_t *e);            /* the stream stops taking sound, unannounced */
unsigned emu_keys_seen(emu_t *e);
/* The core's save bytes (read them only while paused or before start); returns their size. */
size_t emu_debug_saveram(emu_t *e, const uint8_t **data); /* buttons the game was given in any frame since the last call */
/* active (1/0), sample rate, frames per burst, frames played, frames silence-filled, loud frames, reopens */
void emu_audio_stats(emu_t *e, int64_t out[7]);

#endif
```

Replace `platform/android/emu_thread.c`:

```c
#include "emu_thread.h"

#include <android/log.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "audio_out.h"
#include "audio_ring.h"
#include "frame_pacer.h"
#include "key_latch.h"
#include "rate_control.h"
#include "resampler.h"
#include "save_file.h"
#include "save_timer.h"

#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "gbaemu", __VA_ARGS__)

#define AUDIO_TARGET_MS 30        /* the sound buffer is kept at about this much */
#define WATCHDOG_NS 250000000LL   /* buffer not draining this long while playing → the stream is stuck */
#define WATCHDOG_GRACE_NS 2000000000LL /* ...but not in the first 2 s after a start/resume: a waking sound
                                        * device (seen: 350 ms on the phone emulator) takes a while */
#define CONVERTED_FRAMES 8192     /* room for 1,024 GBA frames converted at up to ~8x */

struct emu {
    gba_t *g;
    triple_buffer_t tb;
    pthread_t thread;
    bool started;
    pthread_mutex_t lock; /* guards paused/quit */
    pthread_cond_t wake;
    bool paused, quit;
    key_latch_t keys;
    atomic_ullong frames;
    atomic_uint keys_seen; /* test hook */
    bool parked;           /* the thread is stopped in a pause with the save written (guarded by lock) */

    /* Save file. Before start only the caller touches these; after start, only the emulation thread
     * (save_failed is shared). */
    char *save_path;                /* NULL: no save file */
    save_timer_t save_timer;
    emu_save_status_t save_status;
    atomic_bool save_failed;        /* a write failed since the last emu_save_error */

    /* Sound. The emulation thread alone touches out/rs/converted; the rest is shared as marked. */
    bool audio_wanted;                 /* set before start (test hook) */
    audio_out_t *out;                  /* NULL: no sound, the clock paces the game */
    audio_ring_t ring;                 /* emulation thread → AAudio callback */
    resampler_t rs;
    int16_t converted[CONVERTED_FRAMES * 2];
    audio_stats_t stats;               /* kept by the callback */
    atomic_bool sound_on;
    atomic_int rate, burst, reopens;
    atomic_bool fake_disconnect;       /* test hook */
    atomic_bool fake_stuck;            /* test hook */
    bool watchdog_used;
    int64_t sound_started_ns;          /* last start/resume of the stream */
};

static int64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

static bool paused_or_quitting(emu_t *e) {
    pthread_mutex_lock(&e->lock);
    bool stop = e->paused || e->quit;
    pthread_mutex_unlock(&e->lock);
    return stop;
}

static void sound_open(emu_t *e) {
    e->out = audio_out_open(&e->ring, &e->stats);
    if (e->out && !audio_out_start(e->out)) {
        audio_out_close(e->out);
        e->out = NULL;
    }
    e->sound_started_ns = now_ns();
    atomic_store(&e->rate, e->out ? audio_out_rate(e->out) : 0);
    atomic_store(&e->burst, e->out ? audio_out_burst(e->out) : 0);
    atomic_store(&e->sound_on, e->out != NULL);
    if (!e->out) LOGW("no sound: the clock paces the game");
}

static void sound_close(emu_t *e) {
    audio_out_close(e->out);
    e->out = NULL;
    atomic_store(&e->sound_on, false);
}

/* Close and open again (device change, stuck stream). The ring is emptied while nothing reads it. */
static void sound_reopen(emu_t *e) {
    sound_close(e);
    audio_ring_clear(&e->ring);
    resampler_init(&e->rs);
    atomic_fetch_add(&e->reopens, 1);
    sound_open(e);
}

static size_t target_frames(emu_t *e) {
    return (size_t)audio_out_rate(e->out) * AUDIO_TARGET_MS / 1000;
}

/* Every sample of the frame: into the resampler, then into the ring (or dropped without sound). */
static void feed_sound(emu_t *e) {
    int16_t in[1024 * 2];
    size_t n;
    while ((n = gba_audio_read(e->g, in, 1024)) > 0) {
        if (!e->out) continue;
        double fill = (double)audio_ring_queued(&e->ring) / (2.0 * (double)target_frames(e));
        double ratio = rate_control_ratio(audio_out_rate(e->out), fill);
        size_t m = resampler_run(&e->rs, in, n, ratio, e->converted, CONVERTED_FRAMES);
        audio_ring_push(&e->ring, e->converted, m);
    }
}

/* Sound pacing: wait (1 ms at a time) while the ring holds more than the target. A ring that stops
 * draining for 250 ms means a stuck stream: reopen once; the second time, give up on sound. */
static void sound_pace(emu_t *e) {
    size_t target = target_frames(e);
    size_t last = audio_ring_queued(&e->ring);
    int64_t since = now_ns();
    for (;;) {
        size_t q = audio_ring_queued(&e->ring);
        if (q <= target || paused_or_quitting(e)) return;
        int64_t now = now_ns();
        if (q < last || now - e->sound_started_ns < WATCHDOG_GRACE_NS) {
            last = q;
            since = now;
        } else if (now - since > WATCHDOG_NS) {
            LOGW("sound stream stuck: %s", e->watchdog_used ? "giving up on sound" : "reopening it");
            if (e->watchdog_used) {
                sound_close(e);
            } else {
                e->watchdog_used = true;
                sound_reopen(e);
            }
            return;
        }
        struct timespec ms = {0, 1000000};
        nanosleep(&ms, NULL);
    }
}

/* The core's save bytes into the save file (nothing without a path or a chip of known size). */
static void write_save(emu_t *e) {
    size_t size = gba_saveram_size(e->g);
    if (!e->save_path || !size) return;
    if (!save_file_write(e->save_path, gba_saveram(e->g), size)) {
        LOGW("cannot write the save file %s", e->save_path);
        atomic_store(&e->save_failed, true);
    }
}

/* Pause, stop: write now if the game saved since the last write. */
static void flush_save(emu_t *e) {
    if (gba_saveram_dirty(e->g)) save_timer_frame(&e->save_timer, true);
    if (save_timer_flush(&e->save_timer)) write_save(e);
}

static void *emu_main(void *arg) {
    emu_t *e = arg;
    frame_pacer_t pacer;
    pacer_reset(&pacer);
    if (e->audio_wanted) sound_open(e);
    for (;;) {
        pthread_mutex_lock(&e->lock);
        bool pausing = e->paused && !e->quit;
        pthread_mutex_unlock(&e->lock);
        if (pausing) {
            if (e->out) { /* stop the callback, then drop the stale sound */
                audio_out_pause(e->out);
                audio_ring_clear(&e->ring);
                resampler_init(&e->rs);
            }
            flush_save(e); /* outside the lock, so emu_pause's 1 s limit keeps working */
            pthread_mutex_lock(&e->lock);
            e->parked = true;
            pthread_cond_broadcast(&e->wake);
            while (e->paused && !e->quit) pthread_cond_wait(&e->wake, &e->lock);
            e->parked = false;
            bool resuming = !e->quit;
            pthread_mutex_unlock(&e->lock);
            if (e->out && resuming) {
                audio_out_start(e->out);
                e->sound_started_ns = now_ns();
            }
            pacer_reset(&pacer);
        }
        pthread_mutex_lock(&e->lock);
        bool quit = e->quit;
        pthread_mutex_unlock(&e->lock);
        if (quit) break;

        unsigned keys = key_latch_take(&e->keys);
        atomic_fetch_or(&e->keys_seen, keys);
        gba_set_keys(e->g, (uint16_t)keys);
        gba_run_frame(e->g);
        memcpy(tb_back(&e->tb), gba_framebuffer(e->g), sizeof(uint16_t) * TB_PIXELS);
        tb_publish(&e->tb);
        feed_sound(e);
        if (save_timer_frame(&e->save_timer, gba_saveram_dirty(e->g))) write_save(e);
        atomic_fetch_add(&e->frames, 1);

        if (e->out && atomic_exchange(&e->fake_stuck, false)) audio_out_pause(e->out); /* stops draining, unannounced */
        if (e->out && (audio_out_disconnected(e->out) || atomic_exchange(&e->fake_disconnect, false))) {
            LOGW("sound device changed: reopening the stream");
            sound_reopen(e);
        }
        if (e->out) {
            sound_pace(e);
            pacer_reset(&pacer); /* if sound stops, the clock takes over from now */
        } else {
            int64_t due = pacer_next(&pacer, now_ns());
            struct timespec ts = {(time_t)(due / 1000000000), (long)(due % 1000000000)};
            while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) == EINTR) {
            }
        }
    }
    flush_save(e);
    sound_close(e);
    return NULL;
}

static bool known_save_chip(gba_save_type_t t) {
    return t != GBA_SAVE_NONE && t != GBA_SAVE_UNKNOWN;
}

/* The save file, if there is one, into the core. */
static void load_save(emu_t *e) {
    size_t size = 0;
    uint8_t *data = e->save_path ? save_file_load(e->save_path, &size) : NULL;
    if (data) {
        e->save_status = gba_load_saveram(e->g, data, size) == GBA_OK ? EMU_SAVE_LOADED : EMU_SAVE_MISFIT;
        free(data);
    } else {
        e->save_status = known_save_chip(gba_save_type(e->g)) ? EMU_SAVE_EMPTY : EMU_SAVE_NONE;
    }
}

emu_t *emu_create(const uint8_t *rom, size_t size, const char *save_path, gba_err_t *err) {
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
    pthread_condattr_t attr; /* emu_pause's time limit counts on the monotonic clock */
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    pthread_cond_init(&e->wake, &attr);
    pthread_condattr_destroy(&attr);
    key_latch_init(&e->keys);
    atomic_init(&e->frames, 0ull);
    atomic_init(&e->keys_seen, 0u);
    e->audio_wanted = true;
    audio_ring_init(&e->ring);
    resampler_init(&e->rs);
    atomic_init(&e->stats.played, 0);
    atomic_init(&e->stats.silent, 0);
    atomic_init(&e->stats.loud, 0);
    atomic_init(&e->sound_on, false);
    atomic_init(&e->rate, 0);
    atomic_init(&e->burst, 0);
    atomic_init(&e->reopens, 0);
    atomic_init(&e->fake_disconnect, false);
    atomic_init(&e->fake_stuck, false);
    atomic_init(&e->save_failed, false);
    save_timer_init(&e->save_timer);
    e->save_path = save_path ? strdup(save_path) : NULL;
    load_save(e);
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
    pthread_mutex_lock(&e->lock);
    e->paused = true;
    pthread_cond_broadcast(&e->wake);
    if (e->started) { /* wait until the thread has stopped and written the save: at most 1 s */
        struct timespec until;
        clock_gettime(CLOCK_MONOTONIC, &until);
        until.tv_sec += 1;
        while (!e->parked && !e->quit) {
            if (pthread_cond_timedwait(&e->wake, &e->lock, &until) == ETIMEDOUT) break;
        }
    }
    pthread_mutex_unlock(&e->lock);
}

void emu_resume(emu_t *e) {
    set_paused(e, false);
}

void emu_set_keys(emu_t *e, uint16_t pressed) {
    key_latch_set(&e->keys, pressed);
}

triple_buffer_t *emu_frames(emu_t *e) {
    return &e->tb;
}

uint64_t emu_frame_count(const emu_t *e) {
    return atomic_load(&((emu_t *)e)->frames);
}

void emu_set_audio_enabled(emu_t *e, bool enabled) {
    if (!e->started) e->audio_wanted = enabled;
}

const char *emu_rom_title(const emu_t *e) {
    return gba_rom_title(e->g);
}

emu_save_status_t emu_save_status(const emu_t *e) {
    return e->save_status;
}

gba_err_t emu_import_save(emu_t *e, const uint8_t *data, size_t size) {
    if (e->started) return GBA_ERR_SAVE_SIZE; /* only before start: the thread owns the core then */
    gba_err_t err = gba_load_saveram(e->g, data, size);
    if (err != GBA_OK) return err;
    e->save_status = EMU_SAVE_LOADED;
    write_save(e);
    return GBA_OK;
}

bool emu_save_error(emu_t *e) {
    return atomic_exchange(&e->save_failed, false);
}

size_t emu_debug_saveram(emu_t *e, const uint8_t **data) {
    *data = gba_saveram(e->g);
    return gba_saveram_size(e->g);
}

unsigned emu_keys_seen(emu_t *e) {
    return atomic_exchange(&e->keys_seen, 0u);
}

void emu_simulate_audio_disconnect(emu_t *e) {
    atomic_store(&e->fake_disconnect, true);
}

void emu_simulate_stuck_stream(emu_t *e) {
    atomic_store(&e->fake_stuck, true);
}

void emu_audio_stats(emu_t *e, int64_t out[7]) {
    out[0] = atomic_load(&e->sound_on) ? 1 : 0;
    out[1] = atomic_load(&e->rate);
    out[2] = atomic_load(&e->burst);
    out[3] = atomic_load(&e->stats.played);
    out[4] = atomic_load(&e->stats.silent);
    out[5] = atomic_load(&e->stats.loud);
    out[6] = atomic_load(&e->reopens);
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
    free(e->save_path);
    free(e);
}
```

(The flush on pause runs **outside** the lock: `emu_pause` waits on the same mutex with a 1 s limit, which could not expire while the thread held the lock during a slow write. The condition variable uses `CLOCK_MONOTONIC`, so a clock change cannot stretch the wait.)

- [x] **Step 4: JNI and Kotlin**

Replace `platform/android/jni_bridge.c`:

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

JNIEXPORT jint JNICALL JNI_FN(loadRom)(JNIEnv *env, jobject self, jobject rom, jstring save_path) {
    (void)self;
    stop_session(); /* writes the previous game's save */
    const uint8_t *data = (*env)->GetDirectBufferAddress(env, rom);
    jlong size = (*env)->GetDirectBufferCapacity(env, rom);
    if (!data || size <= 0) return GBA_ERR_ROM_SIZE;
    const char *path = save_path ? (*env)->GetStringUTFChars(env, save_path, NULL) : NULL;
    gba_err_t err;
    s_emu = emu_create(data, (size_t)size, path, &err);
    if (path) (*env)->ReleaseStringUTFChars(env, save_path, path);
    return err;
}

JNIEXPORT jstring JNICALL JNI_FN(romTitle)(JNIEnv *env, jobject self) {
    (void)self;
    return (*env)->NewStringUTF(env, s_emu ? emu_rom_title(s_emu) : "");
}

JNIEXPORT jint JNICALL JNI_FN(saveStatus)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    return s_emu ? (jint)emu_save_status(s_emu) : EMU_SAVE_NONE;
}

JNIEXPORT jint JNICALL JNI_FN(importSave)(JNIEnv *env, jobject self, jbyteArray bytes) {
    (void)self;
    if (!s_emu) return GBA_ERR_SAVE_SIZE;
    jsize n = (*env)->GetArrayLength(env, bytes);
    jbyte *b = (*env)->GetByteArrayElements(env, bytes, NULL);
    gba_err_t err = emu_import_save(s_emu, (const uint8_t *)b, (size_t)n);
    (*env)->ReleaseByteArrayElements(env, bytes, b, JNI_ABORT);
    return err;
}

JNIEXPORT jboolean JNICALL JNI_FN(saveError)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    return s_emu && emu_save_error(s_emu) ? JNI_TRUE : JNI_FALSE;
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

JNIEXPORT jlongArray JNICALL JNI_FN(debugAudioStats)(JNIEnv *env, jobject self) {
    (void)self;
    int64_t v[7] = {0, 0, 0, 0, 0, 0, 0};
    if (s_emu) emu_audio_stats(s_emu, v);
    jlongArray out = (*env)->NewLongArray(env, 7);
    (*env)->SetLongArrayRegion(env, out, 0, 7, (const jlong *)v);
    return out;
}

JNIEXPORT void JNICALL JNI_FN(debugSetAudioEnabled)(JNIEnv *env, jobject self, jboolean enabled) {
    (void)env;
    (void)self;
    if (s_emu) emu_set_audio_enabled(s_emu, enabled == JNI_TRUE);
}

JNIEXPORT void JNICALL JNI_FN(debugSimulateAudioDisconnect)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    if (s_emu) emu_simulate_audio_disconnect(s_emu);
}

JNIEXPORT void JNICALL JNI_FN(debugSimulateStuckStream)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    if (s_emu) emu_simulate_stuck_stream(s_emu);
}

JNIEXPORT jbyteArray JNICALL JNI_FN(debugSaveRam)(JNIEnv *env, jobject self) {
    (void)self;
    if (!s_emu) return NULL;
    const uint8_t *data;
    size_t n = emu_debug_saveram(s_emu, &data);
    jbyteArray out = (*env)->NewByteArray(env, (jsize)n);
    (*env)->SetByteArrayRegion(env, out, 0, (jsize)n, (const jbyte *)data);
    return out;
}

JNIEXPORT jint JNICALL JNI_FN(debugKeysSeen)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    return s_emu ? (jint)emu_keys_seen(s_emu) : 0;
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

Replace `app/src/main/java/com/tengen/gbaemu/NativeBridge.kt`:

```kotlin
package com.tengen.gbaemu

import android.view.Surface
import java.nio.ByteBuffer

/** The C side (platform/android/jni_bridge.c). One game session at a time; call from one thread. */
object NativeBridge {
    init {
        System.loadLibrary("gbaemu")
    }

    /**
     * Creates the emulator with this ROM (a direct buffer; the core copies it) and loads its save file
     * from savePath if there is one (null: no save file). The game's saves go to that file ~1 s after
     * the game saves, and on pause/stop. 0 = OK, else a gba_err_t. Stops (and saves) a previous game.
     */
    external fun loadRom(rom: ByteBuffer, savePath: String?): Int

    fun loadRom(rom: ByteBuffer): Int = loadRom(rom, null)

    /** The loaded ROM's header title (may be empty). */
    external fun romTitle(): String

    /** After loadRom: one of the SAVE_* values below. */
    external fun saveStatus(): Int

    /** Before start(): puts a .sav file's bytes in and writes the save file. 0 = OK, else a gba_err_t (nothing changed). */
    external fun importSave(bytes: ByteArray): Int

    /** True once after a save-file write failed. */
    external fun saveError(): Boolean

    const val SAVE_NONE = 0 // no save chip: nothing to offer
    const val SAVE_LOADED = 1 // the save file was loaded
    const val SAVE_EMPTY = 2 // a save chip and no save file yet: offer an import
    const val SAVE_MISFIT = 3 // the save file does not fit the chip: not loaded, left untouched

    /** Starts the emulation and render threads for the loaded ROM. */
    external fun start()

    /** Stops both threads and frees the emulator. */
    external fun stop()

    /** Returns once the game has stopped and its save is written (at most 1 s). */
    external fun pause()

    external fun resume()

    /** The game's window appeared/changed (or went away: null). False if OpenGL ES 3 cannot draw into it. */
    external fun setSurface(surface: Surface?): Boolean

    /** Held GBA buttons (KeyMap bits). */
    external fun setKeys(pressed: Int)

    // ---- test-only ----

    /** Frames the emulation thread has run since loadRom. */
    external fun debugFrameCount(): Long

    /** Sound since loadRom: [active 1/0, sample rate, frames per burst, played, silence-filled, loud, reopens]. */
    external fun debugAudioStats(): LongArray

    /** Before start(): false = no sound stream, the clock paces the game. */
    external fun debugSetAudioEnabled(enabled: Boolean)

    /** As if the sound device went away (headphones unplugged): the stream must reopen. */
    external fun debugSimulateAudioDisconnect()

    /** The stream silently stops taking sound: the watchdog must reopen it (after its 2 s start-up grace). */
    external fun debugSimulateStuckStream()

    /** The core's save bytes (call while paused), or null without a game. */
    external fun debugSaveRam(): ByteArray?

    /** Buttons the game was given in any frame since the last call (KeyMap bits). */
    external fun debugKeysSeen(): Int

    /** Runs a ROM headless for n frames on this thread: "<frame hash> <audio hash>" like the PC test runner. */
    external fun debugRunFrames(rom: ByteBuffer, frames: Int): String?

    /** Draws one 240x160 BGR555 frame offscreen; RGBA pixels (r | g<<8 | b<<16 | a<<24), top row first. */
    external fun debugDrawFrame(pixels: ShortArray, width: Int, height: Int): IntArray?
}
```

(`loadRom(rom)` stays as a Kotlin overload, so the 4a/4b tests are unchanged. Only one `loadRom` is `external`, so JNI keeps the short symbol name.)

- [x] **Step 5: Run the tests (three times)**

```bash
for i in 1 2 3; do rm -rf app/build/outputs/androidTest-results; bash tools/dev.sh androidtest 2>&1 | grep -E "^e: |Starting|BUILD FAILED"; grep -h -o 'tests="[0-9]*" failures="[0-9]*"' app/build/outputs/androidTest-results/connected/debug/*.xml; done
echo "C warnings: $(bash tools/dev.sh apk --rerun-tasks 2>&1 | grep -c 'warning:.*\.[ch]:')"
```

Expected: `tests="24" failures="0"` three times (17 + 7); `0` C warnings.

- [x] **Step 6: Commit**

```bash
git add gradle.properties app/src/androidTest/java/com/tengen/gbaemu/SaveTest.kt platform/android/emu_thread.h platform/android/emu_thread.c platform/android/jni_bridge.c app/src/main/java/com/tengen/gbaemu/NativeBridge.kt docs/PROGRESS.md docs/superpowers/plans/2026-09-26-milestone-4c-controls-saves.md
git commit -m "feat(android): the emulation thread loads, writes and flushes the save file; pause waits for it" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

**Batch 1 (Tasks 1-2) done — checkpoint with the user.**

---
### Task 3: Button layout, key table, game id

**Files:**
- Create: `app/src/main/java/com/tengen/gbaemu/TouchLayout.kt`, `app/src/main/java/com/tengen/gbaemu/GameId.kt`, `app/src/test/java/com/tengen/gbaemu/TouchLayoutTest.kt`, `app/src/test/java/com/tengen/gbaemu/GameIdTest.kt`
- Modify: `app/src/main/java/com/tengen/gbaemu/KeyMap.kt`, `app/src/test/java/com/tengen/gbaemu/KeyMapTest.kt`

- [x] **Step 1: Write the failing tests**

`app/src/test/java/com/tengen/gbaemu/TouchLayoutTest.kt`:

```kotlin
package com.tengen.gbaemu

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class TouchLayoutTest {
    private val density = 2.625f // Pixel 9
    private val upright = TouchLayout(1080f, 1700f, density, sideways = false)
    private val sideways = TouchLayout(2400f, 1080f, density, sideways = true)

    @Test
    fun dpadGivesEightDirections() {
        val d = TouchLayout.Companion
        assertEquals(KeyMap.RIGHT, d.dpadDirection(10f, 0f))
        assertEquals(KeyMap.RIGHT or KeyMap.UP, d.dpadDirection(10f, -10f))
        assertEquals(KeyMap.UP, d.dpadDirection(0f, -10f))
        assertEquals(KeyMap.UP or KeyMap.LEFT, d.dpadDirection(-10f, -10f))
        assertEquals(KeyMap.LEFT, d.dpadDirection(-10f, 0f))
        assertEquals(KeyMap.LEFT or KeyMap.DOWN, d.dpadDirection(-10f, 10f))
        assertEquals(KeyMap.DOWN, d.dpadDirection(0f, 10f))
        assertEquals(KeyMap.DOWN or KeyMap.RIGHT, d.dpadDirection(10f, 10f))
        assertEquals(KeyMap.UP, d.dpadDirection(3f, -10f)) // 17° off up: still up
    }

    @Test
    fun dpadMiddleIsDeadAndItsEdgeWorks() {
        for (l in listOf(upright, sideways)) {
            assertEquals(0, l.buttonsAt(l.dpadX, l.dpadY))
            assertEquals(0, l.buttonsAt(l.dpadX + l.dpadRadius * 0.2f, l.dpadY))
            assertEquals(KeyMap.RIGHT, l.buttonsAt(l.dpadX + l.dpadRadius * 0.9f, l.dpadY))
            assertEquals(KeyMap.UP, l.buttonsAt(l.dpadX, l.dpadY - l.dpadRadius))
        }
    }

    @Test
    fun theMiddleOfEveryButtonPressesIt() {
        for (l in listOf(upright, sideways)) {
            assertEquals(6, l.buttons.size)
            for (b in l.buttons) assertEquals("${b.label} sideways=${l.sideways}", b.bit, l.buttonsAt(b.cx, b.cy))
        }
    }

    @Test
    fun theGapBetweenAAndBPressesNothing() {
        for (l in listOf(upright, sideways)) {
            val a = l.buttons.first { it.bit == KeyMap.A }
            val b = l.buttons.first { it.bit == KeyMap.B }
            assertEquals("sideways=${l.sideways}", 0, l.buttonsAt((a.cx + b.cx) / 2, (a.cy + b.cy) / 2))
        }
    }

    @Test
    fun buttonsStayInsideTheArea() {
        for (l in listOf(upright, sideways)) {
            for (b in l.buttons) {
                assertTrue("${b.label}", b.left >= 0f && b.top >= 0f && b.right <= l.width && b.bottom <= l.height)
            }
            assertTrue(l.dpadX - l.dpadRadius >= 0f && l.dpadY + l.dpadRadius <= l.height)
        }
    }

    @Test
    fun farFromEverythingPressesNothing() {
        assertEquals(0, upright.buttonsAt(upright.width / 2, upright.height / 2))
        assertEquals(0, sideways.buttonsAt(sideways.width / 2, sideways.height / 2)) // the middle of the game
    }
}
```

`app/src/test/java/com/tengen/gbaemu/GameIdTest.kt`:

```kotlin
package com.tengen.gbaemu

import org.junit.Assert.assertEquals
import org.junit.Test

class GameIdTest {
    @Test
    fun isTheStandardCrc32InLowercaseHex() {
        assertEquals("cbf43926", GameId.of("123456789".toByteArray())) // the CRC-32 check value
        assertEquals("00000000", GameId.of(ByteArray(0)))
    }

    @Test
    fun differentRomsGetDifferentIds() {
        val rom = ByteArray(1024) { it.toByte() }
        val patched = rom.copyOf().also { it[500] = 99 }
        assertEquals(8, GameId.of(rom).length)
        assert(GameId.of(rom) != GameId.of(patched))
    }
}
```

Replace `app/src/test/java/com/tengen/gbaemu/KeyMapTest.kt`:

```kotlin
package com.tengen.gbaemu

import android.view.KeyEvent
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
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

    @Test
    fun theControlsTableCoversEveryButtonOnceWithNames() {
        assertEquals(1023, KeyMap.bindings.sumOf { it.bit }) // every bit exactly once
        assertEquals(10, KeyMap.bindings.size)
        for (b in KeyMap.bindings) {
            assertTrue(b.button, b.keyboard.isNotEmpty() && b.gamepad.isNotEmpty())
            for (code in b.keyboard.keys + b.gamepad.keys) assertTrue(b.button, KeyMap.bitFor(code) == b.bit)
        }
    }

    @Test
    fun keyboardAndTouchButtonsAddUp() {
        assertEquals(KeyMap.A or KeyMap.UP or KeyMap.RIGHT, KeyMap.merge(KeyMap.A, KeyMap.UP or KeyMap.RIGHT))
        assertEquals(KeyMap.B, KeyMap.merge(KeyMap.B, KeyMap.B))
        assertEquals(0, KeyMap.merge(0, 0))
    }
}
```

(In `KeyMapTest`, `assertEquals(message, Int, Int)` picks JUnit's `(String, long, long)` overload and fails to compile — hence `assertTrue(... == ...)` and `sumOf`.)

- [x] **Step 2: Run to see them fail**

```bash
bash tools/dev.sh jvmtest
```

Expected: FAIL — `Unresolved reference 'TouchLayout'`, `'GameId'`, `'bindings'`, `'merge'`.

- [x] **Step 3: Write the code**

`app/src/main/java/com/tengen/gbaemu/TouchLayout.kt`:

```kotlin
package com.tengen.gbaemu

import kotlin.math.atan2
import kotlin.math.hypot
import kotlin.math.roundToInt

/**
 * Where the touch buttons are, in pixels, for a controls area of [width] × [height] pixels.
 * Upright (layout A): the area is the space below the game. Sideways: the whole screen, buttons over the side bars.
 * Pure maths (no Android classes), so it is tested on the PC.
 */
class TouchLayout(val width: Float, val height: Float, density: Float, val sideways: Boolean) {
    /** One button: [round] ones are the circle inside the box, the others a rounded box. */
    data class Button(val bit: Int, val label: String, val round: Boolean,
                      val left: Float, val top: Float, val right: Float, val bottom: Float) {
        val cx get() = (left + right) / 2
        val cy get() = (top + bottom) / 2
    }

    private val dp = density
    private val slop = 8 * dp // fingers are imprecise: a button reacts a little outside its drawing

    val dpadX: Float
    val dpadY: Float
    val dpadRadius: Float
    val buttons: List<Button>

    init {
        val m = 16 * dp
        if (!sideways) {
            val shoulderH = 40 * dp
            val smallW = 80 * dp
            val smallH = 32 * dp
            val smallTop = height - m - smallH
            dpadRadius = 70 * dp
            dpadX = m + dpadRadius
            dpadY = (m + shoulderH + smallTop) / 2
            val ar = 36 * dp
            val ax = width - m - ar
            val ay = dpadY - 28 * dp
            val bx = ax - 84 * dp
            val by = ay + 56 * dp
            buttons = listOf(
                Button(KeyMap.L, "L", false, 0f, m, 96 * dp, m + shoulderH),
                Button(KeyMap.R, "R", false, width - 96 * dp, m, width, m + shoulderH),
                Button(KeyMap.A, "A", true, ax - ar, ay - ar, ax + ar, ay + ar),
                Button(KeyMap.B, "B", true, bx - ar, by - ar, bx + ar, by + ar),
                Button(KeyMap.SELECT, "SELECT", false, width / 2 - 8 * dp - smallW, smallTop, width / 2 - 8 * dp, smallTop + smallH),
                Button(KeyMap.START, "START", false, width / 2 + 8 * dp, smallTop, width / 2 + 8 * dp + smallW, smallTop + smallH),
            )
        } else {
            val shoulderH = 40 * dp
            val smallW = 72 * dp
            val smallH = 28 * dp
            val smallTop = height - m - smallH
            dpadRadius = 60 * dp
            dpadX = m + dpadRadius
            dpadY = height * 0.55f
            val ar = 30 * dp
            val ax = width - m - ar
            val ay = height * 0.45f
            val bx = ax - 66 * dp
            val by = ay + 52 * dp
            buttons = listOf(
                Button(KeyMap.L, "L", false, 0f, m, 96 * dp, m + shoulderH),
                Button(KeyMap.R, "R", false, width - 96 * dp, m, width, m + shoulderH),
                Button(KeyMap.A, "A", true, ax - ar, ay - ar, ax + ar, ay + ar),
                Button(KeyMap.B, "B", true, bx - ar, by - ar, bx + ar, by + ar),
                Button(KeyMap.SELECT, "SELECT", false, width / 2 - 8 * dp - smallW, smallTop, width / 2 - 8 * dp, smallTop + smallH),
                Button(KeyMap.START, "START", false, width / 2 + 8 * dp, smallTop, width / 2 + 8 * dp + smallW, smallTop + smallH),
            )
        }
    }

    /** The GBA buttons a finger at (x, y) presses: D-pad directions, or the nearest button it touches, or 0. */
    fun buttonsAt(x: Float, y: Float): Int {
        val dx = x - dpadX
        val dy = y - dpadY
        val dist = hypot(dx, dy)
        if (dist <= dpadRadius + slop) return if (dist < dpadRadius * DEAD_ZONE) 0 else dpadDirection(dx, dy)
        var best = 0
        var bestDist = Float.MAX_VALUE
        for (b in buttons) {
            if (!touches(b, x, y)) continue
            val d = hypot(x - b.cx, y - b.cy)
            if (d < bestDist) {
                best = b.bit
                bestDist = d
            }
        }
        return best
    }

    private fun touches(b: Button, x: Float, y: Float): Boolean =
        if (b.round) hypot(x - b.cx, y - b.cy) <= (b.right - b.left) / 2 + slop
        else x >= b.left - slop && x <= b.right + slop && y >= b.top - slop && y <= b.bottom + slop

    companion object {
        const val DEAD_ZONE = 0.25f // no direction in the middle quarter of the D-pad's radius

        /** 8 sectors of 45°, from the D-pad's centre (screen y grows downwards): diagonals press two directions. */
        fun dpadDirection(dx: Float, dy: Float): Int {
            val angle = Math.toDegrees(atan2(-dy, dx).toDouble()) // 0 = right, 90 = up
            return when (((angle / 45.0).roundToInt() + 8) % 8) {
                0 -> KeyMap.RIGHT
                1 -> KeyMap.RIGHT or KeyMap.UP
                2 -> KeyMap.UP
                3 -> KeyMap.UP or KeyMap.LEFT
                4 -> KeyMap.LEFT
                5 -> KeyMap.LEFT or KeyMap.DOWN
                6 -> KeyMap.DOWN
                else -> KeyMap.DOWN or KeyMap.RIGHT
            }
        }
    }
}
```

`app/src/main/java/com/tengen/gbaemu/GameId.kt`:

```kotlin
package com.tengen.gbaemu

import java.util.zip.CRC32

/** A ROM's fingerprint, the name of its save file: the CRC32 of its bytes as 8 lowercase hex digits. */
object GameId {
    fun of(rom: ByteArray): String = "%08x".format(CRC32().apply { update(rom) }.value)
}
```

Replace `app/src/main/java/com/tengen/gbaemu/KeyMap.kt`:

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

    /** One GBA button and what presses it: keyboard keys and gamepad buttons (key code → name to show). */
    class Binding(val bit: Int, val button: String, val keyboard: Map<Int, String>, val gamepad: Map<Int, String>)

    /** The whole map; bitFor and the Controls table both come from it. */
    val bindings = listOf(
        Binding(A, "A", mapOf(KeyEvent.KEYCODE_X to "X"), mapOf(KeyEvent.KEYCODE_BUTTON_A to "A")),
        Binding(B, "B", mapOf(KeyEvent.KEYCODE_Z to "Z"), mapOf(KeyEvent.KEYCODE_BUTTON_B to "B")),
        Binding(L, "L", mapOf(KeyEvent.KEYCODE_A to "A"), mapOf(KeyEvent.KEYCODE_BUTTON_L1 to "L1")),
        Binding(R, "R", mapOf(KeyEvent.KEYCODE_S to "S"), mapOf(KeyEvent.KEYCODE_BUTTON_R1 to "R1")),
        Binding(START, "Start", mapOf(KeyEvent.KEYCODE_ENTER to "Enter"), mapOf(KeyEvent.KEYCODE_BUTTON_START to "Start")),
        Binding(SELECT, "Select", mapOf(KeyEvent.KEYCODE_DEL to "Backspace"), mapOf(KeyEvent.KEYCODE_BUTTON_SELECT to "Select")),
        Binding(UP, "Up", mapOf(KeyEvent.KEYCODE_DPAD_UP to "↑"), mapOf(KeyEvent.KEYCODE_DPAD_UP to "D-pad ↑")),
        Binding(DOWN, "Down", mapOf(KeyEvent.KEYCODE_DPAD_DOWN to "↓"), mapOf(KeyEvent.KEYCODE_DPAD_DOWN to "D-pad ↓")),
        Binding(LEFT, "Left", mapOf(KeyEvent.KEYCODE_DPAD_LEFT to "←"), mapOf(KeyEvent.KEYCODE_DPAD_LEFT to "D-pad ←")),
        Binding(RIGHT, "Right", mapOf(KeyEvent.KEYCODE_DPAD_RIGHT to "→"), mapOf(KeyEvent.KEYCODE_DPAD_RIGHT to "D-pad →")),
    )

    /** The GBA button for an Android key code, or 0 if the key is not mapped. */
    fun bitFor(keyCode: Int): Int =
        bindings.firstOrNull { keyCode in it.keyboard || keyCode in it.gamepad }?.bit ?: 0

    /** The held-buttons mask after a key goes down or up. */
    fun update(mask: Int, keyCode: Int, down: Boolean): Int {
        val bit = bitFor(keyCode)
        return if (down) mask or bit else mask and bit.inv()
    }

    /** What the game gets: keyboard/gamepad buttons and touch buttons together. */
    fun merge(keyboardMask: Int, touchMask: Int): Int = keyboardMask or touchMask
}
```

- [x] **Step 4: Run the tests**

```bash
bash tools/dev.sh jvmtest
grep -h -o 'testsuite name="[^"]*" tests="[0-9]*" skipped="[0-9]*" failures="[0-9]*"' app/build/test-results/testDebugUnitTest/*.xml
```

Expected: `GameIdTest` 2, `KeyMapTest` 6, `TouchLayoutTest` 6 — 14 tests, 0 failures.

- [x] **Step 5: Commit**

```bash
git add app/src/main/java/com/tengen/gbaemu/TouchLayout.kt app/src/main/java/com/tengen/gbaemu/GameId.kt app/src/main/java/com/tengen/gbaemu/KeyMap.kt app/src/test/java/com/tengen/gbaemu/TouchLayoutTest.kt app/src/test/java/com/tengen/gbaemu/GameIdTest.kt app/src/test/java/com/tengen/gbaemu/KeyMapTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-09-26-milestone-4c-controls-saves.md
git commit -m "feat(android): touch button layout, one key table for keys and the Controls panel, game id" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 4: The touch buttons view

**Files:**
- Create: `app/src/main/java/com/tengen/gbaemu/TouchControlsView.kt`, `app/src/androidTest/java/com/tengen/gbaemu/TouchTest.kt`

- [x] **Step 1: Write the failing tests**

`app/src/androidTest/java/com/tengen/gbaemu/TouchTest.kt`:

```kotlin
package com.tengen.gbaemu

import android.os.SystemClock
import android.view.InputDevice
import android.view.MotionEvent
import android.view.View
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Test
import org.junit.runner.RunWith

/** The touch buttons, fed with made-up finger events: several fingers, sliding, diagonals, hiding. */
@RunWith(AndroidJUnit4::class)
class TouchTest {
    private val inst = InstrumentationRegistry.getInstrumentation()
    private lateinit var view: TouchControlsView
    private var mask = -1

    private fun makeView(width: Int, height: Int, sideways: Boolean) = inst.runOnMainSync {
        view = TouchControlsView(inst.targetContext)
        view.sideways = sideways
        view.onMask = { mask = it }
        view.measure(View.MeasureSpec.makeMeasureSpec(width, View.MeasureSpec.EXACTLY),
            View.MeasureSpec.makeMeasureSpec(height, View.MeasureSpec.EXACTLY))
        view.layout(0, 0, width, height)
    }

    private fun centre(bit: Int) = view.touchLayout!!.buttons.first { it.bit == bit }.let { it.cx to it.cy }

    /** A point on the D-pad: 70 % out from the centre at `degrees` (0 = right, 90 = up). */
    private fun dpad(degrees: Double): Pair<Float, Float> {
        val l = view.touchLayout!!
        val a = Math.toRadians(degrees)
        return (l.dpadX + 0.7f * l.dpadRadius * Math.cos(a).toFloat()) to (l.dpadY - 0.7f * l.dpadRadius * Math.sin(a).toFloat())
    }

    private fun send(action: Int, vararg fingers: Pair<Float, Float>) {
        val props = Array(fingers.size) { i ->
            MotionEvent.PointerProperties().apply { id = i; toolType = MotionEvent.TOOL_TYPE_FINGER }
        }
        val coords = Array(fingers.size) { i ->
            MotionEvent.PointerCoords().apply { x = fingers[i].first; y = fingers[i].second; pressure = 1f; size = 1f }
        }
        val now = SystemClock.uptimeMillis()
        val e = MotionEvent.obtain(now, now, action, fingers.size, props, coords, 0, 0, 1f, 1f, 0, 0,
            InputDevice.SOURCE_TOUCHSCREEN, 0)
        inst.runOnMainSync { view.dispatchTouchEvent(e) }
        e.recycle()
    }

    private fun pointer(action: Int, index: Int) = action or (index shl MotionEvent.ACTION_POINTER_INDEX_SHIFT)

    @Test
    fun oneFingerPressesAndReleasesA() {
        makeView(1080, 1700, sideways = false)
        send(MotionEvent.ACTION_DOWN, centre(KeyMap.A))
        assertEquals(KeyMap.A, mask)
        send(MotionEvent.ACTION_UP, centre(KeyMap.A))
        assertEquals(0, mask)
    }

    @Test
    fun twoFingersAAndADiagonal() {
        makeView(1080, 1700, sideways = false)
        val a = centre(KeyMap.A)
        val upRight = dpad(45.0)
        send(MotionEvent.ACTION_DOWN, a)
        send(pointer(MotionEvent.ACTION_POINTER_DOWN, 1), a, upRight)
        assertEquals(KeyMap.A or KeyMap.UP or KeyMap.RIGHT, mask)
        send(pointer(MotionEvent.ACTION_POINTER_UP, 0), a, upRight)
        assertEquals(KeyMap.UP or KeyMap.RIGHT, mask)
        send(MotionEvent.ACTION_UP, upRight)
        assertEquals(0, mask)
    }

    @Test
    fun slidingFromBToAMovesThePress() {
        makeView(1080, 1700, sideways = false)
        send(MotionEvent.ACTION_DOWN, centre(KeyMap.B))
        assertEquals(KeyMap.B, mask)
        send(MotionEvent.ACTION_MOVE, centre(KeyMap.A))
        assertEquals(KeyMap.A, mask)
        send(MotionEvent.ACTION_UP, centre(KeyMap.A))
        assertEquals(0, mask)
    }

    @Test
    fun hidingReleasesAndTheShowingTouchPressesNothing() {
        makeView(1080, 1700, sideways = false)
        var shown = false
        view.onShow = { shown = true }
        send(MotionEvent.ACTION_DOWN, centre(KeyMap.A))
        inst.runOnMainSync { view.hidden = true } // a keyboard key was pressed
        assertEquals(0, mask)
        send(MotionEvent.ACTION_UP, centre(KeyMap.A))
        send(MotionEvent.ACTION_DOWN, centre(KeyMap.A)) // shows the buttons...
        assertEquals(0, mask) // ...without pressing A
        assertFalse(view.hidden)
        assertEquals(true, shown)
        send(MotionEvent.ACTION_UP, centre(KeyMap.A))
        send(MotionEvent.ACTION_DOWN, centre(KeyMap.A))
        assertEquals(KeyMap.A, mask)
    }

    @Test
    fun sidewaysButtonsWorkToo() {
        makeView(2400, 1080, sideways = true)
        for (bit in listOf(KeyMap.A, KeyMap.B, KeyMap.L, KeyMap.R, KeyMap.START, KeyMap.SELECT)) {
            send(MotionEvent.ACTION_DOWN, centre(bit))
            assertEquals(bit, mask)
            send(MotionEvent.ACTION_UP, centre(bit))
        }
        send(MotionEvent.ACTION_DOWN, dpad(180.0))
        assertEquals(KeyMap.LEFT, mask)
        send(MotionEvent.ACTION_UP, dpad(180.0))
        assertEquals(0, mask)
    }
}
```

- [x] **Step 2: Run to see them fail**

```bash
bash tools/dev.sh androidtest
```

Expected: FAIL — `Unresolved reference 'TouchControlsView'`.

- [x] **Step 3: Write the view**

`app/src/main/java/com/tengen/gbaemu/TouchControlsView.kt`:

```kotlin
package com.tengen.gbaemu

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Path
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View

/**
 * The on-screen GBA buttons (layout A). Every finger is followed; the pressed buttons go to [onMask] whenever they
 * change, each newly pressed one with a vibration tick (Android's own touch feedback, so the phone's setting applies).
 * While [hidden] (a keyboard or gamepad is in use) nothing is drawn; the next touch shows the buttons again.
 */
class TouchControlsView(context: Context) : View(context) {
    /** True: sideways — see-through buttons over the whole screen. False: upright — the area below the game. */
    var sideways = false
        set(value) {
            if (field != value) {
                field = value
                relayout()
            }
        }

    var hidden = false
        set(value) {
            if (field == value) return
            field = value
            if (value) setMask(0)
            invalidate()
        }

    var onMask: (Int) -> Unit = {}
    var onShow: () -> Unit = {}

    var touchLayout: TouchLayout? = null
        private set

    private var mask = 0
    private var ignoreUntilUp = false // the touch that showed the buttons presses nothing

    private val density = resources.displayMetrics.density
    private val fill = Paint(Paint.ANTI_ALIAS_FLAG)
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        textAlign = Paint.Align.CENTER
        isFakeBoldText = true
    }

    private val dpadShape = Path() // the cross as one shape, so see-through arms do not overlap

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) = relayout()

    private fun relayout() {
        val l = if (width > 0 && height > 0) TouchLayout(width.toFloat(), height.toFloat(), density, sideways) else null
        touchLayout = l
        dpadShape.reset()
        if (l != null) {
            val r = l.dpadRadius
            val a = r * ARM
            val across = Path().apply { addRoundRect(l.dpadX - r, l.dpadY - a, l.dpadX + r, l.dpadY + a, a / 3, a / 3, Path.Direction.CW) }
            val down = Path().apply { addRoundRect(l.dpadX - a, l.dpadY - r, l.dpadX + a, l.dpadY + r, a / 3, a / 3, Path.Direction.CW) }
            dpadShape.op(across, down, Path.Op.UNION)
        }
        invalidate()
    }

    @SuppressLint("ClickableViewAccessibility") // game buttons, not clicks
    override fun onTouchEvent(event: MotionEvent): Boolean {
        val layout = touchLayout ?: return false
        val action = event.actionMasked
        if (hidden) {
            if (action == MotionEvent.ACTION_DOWN) {
                hidden = false
                ignoreUntilUp = true
                onShow()
            }
            return true
        }
        if (ignoreUntilUp) {
            if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) ignoreUntilUp = false
            return true
        }
        var pressed = 0
        if (action != MotionEvent.ACTION_UP && action != MotionEvent.ACTION_CANCEL) {
            val lifted = if (action == MotionEvent.ACTION_POINTER_UP) event.actionIndex else -1
            for (i in 0 until event.pointerCount) {
                if (i != lifted) pressed = pressed or layout.buttonsAt(event.getX(i), event.getY(i))
            }
        }
        setMask(pressed)
        return true
    }

    private fun setMask(pressed: Int) {
        if (pressed == mask) return
        if (pressed and mask.inv() != 0) performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY)
        mask = pressed
        onMask(pressed)
        invalidate()
    }

    override fun onDraw(canvas: Canvas) {
        val layout = touchLayout ?: return
        if (hidden) return
        // Upright: solid grey on black. Sideways: see-through white over the game's side bars.
        val idle = if (sideways) 0x55FFFFFF else 0xFFB4B4B4.toInt()
        val down = if (sideways) 0x99FFFFFF.toInt() else 0xFF6E6E6E.toInt()
        val armDown = if (sideways) 0x55FFFFFF else down // drawn over the cross
        text.color = if (sideways) 0xDDFFFFFF.toInt() else 0xFF222222.toInt()

        // D-pad: the cross, then each pressed arm (outside the middle) again, darker.
        fill.color = idle
        canvas.drawPath(dpadShape, fill)
        val x = layout.dpadX
        val y = layout.dpadY
        val r = layout.dpadRadius
        val a = r * ARM
        fill.color = armDown
        if (mask and KeyMap.UP != 0) canvas.drawRoundRect(x - a, y - r, x + a, y - a, a / 3, a / 3, fill)
        if (mask and KeyMap.DOWN != 0) canvas.drawRoundRect(x - a, y + a, x + a, y + r, a / 3, a / 3, fill)
        if (mask and KeyMap.LEFT != 0) canvas.drawRoundRect(x - r, y - a, x - a, y + a, a / 3, a / 3, fill)
        if (mask and KeyMap.RIGHT != 0) canvas.drawRoundRect(x + a, y - a, x + r, y + a, a / 3, a / 3, fill)

        for (b in layout.buttons) {
            fill.color = if (mask and b.bit != 0) down else idle
            if (b.round) {
                canvas.drawCircle(b.cx, b.cy, (b.right - b.left) / 2, fill)
            } else {
                val corner = (b.bottom - b.top) / 2
                canvas.drawRoundRect(b.left, b.top, b.right, b.bottom, corner, corner, fill)
            }
            text.textSize = if (b.round) 22 * density else 13 * density
            canvas.drawText(b.label, b.cx, b.cy - (text.ascent() + text.descent()) / 2, text)
        }
    }

    private companion object {
        const val ARM = 0.36f // the D-pad's arm half-width, as a part of its radius
    }
}
```

(The D-pad is one `Path` union: drawing the two see-through arms separately made their overlap in the middle lighter.)

- [x] **Step 4: Run the tests (three times)**

```bash
for i in 1 2 3; do rm -rf app/build/outputs/androidTest-results; bash tools/dev.sh androidtest 2>&1 | grep -E "^e: |Starting|BUILD FAILED"; grep -h -o 'tests="[0-9]*" failures="[0-9]*"' app/build/outputs/androidTest-results/connected/debug/*.xml; done
echo "C warnings: $(bash tools/dev.sh apk --rerun-tasks 2>&1 | grep -c 'warning:.*\.[ch]:')"
```

Expected: `tests="29" failures="0"` three times (24 + 5); `0` C warnings.

- [x] **Step 5: Commit**

```bash
git add app/src/main/java/com/tengen/gbaemu/TouchControlsView.kt app/src/androidTest/java/com/tengen/gbaemu/TouchTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-09-26-milestone-4c-controls-saves.md
git commit -m "feat(android): on-screen GBA buttons - multi-touch, slide, diagonals, vibration, hide on keys" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

**Batch 2 (Tasks 3-4) done — checkpoint with the user.**

---
### Task 5: The app — start screen, Continue, import and Back questions

**Files:**
- Create: `app/src/main/java/com/tengen/gbaemu/LastGame.kt`
- Modify: `app/src/main/java/com/tengen/gbaemu/Screens.kt`, `app/src/main/java/com/tengen/gbaemu/MainActivity.kt`, `app/src/main/AndroidManifest.xml`, `tools/dev.sh`

(No new automated test: the flow is checked by Claude on the emulator in Step 3 and by the user in Task 6; its parts are covered by Tasks 1-4.)

- [x] **Step 1: Write the code**

`app/src/main/java/com/tengen/gbaemu/LastGame.kt`:

```kotlin
package com.tengen.gbaemu

import android.content.Context
import android.net.Uri

/** The last ROM opened (its document URI, readable later thanks to a persisted permission, and its title). */
class LastGame(context: Context) {
    private val prefs = context.getSharedPreferences("last_game", Context.MODE_PRIVATE)

    val uri: Uri? get() = prefs.getString("uri", null)?.let(Uri::parse)
    val title: String? get() = prefs.getString("title", null)

    fun remember(uri: Uri, title: String) = prefs.edit().putString("uri", uri.toString()).putString("title", title).apply()

    fun forget() = prefs.edit().clear().apply()
}
```

Replace `app/src/main/java/com/tengen/gbaemu/Screens.kt`:

```kotlin
package com.tengen.gbaemu

import android.content.Context
import android.view.SurfaceHolder
import android.view.SurfaceView
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView

/** Continue (when there is a last game), Open ROM, Controls, and a message when something went wrong. */
@Composable
fun StartScreen(message: String?, lastTitle: String?, onContinue: () -> Unit, onOpenRom: () -> Unit,
                onControls: () -> Unit) {
    Surface(modifier = Modifier.fillMaxSize()) { /* the theme's background and text colours */
        Column(
            modifier = Modifier.fillMaxSize().padding(24.dp),
            verticalArrangement = Arrangement.Center,
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Text("GBA Emulator", style = MaterialTheme.typography.headlineMedium)
            Spacer(Modifier.height(24.dp))
            if (lastTitle != null) {
                Button(onClick = onContinue) { Text("Continue: $lastTitle") }
                Spacer(Modifier.height(12.dp))
            }
            Button(onClick = onOpenRom) { Text("Open ROM") }
            Spacer(Modifier.height(12.dp))
            OutlinedButton(onClick = onControls) { Text("Controls") }
            if (message != null) {
                Spacer(Modifier.height(16.dp))
                Text(message, color = MaterialTheme.colorScheme.error)
            }
        }
    }
}

/** Which keyboard key and gamepad button press each GBA button (from KeyMap, so it is always right). */
@Composable
fun ControlsDialog(onClose: () -> Unit) {
    AlertDialog(
        onDismissRequest = onClose,
        title = { Text("Controls") },
        text = {
            Column {
                Row { for (h in listOf("GBA", "Keyboard", "Gamepad")) Text(h, Modifier.weight(1f), fontWeight = FontWeight.Bold) }
                for (b in KeyMap.bindings) {
                    Row(Modifier.padding(top = 4.dp)) {
                        Text(b.button, Modifier.weight(1f))
                        Text(b.keyboard.values.joinToString(), Modifier.weight(1f))
                        Text(b.gamepad.values.joinToString(), Modifier.weight(1f))
                    }
                }
                Spacer(Modifier.height(12.dp))
                Text("On the phone: the on-screen buttons. They hide while a keyboard or gamepad is used; touch the screen to bring them back.")
            }
        },
        confirmButton = { TextButton(onClick = onClose) { Text("Close") } },
    )
}

/** A game with a save chip and no save yet: start fresh, or bring in a .sav file. Dismissing cancels the opening. */
@Composable
fun ImportDialog(error: String?, onStartFresh: () -> Unit, onImport: () -> Unit, onCancel: () -> Unit) {
    AlertDialog(
        onDismissRequest = onCancel,
        title = { Text("No save for this game yet") },
        text = {
            Column {
                Text("Start a fresh game, or import a .sav file (for example from a PC emulator)?")
                if (error != null) {
                    Spacer(Modifier.height(12.dp))
                    Text(error, color = MaterialTheme.colorScheme.error)
                }
            }
        },
        confirmButton = { TextButton(onClick = onImport) { Text("Import a .sav") } },
        dismissButton = { TextButton(onClick = onStartFresh) { Text("Start fresh") } },
    )
}

/** Back while playing: the game is paused while this asks. */
@Composable
fun QuitDialog(onQuit: () -> Unit, onKeepPlaying: () -> Unit) {
    AlertDialog(
        onDismissRequest = onKeepPlaying,
        title = { Text("Quit the game?") },
        text = { Text("Progress since your last in-game save is lost.") },
        confirmButton = { TextButton(onClick = onQuit) { Text("Quit") } },
        dismissButton = { TextButton(onClick = onKeepPlaying) { Text("Keep playing") } },
    )
}

/** The game (a SurfaceView handed to the native render thread) and the touch buttons, in layout A. */
@Composable
fun GameScreen(controlsHidden: Boolean, onTouchMask: (Int) -> Unit, onShowControls: () -> Unit,
               onDrawFailed: () -> Unit) {
    BoxWithConstraints(Modifier.fillMaxSize().background(Color.Black)) {
        val sideways = maxWidth > maxHeight
        val game: @Composable (Modifier) -> Unit = { modifier ->
            AndroidView(modifier = modifier, factory = { gameSurface(it, onDrawFailed) })
        }
        val buttons: @Composable (Modifier) -> Unit = { modifier ->
            AndroidView(
                modifier = modifier,
                factory = { TouchControlsView(it).also { onTouchMask(0) } }, // a new view (e.g. after turning): nothing held
                update = { v ->
                    v.sideways = sideways
                    v.hidden = controlsHidden
                    v.onMask = onTouchMask
                    v.onShow = onShowControls
                },
            )
        }
        if (sideways) {
            Box(Modifier.fillMaxSize()) {
                game(Modifier.fillMaxSize()) // the game centred, black bars left and right
                buttons(Modifier.fillMaxSize()) // see-through, over the bars
            }
        } else {
            Column(Modifier.fillMaxSize()) {
                game(Modifier.fillMaxWidth().aspectRatio(1.5f)) // 240×160 across the full width, at the top
                buttons(Modifier.fillMaxWidth().weight(1f))
            }
        }
    }
}

private fun gameSurface(context: Context, onDrawFailed: () -> Unit) = SurfaceView(context).apply {
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
```

Replace `app/src/main/java/com/tengen/gbaemu/MainActivity.kt`:

```kotlin
package com.tengen.gbaemu

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.view.KeyEvent
import android.view.WindowManager
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import java.io.File
import java.nio.ByteBuffer
import kotlinx.coroutines.delay

class MainActivity : ComponentActivity() {
    private var playing by mutableStateOf(false)
    private var message by mutableStateOf<String?>(null)
    private var lastTitle by mutableStateOf<String?>(null)
    private var showControls by mutableStateOf(false)
    private var askImport by mutableStateOf(false)
    private var importError by mutableStateOf<String?>(null)
    private var askQuit by mutableStateOf(false)
    private var controlsHidden by mutableStateOf(false)
    private var keyboardMask = 0
    private var touchMask = 0
    private lateinit var lastGame: LastGame

    private val openRom = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) openFromPicker(uri)
    }
    private val pickSave = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) importFrom(uri)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        lastGame = LastGame(this)
        lastTitle = lastGame.title
        setContent {
            MaterialTheme(colorScheme = darkColorScheme()) {
                if (playing) {
                    GameScreen(
                        controlsHidden = controlsHidden,
                        onTouchMask = { touchMask = it; sendKeys() },
                        onShowControls = { controlsHidden = false },
                        onDrawFailed = {
                            stopGame()
                            message = "This device cannot show the game: OpenGL ES 3.0 failed."
                        },
                    )
                    LaunchedEffect(Unit) { // a failed save-file write: tell once per game
                        while (true) {
                            delay(1000)
                            if (NativeBridge.saveError()) {
                                toast("Could not write the save file.")
                                break
                            }
                        }
                    }
                } else {
                    StartScreen(message, lastTitle, onContinue = ::openLast,
                        onOpenRom = { openRom.launch(arrayOf("*/*")) }, onControls = { showControls = true })
                }
                if (showControls) ControlsDialog { showControls = false }
                if (askImport) {
                    ImportDialog(importError,
                        onStartFresh = { askImport = false; startGame() },
                        onImport = { pickSave.launch(arrayOf("*/*")) },
                        onCancel = { askImport = false; NativeBridge.stop() })
                }
                if (askQuit) {
                    QuitDialog(onQuit = { askQuit = false; stopGame() },
                        onKeepPlaying = { askQuit = false; NativeBridge.resume() })
                }
                BackHandler(enabled = playing && !askQuit) {
                    NativeBridge.pause()
                    askQuit = true
                }
            }
        }
    }

    private fun readBytes(uri: Uri): ByteArray? = try {
        contentResolver.openInputStream(uri)?.use { it.readBytes() }
    } catch (e: Exception) {
        null
    }

    private fun openFromPicker(uri: Uri) {
        try { /* so Continue can read it again after a restart */
            contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
        } catch (e: SecurityException) {
        }
        open(uri, fromContinue = false)
    }

    private fun openLast() {
        open(lastGame.uri ?: return, fromContinue = true)
    }

    private fun open(uri: Uri, fromContinue: Boolean) {
        val bytes = readBytes(uri)
        if (bytes == null) {
            if (fromContinue) {
                lastGame.forget()
                lastTitle = null
                message = "The last game's file can't be opened any more."
            } else {
                message = "Could not read that file."
            }
            return
        }
        val rom = ByteBuffer.allocateDirect(bytes.size).put(bytes)
        rom.flip()
        val saves = File(filesDir, "saves").apply { mkdirs() }
        if (NativeBridge.loadRom(rom, File(saves, GameId.of(bytes) + ".sav").path) != 0) {
            message = "Not a GBA ROM (size 0 or over 32 MiB)."
            return
        }
        val title = NativeBridge.romTitle().trim().ifEmpty { uri.lastPathSegment?.substringAfterLast('/') ?: "last game" }
        lastGame.remember(uri, title)
        lastTitle = title
        message = null
        when (NativeBridge.saveStatus()) {
            NativeBridge.SAVE_EMPTY -> {
                importError = null
                askImport = true
            }
            NativeBridge.SAVE_MISFIT -> {
                startGame()
                toast("The save file for this game doesn't fit it and was not loaded.")
            }
            else -> startGame()
        }
    }

    private fun importFrom(uri: Uri) {
        val bytes = readBytes(uri)
        when {
            bytes == null -> importError = "Could not read that file."
            NativeBridge.importSave(bytes) != 0 -> importError = "This .sav doesn't fit this game."
            else -> {
                askImport = false
                startGame()
            }
        }
    }

    private fun startGame() {
        keyboardMask = 0
        touchMask = 0
        controlsHidden = false
        NativeBridge.start()
        playing = true
        setFullScreen(true)
    }

    private fun stopGame() {
        NativeBridge.stop() // writes the save
        playing = false
        setFullScreen(false)
    }

    private fun sendKeys() = NativeBridge.setKeys(KeyMap.merge(keyboardMask, touchMask))

    private fun toast(text: String) = Toast.makeText(this, text, Toast.LENGTH_LONG).show()

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
                KeyEvent.ACTION_DOWN -> keyboardMask = KeyMap.update(keyboardMask, event.keyCode, true)
                KeyEvent.ACTION_UP -> keyboardMask = KeyMap.update(keyboardMask, event.keyCode, false)
            }
            controlsHidden = true // a keyboard or gamepad is in use
            sendKeys()
            return true
        }
        return super.dispatchKeyEvent(event)
    }

    override fun onPause() {
        super.onPause()
        NativeBridge.pause() // returns once the save is written
    }

    override fun onResume() {
        super.onResume()
        if (!askQuit) NativeBridge.resume()
    }

    override fun onDestroy() {
        if (isFinishing) NativeBridge.stop()
        super.onDestroy()
    }
}
```

Replace `app/src/main/AndroidManifest.xml`:

```xml
<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android">

    <application
        android:label="GBA Emulator"
        android:theme="@android:style/Theme.Material.NoActionBar">

        <!-- configChanges: rotation and keyboard changes resize the surface instead of restarting the game.
             singleTask: one game screen only. Without it, opening the app again (e.g. from its icon after it was
             started some other way) stacks a new, empty start screen on top of the running game. -->
        <activity
            android:name=".MainActivity"
            android:launchMode="singleTask"
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

(`singleTask` — found in the user's trial: without it, opening the app from its icon after it had been started another way stacked a new, empty start screen on top of the paused game.)

In `tools/dev.sh` replace the `launch)` line with:

```bash
    launch) emu_up && adb shell am start -a android.intent.action.MAIN -c android.intent.category.LAUNCHER -n "$APP_ID/.MainActivity" ;; # as the app icon does
```

- [x] **Step 2: Build and run every test**

```bash
bash tools/dev.sh jvmtest && bash tools/dev.sh unit
rm -rf app/build/outputs/androidTest-results; bash tools/dev.sh androidtest 2>&1 | grep -E "^e: |Starting|BUILD FAILED"; grep -h -o 'tests="[0-9]*" failures="[0-9]*"' app/build/outputs/androidTest-results/connected/debug/*.xml
echo "C warnings: $(bash tools/dev.sh apk --rerun-tasks 2>&1 | grep -c 'warning:.*\.[ch]:')"
```

Expected: Kotlin 14/14, `363 tests, 0 failed`, on-device `tests="29" failures="0"`, `0` C warnings.

- [x] **Step 3: Claude's check on the emulator (adb)**

Use a scratchpad folder `$SP` for screenshots and files. Helpers:

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
texts() { ADB shell uiautomator dump /sdcard/ui.xml >/dev/null; ADB shell cat /sdcard/ui.xml | tr '>' '\n' | grep -oE 'text="[^"]+"'; }
copies() { ADB shell dumpsys activity activities | grep -c "Hist  #.*com.tengen.gbaemu"; }
icon() { ADB shell am start -a android.intent.action.MAIN -c android.intent.category.LAUNCHER -f 0x10200000 -n com.tengen.gbaemu/.MainActivity >/dev/null 2>&1; }
```

Then (the emulator's Download folder already has `m7_demo.gba`; the others are pushed here):

```bash
python -c "open('$SP/flash64_test.sav','wb').write(bytes((i*5)&255 for i in range(65536))); open('$SP/bad.sav','wb').write(bytes(1000))"
bash tools/dev.sh push tests/roms/jsmolka/flash64.gba; bash tools/dev.sh push "$SP/flash64_test.sav"; bash tools/dev.sh push "$SP/bad.sav"
ADB shell settings put system accelerometer_rotation 0; ADB shell settings put system user_rotation 0
ADB shell input keyevent KEYCODE_HOME; ADB shell am force-stop com.tengen.gbaemu; ADB shell am force-stop com.google.android.documentsui
ADB shell run-as com.tengen.gbaemu rm -f files/saves/d0aeb7c2.sav files/saves/d0aeb7c2.sav.bak   # flash64's test save only - never the user's saves
bash tools/dev.sh install && bash tools/dev.sh launch && sleep 3 && texts && shot start
tap_on text "Controls" && sleep 1 && shot controls && tap_on text "Close"
tap_on text "Open ROM" && pick_download m7_demo.gba && sleep 3 && shot upright
ADB shell input swipe 226 1453 226 1453 700 & sleep 0.4; shot dpad_up; wait
ADB shell settings put system user_rotation 1; sleep 3; shot sideways; ADB shell settings put system user_rotation 0; sleep 2
ADB shell input keyevent KEYCODE_BACK; sleep 1; texts; tap_on text "Keep playing"; sleep 1; texts
ADB shell input keyevent KEYCODE_HOME; sleep 2; icon; sleep 2; echo "copies: $(copies)"; texts
ADB shell input keyevent KEYCODE_BACK; sleep 1; tap_on text "Quit"; sleep 1; ADB shell am force-stop com.tengen.gbaemu
bash tools/dev.sh launch; sleep 2; texts
tap_on text "Open ROM" && pick_download flash64.gba && sleep 2 && texts
tap_on text "Import a .sav" && sleep 2 && pick_download bad.sav && sleep 2 && texts
tap_on text "Import a .sav" && sleep 2 && pick_download flash64_test.sav && sleep 3 && texts
ADB shell run-as com.tengen.gbaemu md5sum files/saves/d0aeb7c2.sav; md5sum "$SP/flash64_test.sav"
ADB shell input keyevent KEYCODE_BACK; sleep 1; tap_on text "Quit"; sleep 1; tap_on text "Continue: GBA Tests"; sleep 2; texts
ADB shell input keyevent KEYCODE_BACK; sleep 1; tap_on text "Quit"
ADB logcat -d -s AndroidRuntime:E | tail
```

Expected (look at every screenshot):
- `start`: "GBA Emulator", **Open ROM**, **Controls** (and **Continue: …** if a game was opened before); `controls`: the table (A = X / A, B = Z / B, L = A / L1, R = S / R1, Start = Enter / Start, Select = Backspace / Select, arrows / D-pad) and the note about the on-screen buttons.
- `upright`: the Mode 7 demo full width at the top (1080×720), L/R, a D-pad, A/B, Select/Start below it; `dpad_up`: the Up arm darker and the camera moved.
- `sideways`: the game in the middle, see-through buttons over the side bars, Select/Start over the bottom of the game. (Holding Up for long drives the Mode 7 camera off the map: then the floor is black - not a bug.)
- Back → texts "Quit the game?", "Progress since your last in-game save is lost.", "Keep playing", "Quit"; after Keep playing no texts (the game).
- After Home and the icon: `copies: 1` and no texts (the game again, not the start screen).
- After Quit + force-stop + launch: "Continue: m7_demo", "Open ROM", "Controls".
- `flash64.gba`: "No save for this game yet", "Start fresh", "Import a .sav"; `bad.sav` → also "This .sav doesn't fit this game."; `flash64_test.sav` → no texts (the game started; it then shows "Failed test 001" — expected with that made-up save: `bash tools/dev.sh run tests/roms/jsmolka/flash64.gba --frames 120 --result-reg 12 --save <copy of flash64_test.sav>` fails test 1 on the PC too, and passes with no save file); both md5 sums equal.
- `Continue: GBA Tests` → no texts (no import question: the save was loaded). No `AndroidRuntime` crash.

- [x] **Step 4: Commit**

```bash
git add app/src/main/java/com/tengen/gbaemu/LastGame.kt app/src/main/java/com/tengen/gbaemu/Screens.kt app/src/main/java/com/tengen/gbaemu/MainActivity.kt app/src/main/AndroidManifest.xml tools/dev.sh docs/PROGRESS.md docs/superpowers/plans/2026-09-26-milestone-4c-controls-saves.md
git commit -m "feat(android): start screen with Continue and Controls, import and Back questions, one game screen" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 6: User check and milestone 4c exit

- [x] **Step 1: Full run**

```bash
bash tools/dev.sh test && bash tools/dev.sh unit && bash tools/dev.sh jvmtest && bash tools/dev.sh androidtest
```

Expected: CTest 56/56 (sanitizers on, no reports), 363 unit tests, Kotlin 14, on-device 29 (emulator only).

- [x] **Step 2: The user's check** (the app installed and open on the emulator; the user's `hack-1.sav` from their Downloads folder pushed to the emulator's Download folder; never clear the app's data — if their game already has a save in the app, the import question is simply skipped)

The user: opens their game A hack → "No save for this game yet" → Import a .sav → their PC save → continues where they were on the PC; plays with only the on-screen buttons (walking, menus, a diagonal); saves in the game → Back → Quit → Continue → the save is there; Home → the icon → exactly where they were; turns the phone → the buttons move to the sides; a keyboard key hides the buttons, a click on the screen brings them back.

- [x] **Step 3: Docs**

`docs/PROGRESS.md` (4c ✅ with the numbers and the user's check; Next = milestone 4d brainstorming — rumble + tilt/gyro/light sensors); spec status → `Implemented — milestone 4c complete <date> (tag milestone-4c)`; parent spec §10 row 4 → 4c ✅; `CLAUDE.md` key files → 4d needs brainstorming; tick every box here.

- [x] **Step 4: Commit, tag, merge** — ask the user how to integrate (so far always a fast-forward merge into `main`); then superpowers:finishing-a-development-branch, `bash tools/dev.sh test` and `bash tools/dev.sh androidtest` on `main`, delete the branch, record the merge in PROGRESS.md; delete the prototype and the replay worktree (`rm -rf` the folders in Git Bash, `git worktree prune`, `git branch -D proto-4c replay-4c`) with the user's OK.

```bash
git add -A docs CLAUDE.md
git commit -m "docs: milestone 4c complete - touch controls, save files, leaving and returning" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git tag milestone-4c
```
