# Milestone 4d — Rumble and motion/light sensors on Android — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. (This project always executes plans inline with superpowers:executing-plans — see `CLAUDE.md`.)

**Goal:** The cartridge extras work on Android: tilt from the phone's accelerometer, turn from its gyroscope, light from its light sensor (or a ☀ slider), I/J/K/L and Q/E keys, and rumble on the phone's vibrator — checked by the user with game D on the emulator and on their real phone.

**Architecture:** Kotlin reads the sensors (`MotionInput`) and turns them into the core's numbers with pure, PC-tested maths (`MotionMath`); three numbers go to the C emulation thread as atomics, applied at each frame's start (like the keys). The thread keeps the rumble motor's state after each frame; `Rumble` polls it once per display frame (Choreographer) and drives the vibrator. Three tiny own test cartridges carry the game codes of game D (gyro + rumble), game F (tilt) and game E (solar), so the core's own game list flags them. `dev.sh` gains deliberate commands for the user's real phone.

**Tech Stack:** Kotlin, Android `SensorManager` / `VibratorManager` / `Choreographer`, Jetpack Compose (the ☀ slider), C11 + JNI, GNU as (ARM) through the NDK's clang for the test cartridges, JUnit 4 + androidx.test.

**Spec:** `docs/superpowers/specs/2026-09-26-milestone-4d-rumble-sensors-design.md`

---

## Conventions (same as 4a–4c)

- Git Bash from the repo root. PC: `bash tools/dev.sh <configure|unit [filter]|test|run>`. Android: `bash tools/dev.sh <apk|jvmtest|emu|install|launch|androidtest|push <file>>` — Gradle `--offline` with Android Studio's JBR; a missing artifact → stop and ask the user before any download.
- **Only the phone emulator, never the user's phone**, except the Task 6 step where the user asks for it: `dev.sh` exports `ANDROID_SERIAL=<emulator>`; the helpers below refuse when no emulator is running. The user's phone (phone A) is often plugged in.
- `androidtest` leaves the app and its saves installed (`gradle.properties`, since 4c). Never uninstall the app, `pm clear` it or delete its `files/saves` without the user's OK.
- **If the emulator's Android breaks** (`adb shell getprop init.svc.zygote` says `restarting`, storage gives 'Transport endpoint is not connected'): a quick-boot snapshot keeps restoring the broken state — ask the user, then `adb -s emulator-5554 emu kill` and start it with `"$LOCALAPPDATA/Android/Sdk/emulator/emulator.exe" -avd Pixel_9_API_36_1 -no-boot-anim -no-snapshot-load` (a cold boot; the app's data survives). Seen on 2026-09-27 after the PC slept: system_server crashed in `AppOpsService` on a negative wakelock duration.
- Unit tests: `TEST(name)` in `tests/unit/*.c`. One commit per task; messages end with `-m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"`. Each commit also includes `docs/PROGRESS.md` and this plan (ticked boxes) — project rule in `CLAUDE.md`.
- Branch `milestone-4d` (already created; the spec commits are on it). Checkpoints with the user after Tasks 2 and 4; Task 6 is the user's check and the milestone exit.
- This plan's code is the prototype's (worktree `<scratchpad>/p4d`, branch `proto-4d`), which the user tried on the emulator (Q/E) and on their phone (turning, rumble) on 2026-09-27.

## File map

```
tools/build_test_roms.py; tests/own_roms/src/hw_{gyro,tilt,solar}.S (+ the built .gba)   Task 1
platform/android/emu_thread.{h,c}, jni_bridge.c; NativeBridge.kt; androidTest/MotionTest.kt   Task 2
app/src/main/java/com/tengen/gbaemu/MotionMath.kt; app/src/test/.../MotionMathTest.kt          Task 3
MotionInput.kt, Rumble.kt, Screens.kt, MainActivity.kt; app/src/main/AndroidManifest.xml      Task 4
tools/dev.sh (phone-install, phone-uninstall, phone-push)                                     Task 5
```

---
### Task 1: Test cartridges with the extras' game codes

**Files:**
- Create: `tests/own_roms/src/hw_gyro.S`, `tests/own_roms/src/hw_tilt.S`, `tests/own_roms/src/hw_solar.S` (and the built `tests/own_roms/hw_*.gba`)
- Modify: `tools/build_test_roms.py`

(No unit test: the cartridges are checked by `MotionTest` in Task 2 through the core's own game list. Here: their headers, and the existing own ROMs must rebuild byte-identical.)

- [x] **Step 1: The builder takes a game code from the source**

Replace `tools/build_test_roms.py`:

```python
#!/usr/bin/env python3
"""Assemble our own test ROMs: tests/own_roms/src/<name>.S -> tests/own_roms/<name>.gba.

Uses clang, ld.lld and llvm-objcopy from the Android NDK (or from $CLANG_BIN_DIR), like
tools/gen_bios_stub.py. Fills the cartridge header (title, game code, maker, fixed byte and
complement checksum); the Nintendo logo area stays blank, so these ROMs are for HLE mode.
Re-run after editing a source and commit the regenerated .gba files.
"""
import glob
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC_DIR = ROOT / "tests/own_roms/src"
OUT_DIR = ROOT / "tests/own_roms"


def find_bin_dir() -> Path:
    if os.environ.get("CLANG_BIN_DIR"):
        return Path(os.environ["CLANG_BIN_DIR"])
    sdk = Path(os.environ.get("ANDROID_HOME") or Path(os.environ["LOCALAPPDATA"]) / "Android/Sdk")
    found = sorted(glob.glob(str(sdk / "ndk/*/toolchains/llvm/prebuilt/*/bin")))
    if not found:
        sys.exit("Android NDK not found; set CLANG_BIN_DIR to a directory containing clang")
    return Path(found[-1])


def tool(bin_dir: Path, name: str) -> str:
    exe = bin_dir / (name + ".exe")
    return str(exe if exe.exists() else bin_dir / name)


def game_code(src: Path) -> bytes:
    """A line `@ game code: XXXX` in the source picks the game code (the core's list of games with extras matches its
    first three letters); otherwise ZOWN."""
    for line in src.read_text(encoding="utf-8").splitlines():
        if line.startswith("@ game code:"):
            code = line.split(":", 1)[1].strip().encode("ascii")
            if len(code) != 4:
                sys.exit(f"{src.name}: a game code has 4 letters")
            return code
    return b"ZOWN"


def fill_header(rom: bytearray, name: str, code: bytes) -> None:
    rom[0xA0:0xAC] = name.upper().replace("_", " ")[:12].encode("ascii").ljust(12, b"\0")
    rom[0xAC:0xB0] = code     # game code
    rom[0xB0:0xB2] = b"00"    # maker
    rom[0xB2] = 0x96          # fixed value
    rom[0xBD] = (-(sum(rom[0xA0:0xBD]) + 0x19)) & 0xFF


def build(src: Path, bin_dir: Path) -> None:
    with tempfile.TemporaryDirectory() as tmp:
        elf = Path(tmp) / "rom.elf"
        raw = Path(tmp) / "rom.bin"
        subprocess.run(
            [tool(bin_dir, "clang"), "--target=armv4t-none-eabi", "-nostdlib", "-fuse-ld=lld", "-I", str(SRC_DIR),
             "-Wl,-Ttext=0x08000000", "-Wl,--entry=_start", str(src), "-o", str(elf)],
            check=True)
        subprocess.run(
            [tool(bin_dir, "llvm-objcopy"), "-O", "binary", "--only-section=.text", str(elf), str(raw)],
            check=True)
        rom = bytearray(raw.read_bytes())
    if len(rom) < 0xC0:
        sys.exit(f"{src.name}: too small to hold a header")
    fill_header(rom, src.stem, game_code(src))
    rom += b"\0" * (-len(rom) % 4)
    out = OUT_DIR / (src.stem + ".gba")
    out.write_bytes(rom)
    print(f"wrote {out.relative_to(ROOT).as_posix()} ({len(rom)} bytes)")


def main() -> None:
    bin_dir = find_bin_dir()
    sources = sorted(SRC_DIR.glob("*.S"))
    if not sources:
        sys.exit(f"no sources in {SRC_DIR}")
    for src in sources:
        build(src, bin_dir)


if __name__ == "__main__":
    main()
```

(Sources are read as UTF-8: an existing one contains a non-ASCII character.)

- [x] **Step 2: The three cartridges**

`tests/own_roms/src/hw_gyro.S`:

```asm
@ hw_gyro: a cartridge like game D (gyro + rumble in the core's game list, by its game code).
@ Turns the rumble motor (GPIO pin 3) on for 30 frames, then off for 30 frames, for ever.
@ The GPIO registers are left unreadable: they sit at 0x080000C4-0x080000C9, inside wait_frames (common.inc),
@ and once readable the CPU would fetch them instead of that code. Rumble needs no reads.
@ game code: RZWT
    .include "common.inc"
    rom_start
    ldr r4, =0x080000C4 @ GPIO data; +2 direction
    mov r0, #8
    strh r0, [r4, #2]   @ pin 3 (the motor) is an output
loop:
    mov r0, #8
    strh r0, [r4]       @ motor on
    frames 30
    mov r0, #0
    strh r0, [r4]       @ motor off
    frames 30
    b loop
```

(Found while prototyping: making the GPIO registers readable derailed this ROM after its first frame — `wait_frames` in `common.inc` sits at 0x080000C0, so the CPU fetched the GPIO registers at 0xC4–0xC9 instead of its code.)

`tests/own_roms/src/hw_tilt.S`:

```asm
@ hw_tilt: a cartridge like game F (tilt sensor in the core's game list, by its game code). Does nothing.
@ game code: KYGT
    .include "common.inc"
    rom_start
1:  frames 1
    b 1b
```

`tests/own_roms/src/hw_solar.S`:

```asm
@ hw_solar: a cartridge like game E (clock + solar sensor in the core's game list, by its game code). Does nothing.
@ game code: U3IT
    .include "common.inc"
    rom_start
1:  frames 1
    b 1b
```

- [x] **Step 3: Build and check**

```bash
python tools/build_test_roms.py
git status --short tests/own_roms
for r in hw_gyro hw_tilt hw_solar; do python -c "d=open('tests/own_roms/$r.gba','rb').read(); print('$r', d[0xA0:0xAC].rstrip(bytes(1)), d[0xAC:0xB0])"; done
```

Expected: the 8 ROMs written; `git status` lists only the three new sources and `.gba` files (the existing ROMs unchanged); `hw_gyro b'HW GYRO' b'RZWT'`, `hw_tilt b'HW TILT' b'KYGT'`, `hw_solar b'HW SOLAR' b'U3IT'`.

- [x] **Step 4: Commit**

```bash
git add tools/build_test_roms.py tests/own_roms/src/hw_gyro.S tests/own_roms/src/hw_tilt.S tests/own_roms/src/hw_solar.S tests/own_roms/hw_gyro.gba tests/own_roms/hw_tilt.gba tests/own_roms/hw_solar.gba docs/PROGRESS.md docs/superpowers/plans/2026-09-27-milestone-4d-rumble-sensors.md
git commit -m "test: own cartridges with the game codes of game D, game F and game E" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 2: Sensor values and rumble between Kotlin and the emulation thread

**Files:**
- Create: `app/src/androidTest/java/com/tengen/gbaemu/MotionTest.kt`
- Modify: `platform/android/emu_thread.h`, `platform/android/emu_thread.c`, `platform/android/jni_bridge.c`, `app/src/main/java/com/tengen/gbaemu/NativeBridge.kt`

- [x] **Step 1: Write the failing tests**

`app/src/androidTest/java/com/tengen/gbaemu/MotionTest.kt`:

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

    @Test
    fun sensorValuesReachTheCoreWithinAFrame() {
        load("../own_roms/hw_tilt.gba")
        NativeBridge.start()
        NativeBridge.setMotion(12000, -5000, 16000)
        NativeBridge.setLight(200)
        Thread.sleep(100)
        assertArrayEquals(intArrayOf(12000, -5000, 16000, 200), NativeBridge.debugMotionSeen())
        NativeBridge.setMotion(99999, -99999, 0) // out of range: clamped
        NativeBridge.setLight(300)
        Thread.sleep(100)
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

- [x] **Step 2: Run to see them fail**

```bash
bash tools/dev.sh androidtest
```

Expected: FAIL — `Unresolved reference 'HW_GYRO'`, `'cartHardware'`, `'setMotion'`, `'debugMotionSeen'`, `'rumbleActive'`.

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

/* Cartridge sensors (any thread; applied at the next frame start): tilt and turn ±32767, light 0..255. */
void emu_set_motion(emu_t *e, int tilt_x, int tilt_y, int turn);
void emu_set_light(emu_t *e, int level);
uint32_t emu_cart_hardware(const emu_t *e); /* GBA_HW_* extras of the loaded cartridge */
bool emu_rumble_active(emu_t *e);           /* the rumble motor after the last frame */
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
void emu_motion_seen(emu_t *e, int32_t out[4]); /* the sensor values last applied: tilt x, tilt y, turn, light */
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
    atomic_int tilt_x, tilt_y, turn, light; /* cartridge sensors, from the UI thread; applied at frame start */
    atomic_int seen[4];                     /* test hook: the sensor values last applied (x, y, turn, light) */
    atomic_bool rumble;                     /* the cartridge's rumble motor after the last frame */
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

/* The sensor values from the UI thread into the core (cheap: a few stores; harmless for carts without them). */
static void apply_sensors(emu_t *e) {
    int x = atomic_load(&e->tilt_x), y = atomic_load(&e->tilt_y);
    int turn = atomic_load(&e->turn), light = atomic_load(&e->light);
    gba_set_tilt(e->g, (int16_t)x, (int16_t)y);
    gba_set_gyro(e->g, (int16_t)turn);
    gba_set_light(e->g, (uint8_t)light);
    atomic_store(&e->seen[0], x);
    atomic_store(&e->seen[1], y);
    atomic_store(&e->seen[2], turn);
    atomic_store(&e->seen[3], light);
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
        apply_sensors(e);
        gba_run_frame(e->g);
        atomic_store(&e->rumble, gba_rumble_active(e->g) != 0);
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
    atomic_init(&e->tilt_x, 0);
    atomic_init(&e->tilt_y, 0);
    atomic_init(&e->turn, 0);
    atomic_init(&e->light, 0);
    for (int i = 0; i < 4; i++) atomic_init(&e->seen[i], 0);
    atomic_init(&e->rumble, false);
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

static int clamp(int v, int lo, int hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

void emu_set_motion(emu_t *e, int tilt_x, int tilt_y, int turn) {
    atomic_store(&e->tilt_x, clamp(tilt_x, -32767, 32767));
    atomic_store(&e->tilt_y, clamp(tilt_y, -32767, 32767));
    atomic_store(&e->turn, clamp(turn, -32767, 32767));
}

void emu_set_light(emu_t *e, int level) {
    atomic_store(&e->light, clamp(level, 0, 255));
}

uint32_t emu_cart_hardware(const emu_t *e) {
    return gba_cart_hardware(e->g);
}

bool emu_rumble_active(emu_t *e) {
    return atomic_load(&e->rumble);
}

void emu_motion_seen(emu_t *e, int32_t out[4]) {
    for (int i = 0; i < 4; i++) out[i] = atomic_load(&e->seen[i]);
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

(`apply_sensors` runs every frame: the core's `gba_set_tilt/gyro/light` only store values, so this is cheap and harmless for cartridges without them.)

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

JNIEXPORT void JNICALL JNI_FN(setMotion)(JNIEnv *env, jobject self, jint tilt_x, jint tilt_y, jint turn) {
    (void)env;
    (void)self;
    if (s_emu) emu_set_motion(s_emu, tilt_x, tilt_y, turn);
}

JNIEXPORT void JNICALL JNI_FN(setLight)(JNIEnv *env, jobject self, jint level) {
    (void)env;
    (void)self;
    if (s_emu) emu_set_light(s_emu, level);
}

JNIEXPORT jint JNICALL JNI_FN(cartHardware)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    return s_emu ? (jint)emu_cart_hardware(s_emu) : 0;
}

JNIEXPORT jboolean JNICALL JNI_FN(rumbleActive)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    return s_emu && emu_rumble_active(s_emu) ? JNI_TRUE : JNI_FALSE;
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

JNIEXPORT jintArray JNICALL JNI_FN(debugMotionSeen)(JNIEnv *env, jobject self) {
    (void)self;
    int32_t v[4] = {0, 0, 0, 0};
    if (s_emu) emu_motion_seen(s_emu, v);
    jintArray out = (*env)->NewIntArray(env, 4);
    (*env)->SetIntArrayRegion(env, out, 0, 4, (const jint *)v);
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

    /** Cartridge sensors, applied at the next frame: tilt x/y and turn ±32767 (turn > 0 = clockwise). */
    external fun setMotion(tiltX: Int, tiltY: Int, turn: Int)

    /** The solar sensor's light: 0 = dark .. 255 = full sun. */
    external fun setLight(level: Int)

    /** The loaded cartridge's extras: HW_* bits below. */
    external fun cartHardware(): Int

    /** True while the cartridge's rumble motor runs (as of the last frame). */
    external fun rumbleActive(): Boolean

    const val HW_RTC = 1
    const val HW_SOLAR = 2
    const val HW_GYRO = 4
    const val HW_RUMBLE = 8
    const val HW_TILT = 16

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

    /** The sensor values last applied to the core: [tilt x, tilt y, turn, light]. */
    external fun debugMotionSeen(): IntArray

    /** Buttons the game was given in any frame since the last call (KeyMap bits). */
    external fun debugKeysSeen(): Int

    /** Runs a ROM headless for n frames on this thread: "<frame hash> <audio hash>" like the PC test runner. */
    external fun debugRunFrames(rom: ByteBuffer, frames: Int): String?

    /** Draws one 240x160 BGR555 frame offscreen; RGBA pixels (r | g<<8 | b<<16 | a<<24), top row first. */
    external fun debugDrawFrame(pixels: ShortArray, width: Int, height: Int): IntArray?
}
```

- [x] **Step 5: Run the tests (three times)**

```bash
for i in 1 2 3; do rm -rf app/build/outputs/androidTest-results; bash tools/dev.sh androidtest 2>&1 | grep -E "^e: |Starting|BUILD FAILED"; grep -h -o 'tests="[0-9]*" failures="[0-9]*"' app/build/outputs/androidTest-results/connected/debug/*.xml; done
echo "C warnings: $(bash tools/dev.sh apk --rerun-tasks 2>&1 | grep -c 'warning:.*\.[ch]:')"
```

Expected: `tests="33" failures="0"` three times (29 + 4); `0` C warnings.

- [x] **Step 6: Commit**

```bash
git add app/src/androidTest/java/com/tengen/gbaemu/MotionTest.kt platform/android/emu_thread.h platform/android/emu_thread.c platform/android/jni_bridge.c app/src/main/java/com/tengen/gbaemu/NativeBridge.kt docs/PROGRESS.md docs/superpowers/plans/2026-09-27-milestone-4d-rumble-sensors.md
git commit -m "feat(android): tilt, turn and light from Kotlin to the core each frame; rumble state and cartridge extras back" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

**Batch 1 (Tasks 1-2) done — checkpoint with the user.**

---
### Task 3: The motion maths

**Files:**
- Create: `app/src/main/java/com/tengen/gbaemu/MotionMath.kt`, `app/src/test/java/com/tengen/gbaemu/MotionMathTest.kt`

- [x] **Step 1: Write the failing tests**

`app/src/test/java/com/tengen/gbaemu/MotionMathTest.kt`:

```kotlin
package com.tengen.gbaemu

import android.view.KeyEvent
import com.tengen.gbaemu.MotionMath.RumbleAction
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.PI
import kotlin.math.abs

class MotionMathTest {
    private val g = 9.80665f
    private val full = MotionMath.FULL

    @Test
    fun flatIsLevel() {
        assertEquals(0 to 0, MotionMath.tilt(0f, 0f, 0))
        assertEquals(0 to 0, MotionMath.tilt(0f, 0f, 1))
    }

    @Test
    fun uprightScreenEdgesGiveFullTilt() {
        // Right edge lowered: the accelerometer's x reads -g. Top edge lowered: y reads -g.
        assertEquals(full to 0, MotionMath.tilt(-g, 0f, 0))
        assertEquals(-full to 0, MotionMath.tilt(g, 0f, 0))
        assertEquals(0 to -full, MotionMath.tilt(0f, -g, 0))
        assertEquals(0 to full, MotionMath.tilt(0f, g, 0))
    }

    @Test
    fun sidewaysScreenSwapsTheAxes() {
        // ROTATION_90 (the device's top edge to the left): the screen's right edge is the device's bottom edge.
        assertEquals(full to 0, MotionMath.tilt(0f, g, 1))
        assertEquals(0 to -full, MotionMath.tilt(-g, 0f, 1))
        // ROTATION_270: the other way round.
        assertEquals(full to 0, MotionMath.tilt(0f, -g, 3))
        assertEquals(0 to -full, MotionMath.tilt(g, 0f, 3))
        // Upside down.
        assertEquals(full to 0, MotionMath.tilt(g, 0f, 2))
    }

    @Test
    fun beyondOneGIsClamped() {
        assertEquals(full to -full, MotionMath.tilt(-3 * g, -3 * g, 0))
    }

    @Test
    fun halfwayIsHalf() {
        val (x, _) = MotionMath.tilt(-g / 2, 0f, 0)
        assertTrue("$x", abs(x - full / 2) <= 1)
    }

    @Test
    fun turnIsClockwisePositiveAndTwoTurnsPerSecondIsFull() {
        assertEquals(full, MotionMath.turn(-(4 * PI).toFloat()))
        assertEquals(-full, MotionMath.turn((4 * PI).toFloat()))
        assertEquals(0, MotionMath.turn(0f))
        assertEquals(full, MotionMath.turn(-100f)) // clamped
    }

    @Test
    fun lightScaleEnds() {
        assertEquals(0, MotionMath.light(0f))
        assertEquals(0, MotionMath.light(10f))
        assertEquals(255, MotionMath.light(100_000f))
        assertEquals(255, MotionMath.light(1_000_000f))
        assertEquals(128, MotionMath.light(1_000f)) // 255 × 2/4 = 127.5
    }

    @Test
    fun aHeldKeyOverridesOnlyItsAxis() {
        assertEquals(1234, MotionMath.withKeys(1234, minusHeld = false, plusHeld = false, keyValue = 24000))
        assertEquals(24000, MotionMath.withKeys(1234, minusHeld = false, plusHeld = true, keyValue = 24000))
        assertEquals(-24000, MotionMath.withKeys(1234, minusHeld = true, plusHeld = false, keyValue = 24000))
        assertEquals(0, MotionMath.withKeys(1234, minusHeld = true, plusHeld = true, keyValue = 24000))
    }

    @Test
    fun motionKeysAreIJKLQE() {
        assertEquals(MotionMath.KEY_I, MotionMath.motionKey(KeyEvent.KEYCODE_I))
        assertEquals(MotionMath.KEY_J, MotionMath.motionKey(KeyEvent.KEYCODE_J))
        assertEquals(MotionMath.KEY_K, MotionMath.motionKey(KeyEvent.KEYCODE_K))
        assertEquals(MotionMath.KEY_L, MotionMath.motionKey(KeyEvent.KEYCODE_L))
        assertEquals(MotionMath.KEY_Q, MotionMath.motionKey(KeyEvent.KEYCODE_Q))
        assertEquals(MotionMath.KEY_E, MotionMath.motionKey(KeyEvent.KEYCODE_E))
        assertEquals(0, MotionMath.motionKey(KeyEvent.KEYCODE_X)) // a GBA button, not motion
        for (b in KeyMap.bindings) for (code in b.keyboard.keys) assertEquals(0, MotionMath.motionKey(code))
    }

    @Test
    fun rumbleActsOnlyOnChanges() {
        val s = MotionMath.RumbleSwitch()
        assertEquals(RumbleAction.NOTHING, s.next(false))
        assertEquals(RumbleAction.START, s.next(true))
        assertEquals(RumbleAction.NOTHING, s.next(true))
        assertEquals(RumbleAction.STOP, s.next(false))
        assertEquals(RumbleAction.NOTHING, s.next(false))
        s.next(true)
        s.reset()
        assertEquals(RumbleAction.START, s.next(true))
    }
}
```

- [x] **Step 2: Run to see them fail**

```bash
bash tools/dev.sh jvmtest
```

Expected: FAIL — `Unresolved reference 'MotionMath'`.

- [x] **Step 3: Write the code**

`app/src/main/java/com/tengen/gbaemu/MotionMath.kt`:

```kotlin
package com.tengen.gbaemu

import android.view.KeyEvent
import kotlin.math.PI
import kotlin.math.log10
import kotlin.math.roundToInt

/**
 * Phone sensors and keys → the values the cartridge extras take (pure maths, tested on the PC):
 * tilt x/y and turn ±32767, light 0..255. Also the rumble on/off decision.
 */
object MotionMath {
    const val FULL = 32767
    const val KEY_TILT = 24000 // I/J/K/L held (as in the PC viewer)
    const val KEY_TURN = 16000 // Q/E held
    private const val G = 9.80665f
    private const val FULL_TURN_RATE = (4 * PI).toFloat() // rad/s: two turns per second = full turn

    /**
     * Gravity from the accelerometer (m/s², the device's natural axes: x right, y up the screen) → tilt, for the screen
     * turned by [rotation] quarter turns (Surface.ROTATION_0..270). Lowering the screen's right edge gives +x, lowering
     * its top edge −y; flat = (0, 0); 1 g along an edge = full tilt — like the cartridge's sensor, which measures gravity.
     */
    fun tilt(ax: Float, ay: Float, rotation: Int): Pair<Int, Int> {
        val (sx, sy) = when (rotation and 3) {
            0 -> ax to ay
            1 -> -ay to ax
            2 -> -ax to -ay
            else -> ay to -ax
        }
        return clamp(-sx / G * FULL) to clamp(sy / G * FULL)
    }

    /** The gyroscope's z rate (rad/s, counter-clockwise +) → turn: clockwise +, two turns per second = full. */
    fun turn(wz: Float): Int = clamp(-wz / FULL_TURN_RATE * FULL)

    /** Lux → light: 0 at 10 lux or less (a dark room), 255 at 100,000 lux or more (direct sun), log scale between. */
    fun light(lux: Float): Int =
        if (lux <= 10f) 0 else (255 * log10(lux / 10f) / 4f).roundToInt().coerceIn(0, 255)

    /** One axis: a held key replaces the sensor (both held cancel out to 0). */
    fun withKeys(sensor: Int, minusHeld: Boolean, plusHeld: Boolean, keyValue: Int): Int =
        if (minusHeld || plusHeld) (if (plusHeld) keyValue else 0) - (if (minusHeld) keyValue else 0) else sensor

    // Motion keys (bits), as in the PC viewer: I/K tilt up/down (−y/+y), J/L tilt left/right (−x/+x), Q/E turn −/+.
    const val KEY_I = 1
    const val KEY_J = 2
    const val KEY_K = 4
    const val KEY_L = 8
    const val KEY_Q = 16
    const val KEY_E = 32

    fun motionKey(keyCode: Int): Int = when (keyCode) {
        KeyEvent.KEYCODE_I -> KEY_I
        KeyEvent.KEYCODE_J -> KEY_J
        KeyEvent.KEYCODE_K -> KEY_K
        KeyEvent.KEYCODE_L -> KEY_L
        KeyEvent.KEYCODE_Q -> KEY_Q
        KeyEvent.KEYCODE_E -> KEY_E
        else -> 0
    }

    private fun clamp(v: Float): Int = v.roundToInt().coerceIn(-FULL, FULL)

    /** Rumble: act only when the motor changes. */
    enum class RumbleAction { START, STOP, NOTHING }

    class RumbleSwitch {
        var on = false
            private set

        fun next(active: Boolean): RumbleAction = when {
            active && !on -> RumbleAction.START.also { on = true }
            !active && on -> RumbleAction.STOP.also { on = false }
            else -> RumbleAction.NOTHING
        }

        fun reset() {
            on = false
        }
    }
}
```

- [x] **Step 4: Run the tests**

```bash
bash tools/dev.sh jvmtest
grep -h -o 'testsuite name="[^"]*" tests="[0-9]*" skipped="[0-9]*" failures="[0-9]*"' app/build/test-results/testDebugUnitTest/*.xml
```

Expected: `MotionMathTest` 10 plus the earlier 14 — 24 tests, 0 failures.

- [x] **Step 5: Commit**

```bash
git add app/src/main/java/com/tengen/gbaemu/MotionMath.kt app/src/test/java/com/tengen/gbaemu/MotionMathTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-09-27-milestone-4d-rumble-sensors.md
git commit -m "feat(android): motion maths - tilt from gravity, turn from the gyro, light from lux, key overrides, rumble switch" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 4: Sensors, rumble and the ☀ slider in the app

**Files:**
- Create: `app/src/main/java/com/tengen/gbaemu/MotionInput.kt`, `app/src/main/java/com/tengen/gbaemu/Rumble.kt`
- Modify: `app/src/main/java/com/tengen/gbaemu/Screens.kt`, `app/src/main/java/com/tengen/gbaemu/MainActivity.kt`, `app/src/main/AndroidManifest.xml`

(No new automated test: the parts are covered by Tasks 2-3; the whole is checked by Claude on the emulator in Step 3 with its virtual sensors and vibrator log, and by the user in Task 6.)

- [x] **Step 1: Write the code**

`app/src/main/java/com/tengen/gbaemu/MotionInput.kt`:

```kotlin
package com.tengen.gbaemu

import android.app.Activity
import android.content.pm.ApplicationInfo
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.os.SystemClock
import android.util.Log
import androidx.core.content.ContextCompat

/**
 * The cartridge's sensors from the phone: the accelerometer (tilt), the gyroscope (turn) and the light sensor (solar),
 * each only when the cartridge has it ([hardware] = NativeBridge.HW_* bits); the I/J/K/L and Q/E keys override their
 * axis while held; [manualLight] (the ☀ slider) overrides the light sensor. Every change goes to the core at once.
 */
class MotionInput(private val activity: Activity, private val hardware: Int, private val onMissing: (String) -> Unit) :
    SensorEventListener {
    private val sensors = activity.getSystemService(SensorManager::class.java)
    private val debug = (activity.applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE) != 0
    private var ax = 0f
    private var ay = 0f
    private var wz = 0f
    private var lux = 0f
    private var keys = 0
    private var missingTold = false
    private var lastLog = ""
    private var lastLogMs = 0L
    private var started = false

    /** null = Auto (the light sensor); 0..255 = set by hand. */
    var manualLight: Int? = null
        set(value) {
            field = value
            push()
        }

    fun start() {
        if (started) return
        started = true
        val wanted = buildList {
            if (hardware and NativeBridge.HW_TILT != 0) add(Sensor.TYPE_ACCELEROMETER to "motion (tilt)")
            if (hardware and NativeBridge.HW_GYRO != 0) add(Sensor.TYPE_GYROSCOPE to "gyroscope (turn)")
            if (hardware and NativeBridge.HW_SOLAR != 0) add(Sensor.TYPE_LIGHT to "light")
        }
        val missing = mutableListOf<String>()
        for ((type, name) in wanted) {
            val s = sensors?.getDefaultSensor(type)
            if (s == null) missing += name else sensors.registerListener(this, s, SensorManager.SENSOR_DELAY_GAME)
        }
        if (missing.isNotEmpty() && !missingTold) {
            missingTold = true
            onMissing("This phone has no ${missing.joinToString(" or ")} sensor: use the keys or the ☀ slider.")
        }
        push()
    }

    fun stop() {
        if (!started) return
        started = false
        sensors?.unregisterListener(this)
    }

    /** A motion key (MotionMath.KEY_*) went down or up. */
    fun setKey(bit: Int, down: Boolean) {
        keys = if (down) keys or bit else keys and bit.inv()
        push()
    }

    override fun onSensorChanged(event: SensorEvent) {
        when (event.sensor.type) {
            Sensor.TYPE_ACCELEROMETER -> {
                ax = event.values[0]
                ay = event.values[1]
            }
            Sensor.TYPE_GYROSCOPE -> wz = event.values[2]
            Sensor.TYPE_LIGHT -> lux = event.values[0]
        }
        push()
    }

    override fun onAccuracyChanged(sensor: Sensor, accuracy: Int) {}

    private fun push() {
        val rotation = ContextCompat.getDisplayOrDefault(activity).rotation
        val (sx, sy) = MotionMath.tilt(ax, ay, rotation)
        val x = MotionMath.withKeys(sx, keys and MotionMath.KEY_J != 0, keys and MotionMath.KEY_L != 0, MotionMath.KEY_TILT)
        val y = MotionMath.withKeys(sy, keys and MotionMath.KEY_I != 0, keys and MotionMath.KEY_K != 0, MotionMath.KEY_TILT)
        val turn = MotionMath.withKeys(MotionMath.turn(wz), keys and MotionMath.KEY_Q != 0, keys and MotionMath.KEY_E != 0,
            MotionMath.KEY_TURN)
        val light = manualLight ?: MotionMath.light(lux)
        NativeBridge.setMotion(x, y, turn)
        NativeBridge.setLight(light)
        if (debug) { // for checking with the phone emulator's virtual sensors: at most once a second, on changes
            val line = "motion: tilt $x $y turn $turn light $light${if (manualLight != null) " (manual)" else ""}"
            val now = SystemClock.elapsedRealtime()
            if (line != lastLog && now - lastLogMs >= 1000) {
                Log.i("gbaemu", line)
                lastLog = line
                lastLogMs = now
            }
        }
    }
}
```

`app/src/main/java/com/tengen/gbaemu/Rumble.kt`:

```kotlin
package com.tengen.gbaemu

import android.content.Context
import android.os.Build
import android.os.SystemClock
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager
import android.view.Choreographer

/** The cartridge's rumble motor → the phone's vibrator, checked once per display frame while running. */
class Rumble(context: Context) : Choreographer.FrameCallback {
    private val vibrator: Vibrator? = if (Build.VERSION.SDK_INT >= 31) {
        context.getSystemService(VibratorManager::class.java)?.defaultVibrator
    } else {
        @Suppress("DEPRECATION")
        context.getSystemService(Vibrator::class.java)
    }
    private val switch = MotionMath.RumbleSwitch()
    private var running = false
    private var startedMs = 0L

    fun start() {
        if (running || vibrator?.hasVibrator() != true) return
        running = true
        Choreographer.getInstance().postFrameCallback(this)
    }

    /** Pause, quit, the app in the background: the motor stops with the game. */
    fun stop() {
        if (!running) return
        running = false
        Choreographer.getInstance().removeFrameCallback(this)
        if (switch.on) vibrator?.cancel()
        switch.reset()
    }

    override fun doFrame(frameTimeNanos: Long) {
        if (!running) return
        when (switch.next(NativeBridge.rumbleActive())) {
            MotionMath.RumbleAction.START -> buzz()
            MotionMath.RumbleAction.STOP -> vibrator?.cancel()
            MotionMath.RumbleAction.NOTHING -> if (switch.on && SystemClock.elapsedRealtime() - startedMs > LONG_MS - 1000) buzz()
        }
        Choreographer.getInstance().postFrameCallback(this)
    }

    /** A long buzz, renewed while the motor keeps running, cancelled when it stops. */
    private fun buzz() {
        vibrator?.vibrate(VibrationEffect.createOneShot(LONG_MS, VibrationEffect.DEFAULT_AMPLITUDE))
        startedMs = SystemClock.elapsedRealtime()
    }

    private companion object {
        const val LONG_MS = 5000L
    }
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
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Slider
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlin.math.roundToInt
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

/**
 * The game (a SurfaceView handed to the native render thread) and the touch buttons, in layout A; for a solar
 * cartridge also the ☀ button with the light slider ([manualLight] null = Auto, the phone's light sensor).
 */
@Composable
fun GameScreen(controlsHidden: Boolean, onTouchMask: (Int) -> Unit, onShowControls: () -> Unit,
               onDrawFailed: () -> Unit, solar: Boolean = false, manualLight: Int? = null,
               onManualLight: (Int?) -> Unit = {}) {
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
        if (solar) SunControl(manualLight, onManualLight, Modifier.align(Alignment.BottomStart))
    }
}

/** ☀: opens a light slider (moving it sets the level by hand) with an Auto switch (the phone's light sensor). */
@Composable
private fun SunControl(level: Int?, onLevel: (Int?) -> Unit, modifier: Modifier) {
    var open by remember { mutableStateOf(false) }
    Column(modifier.padding(8.dp)) {
        if (open) {
            Surface(shape = RoundedCornerShape(12.dp), color = Color(0xE0202020)) {
                Column(Modifier.padding(12.dp).width(260.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text("Light: ${level ?: "sensor"}", color = Color.White, modifier = Modifier.weight(1f))
                        Text("Auto ", color = Color.White)
                        Switch(checked = level == null, onCheckedChange = { onLevel(if (it) null else 128) })
                    }
                    Slider(value = (level ?: 128).toFloat(), onValueChange = { onLevel(it.roundToInt()) },
                        valueRange = 0f..255f)
                }
            }
            Spacer(Modifier.height(4.dp))
        }
        TextButton(onClick = { open = !open }) { Text("☀", fontSize = 28.sp, color = Color(0xFFFFD54F)) }
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
    private var manualLight by mutableStateOf<Int?>(null)
    private var hardware = 0 // the cartridge's extras (NativeBridge.HW_*)
    private var motion: MotionInput? = null
    private var rumble: Rumble? = null
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
                        solar = hardware and NativeBridge.HW_SOLAR != 0,
                        manualLight = manualLight,
                        onManualLight = {
                            manualLight = it
                            motion?.manualLight = it
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
                        onKeepPlaying = { askQuit = false; NativeBridge.resume(); startExtras() })
                }
                BackHandler(enabled = playing && !askQuit) {
                    stopExtras()
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
        hardware = NativeBridge.cartHardware()
        manualLight = null
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
        if (hardware and (NativeBridge.HW_TILT or NativeBridge.HW_GYRO or NativeBridge.HW_SOLAR) != 0) {
            motion = MotionInput(this, hardware) { toast(it) }
        }
        if (hardware and NativeBridge.HW_RUMBLE != 0) rumble = Rumble(this)
        NativeBridge.start()
        playing = true
        setFullScreen(true)
        startExtras()
    }

    /** The cartridge's sensors and rumble run only while the game does. */
    private fun startExtras() {
        motion?.start()
        rumble?.start()
    }

    private fun stopExtras() {
        motion?.stop()
        rumble?.stop()
    }

    private fun stopGame() {
        stopExtras()
        motion = null
        rumble = null
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
        val motionKey = MotionMath.motionKey(event.keyCode)
        val m = motion
        if (playing && motionKey != 0 && m != null) { // I/J/K/L tilt, Q/E turn
            when (event.action) {
                KeyEvent.ACTION_DOWN -> m.setKey(motionKey, true)
                KeyEvent.ACTION_UP -> m.setKey(motionKey, false)
            }
            controlsHidden = true
            return true
        }
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
        stopExtras()
        NativeBridge.pause() // returns once the save is written
    }

    override fun onResume() {
        super.onResume()
        if (!askQuit) {
            NativeBridge.resume()
            if (playing) startExtras()
        }
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

    <!-- Rumble cartridges (game D, game H) vibrate the phone; granted without asking. -->
    <uses-permission android:name="android.permission.VIBRATE" />

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

- [x] **Step 2: Build and run every test**

```bash
bash tools/dev.sh jvmtest && bash tools/dev.sh unit
rm -rf app/build/outputs/androidTest-results; bash tools/dev.sh androidtest 2>&1 | grep -E "^e: |Starting|BUILD FAILED"; grep -h -o 'tests="[0-9]*" failures="[0-9]*"' app/build/outputs/androidTest-results/connected/debug/*.xml
echo "C warnings: $(bash tools/dev.sh apk --rerun-tasks 2>&1 | grep -c 'warning:.*\.[ch]:')"
```

Expected: Kotlin 24/24, `363 tests, 0 failed`, on-device `tests="33" failures="0"`, `0` C warnings.

- [x] **Step 3: Claude's check on the emulator (adb)**

Use a scratchpad folder `$SP` for screenshots. Helpers:

```bash
ADB_EXE="$(cygpath -u "$LOCALAPPDATA")/Android/Sdk/platform-tools/adb"
EMU=$(MSYS_NO_PATHCONV=1 "$ADB_EXE" devices | awk '$1 ~ /^emulator-/ && $2 == "device" { print $1; exit }')
ADB() {  # the emulator only: refuse when none is running (never fall back to a USB phone)
    [ -n "$EMU" ] || { echo "ADB: no emulator running - refusing" >&2; return 1; }
    MSYS_NO_PATHCONV=1 "$ADB_EXE" -s "$EMU" "$@"
}
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
pick_download() {  # in the open file picker: show Downloads and pick file $1 (the side menu animates: wait for it)
    for try in 1 2 3; do
        if ADB shell uiautomator dump /sdcard/ui.xml >/dev/null && ADB shell cat /sdcard/ui.xml | grep -qF "text=\"$1\""; then
            tap_on text "$1"
            return
        fi
        tap_on content-desc "Show roots" && sleep 3 && tap_on text "Downloads" && sleep 3
    done
    echo "could not pick $1 from Downloads" >&2
    return 1
}
texts() { ADB shell uiautomator dump /sdcard/ui.xml >/dev/null; ADB shell cat /sdcard/ui.xml | tr '>' '\n' | grep -oE 'text="[^"]+"'; }
open_rom() {  # Open ROM -> the file -> Start fresh (the test cartridges never save, so the question comes every time)
    ADB shell am force-stop com.google.android.documentsui
    tap_on text "Open ROM" && sleep 2 && pick_download "$1" && sleep 2 && tap_on text "Start fresh" && sleep 2
}
quit_game() { ADB shell input keyevent KEYCODE_BACK; sleep 1; tap_on text "Quit"; sleep 1; }
motion_log() { ADB logcat -d -s gbaemu:I | grep 'motion:' | sed 's/.*gbaemu  : //'; }
rumbles() { ADB shell dumpsys vibrator_manager | grep 'com.tengen.gbaemu' | grep -vc 'usage: TOUCH'; }
```

Then:

```bash
for r in hw_gyro hw_tilt hw_solar; do bash tools/dev.sh push tests/own_roms/$r.gba; done
ADB shell settings put system accelerometer_rotation 0; ADB shell settings put system user_rotation 0   # fake gravity must not turn the screen
ADB shell input keyevent KEYCODE_HOME; ADB shell am force-stop com.tengen.gbaemu
bash tools/dev.sh install && bash tools/dev.sh launch && sleep 3
open_rom hw_tilt.gba; ADB logcat -c
for a in 0:0:9.8 -4.9:0:8.5 0:-4.9:8.5; do ADB emu sensor set acceleration $a >/dev/null; sleep 1.3; done
ADB shell input keyevent --longpress KEYCODE_L; sleep 1.3; motion_log; quit_game
open_rom hw_gyro.gba; ADB logcat -c; before=$(rumbles)
for w in 0:0:-6.28 0:0:3.14 0:0:0; do ADB emu sensor set gyroscope $w >/dev/null; sleep 1.3; done
motion_log; echo "rumble vibrations while playing: $(( $(rumbles) - before ))"
ADB shell input keyevent KEYCODE_BACK; sleep 1; n=$(rumbles); sleep 3; echo "during the Back question: $(( $(rumbles) - n ))"; tap_on text "Quit"; sleep 1
open_rom hw_solar.gba; ADB logcat -c
for l in 1000 100000 5; do ADB emu sensor set light $l >/dev/null; sleep 1.3; done
tap_on text "☀"; sleep 1; shot sun; texts | grep -E 'Light|Auto'
ADB shell input tap 678 2163; sleep 1.3; texts | grep Light   # the slider's right part (Pixel 9, upright)
ADB shell input tap 666 2039; sleep 1.3; texts | grep Light   # the Auto switch
motion_log; quit_game
ADB logcat -d -s AndroidRuntime:E | tail -3
```

Expected:
- `hw_tilt`: `motion:` lines `tilt 0 0 …`, `tilt 16372 0 …` (right edge half down), `tilt 0 -16372 …` (top edge half down), and `tilt 24000 …` while L is held (±1 on the numbers).
- `hw_gyro`: `turn 16375` (one clockwise turn a second), `turn -8188`, `turn 0`; **rumble vibrations while playing: 3 or more** (0.5 s buzzes every second); **during the Back question: 0**.
- `hw_solar`: `light 128`, `light 255`, `light 0`; `sun` shows the panel ('Light: sensor', Auto on) above the ☀ button at the bottom left; the slider tap → 'Light: 2xx' and a `light 2xx (manual)` line; Auto → 'Light: sensor'. No `AndroidRuntime` crash.

- [x] **Step 4: Commit**

```bash
git add app/src/main/java/com/tengen/gbaemu/MotionInput.kt app/src/main/java/com/tengen/gbaemu/Rumble.kt app/src/main/java/com/tengen/gbaemu/Screens.kt app/src/main/java/com/tengen/gbaemu/MainActivity.kt app/src/main/AndroidManifest.xml docs/PROGRESS.md docs/superpowers/plans/2026-09-27-milestone-4d-rumble-sensors.md
git commit -m "feat(android): phone sensors for tilt, turn and light, the sun slider, and rumble on the vibrator" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

**Batch 2 (Tasks 3-4) done — checkpoint with the user.**

---
### Task 5: Commands for the user's real phone

**Files:**
- Modify: `tools/dev.sh`

- [x] **Step 1: Write the code**

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
    (cd "$ROOT" && JAVA_HOME="$(cygpath -m "$JBR")" ./gradlew --offline "$@")
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
    phone-push) s=$(phone_serial) && echo "copying to $(phone_name "$s")" && adb -s "$s" push "$(cygpath -w "$1")" /sdcard/Download/ ;;
    *) echo "usage: tools/dev.sh configure|build|unit [filter]|test|run <args>|view <args>|release|apk|jvmtest|emu|install|launch|androidtest|push <file>|phone-install|phone-uninstall|phone-push <file>" >&2; exit 2 ;;
esac
```

- [x] **Step 2: Check** (with the user's phone **unplugged or not asked for**, only the refusal is tried here)

```bash
bash -n tools/dev.sh && echo "syntax ok"
"$LOCALAPPDATA/Android/Sdk/platform-tools/adb.exe" devices
bash tools/dev.sh phone-install; echo "exit $?"
bash tools/dev.sh bogus; echo "exit $?"
```

Expected: `syntax ok`. With only the emulator attached: `need exactly one USB phone with USB debugging on; found 0`, `exit 1`. (If the user's phone is attached, do **not** run `phone-install` here — Task 6 does it with their OK.) `bogus` → the usage line (now listing `phone-install|phone-uninstall|phone-push <file>`), `exit 2`.

- [x] **Step 3: Commit**

```bash
git add tools/dev.sh docs/PROGRESS.md docs/superpowers/plans/2026-09-27-milestone-4d-rumble-sensors.md
git commit -m "tools: dev.sh phone-install/-uninstall/-push for the user's real phone (exactly one USB phone, only on request)" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 6: User check and milestone 4d exit

- [x] **Step 1: Full run**

```bash
bash tools/dev.sh test && bash tools/dev.sh unit && bash tools/dev.sh jvmtest && bash tools/dev.sh androidtest
```

Expected: CTest 56/56 (sanitizers on, no reports), 363 unit tests, Kotlin 24, on-device 33 (emulator only).

- [x] **Step 2: The user's check** (their `game-d.gba` from their Downloads folder — push with `bash tools/dev.sh push <file>`)

On the emulator (app installed and open): game D → Start fresh (or import their save) → microgames; Q/E turn. Then **with the user's explicit OK and their phone plugged in**: `bash tools/dev.sh phone-install` and `bash tools/dev.sh phone-push "<the ROM>"` → on the phone: turning the phone turns the game, the rumble is felt, speed and sound fine.

- [x] **Step 3: Docs**

`docs/PROGRESS.md` (4d ✅ with the numbers and the user's check — **milestone 4 complete**; Next = milestone 5 brainstorming: save states, rewind, fast-forward, cheats); spec status → `Implemented — milestone 4d complete <date> (tag milestone-4d)`; parent spec §10 row 4 → ✅; `CLAUDE.md` key files → milestone 5 needs brainstorming; tick every box here.

- [x] **Step 4: Commit, tag, merge** — ask the user how to integrate (so far always a fast-forward merge into `main`); then superpowers:finishing-a-development-branch, `bash tools/dev.sh test` and `bash tools/dev.sh androidtest` on `main`, delete the branch, record the merge in PROGRESS.md; delete the prototype and replay worktrees (`rm -rf` the folders in Git Bash, `git worktree prune`, `git branch -D proto-4d replay-4d`) with the user's OK.

```bash
git add -A docs CLAUDE.md
git commit -m "docs: milestone 4d complete - rumble and motion/light sensors; milestone 4 complete" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git tag milestone-4d
```
