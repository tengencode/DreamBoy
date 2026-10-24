# Milestone 4b — Sound — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. (This project always executes plans inline with superpowers:executing-plans — see `CLAUDE.md`.)

**Goal:** Games play with sound on the Pixel 9 phone emulator — AAudio output, 32,768 Hz → the phone's rate with cubic interpolation, and the sound setting the game's pace (clock pacing only without sound).

**Architecture:** Three plain-C pieces tested on the PC (`rate_control`, `audio_ring` — a lock-free SPSC ring — and `resampler`), one AAudio wrapper (`audio_out`, callback copies from the ring, silence when short), and the 4a emulation thread extended: each frame's samples are converted and pushed; the thread waits (1 ms naps) while the ring holds more than 30 ms; a device change reopens the stream; a watchdog reopens a stuck stream once and otherwise falls back to 4a's clock pacer.

**Tech Stack:** C11, AAudio (NDK, `libaaudio`), Kotlin test hooks, JUnit 4 + androidx.test.

**Spec:** `docs/superpowers/specs/2026-09-26-milestone-4b-sound-design.md` (§8: the prototype this plan's code comes from)

---

## Conventions (same as 4a)

- Git Bash from the repo root. PC: `bash tools/dev.sh <configure|unit [filter]|test>`. Android: `bash tools/dev.sh <apk|jvmtest|emu|install|launch|androidtest|push <file>>` — Gradle `--offline` with Android Studio's JBR; a missing artifact → stop and ask the user before any download.
- **Only the phone emulator, never a USB phone:** `dev.sh` exports `ANDROID_SERIAL=<emulator>`; never run `./gradlew connected…`/`installDebug` or `adb` without it (the user's phone may be plugged in). The emulator window stays open between tasks; if it has been closed, `dev.sh` starts it again.
- The on-device tests read the test ROMs from `tests/roms/` and `tests/own_roms/` (packed into the test APK by `copyTestRoms`).
- Unit tests: `TEST(name)` in `tests/unit/*.c`; re-run `bash tools/dev.sh configure` after adding a test file. Test names must not equal any function name.
- One commit per task; messages end with `-m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"`. Each commit also includes `docs/PROGRESS.md` and this plan (ticked boxes) — project rule in `CLAUDE.md`.
- Branch `milestone-4b` (already created; the spec commits are on it).
- Checkpoints with the user after Tasks 2 and 4; Task 6 is the user's listening check and the milestone exit. (Task 5 was added during the listening check, 2026-09-26: the phone emulator's window sends letter keys as 0.1 ms taps, which the game never saw.)

## File map

```
platform/android/rate_control.{h,c}, audio_ring.{h,c}   Task 1 (plain C; tests/unit/test_audio.c)
platform/android/resampler.{h,c}                         Task 2 (plain C; tests/unit/test_resampler.c)
platform/android/audio_out.{h,c}                         Task 3 (AAudio)
platform/android/emu_thread.{h,c}, jni_bridge.c          Task 3 (sound + pacing), Task 4 (reopen + watchdog)
platform/android/CMakeLists.txt                          Task 3 (+ aaudio)
app/src/main/java/com/tengen/gbaemu/NativeBridge.kt      Tasks 3-4 (test hooks)
app/src/androidTest/java/com/tengen/gbaemu/AudioTest.kt  Tasks 3-4
platform/android/key_latch.{h,c}                         Task 5 (plain C; tests/unit/test_keys.c)
app/src/androidTest/java/com/tengen/gbaemu/KeyTest.kt    Task 5
tests/unit/CMakeLists.txt                                Tasks 1-2, 5
```

---
### Task 1: Rate control and the sound buffer

**Files:**
- Create: `tests/unit/test_audio.c`, `platform/android/rate_control.h`, `platform/android/rate_control.c`, `platform/android/audio_ring.h`, `platform/android/audio_ring.c`
- Modify: `tests/unit/CMakeLists.txt`

- [x] **Step 1: Write the failing tests**

`tests/unit/test_audio.c`:

```c
/* The Android sound path's plain-C pieces (platform/android): rate control and the sound buffer. */
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "audio_ring.h"
#include "rate_control.h"
#include "test.h"

#define BASE_48K (48000.0 / 32768.0)

static int near(double a, double b, double eps) {
    return fabs(a - b) <= eps;
}

/* ---- rate control ---- */

TEST(rate_control_half_full_gives_the_exact_ratio) {
    CHECK(near(rate_control_ratio(48000, 0.5), BASE_48K, 1e-12));
    CHECK(near(rate_control_ratio(44100, 0.5), 44100.0 / 32768.0, 1e-12));
}

TEST(rate_control_empty_and_full_move_half_a_percent) {
    CHECK(near(rate_control_ratio(48000, 0.0), BASE_48K * 1.005, 1e-12));
    CHECK(near(rate_control_ratio(48000, 1.0), BASE_48K * 0.995, 1e-12));
    CHECK(rate_control_ratio(48000, 0.25) > BASE_48K);
    CHECK(rate_control_ratio(48000, 0.75) < BASE_48K);
}

TEST(rate_control_clamps_fill_to_0_and_1) {
    CHECK(near(rate_control_ratio(48000, -3.0), rate_control_ratio(48000, 0.0), 1e-15));
    CHECK(near(rate_control_ratio(48000, 7.0), rate_control_ratio(48000, 1.0), 1e-15));
}

/* ---- sound buffer ---- */

static audio_ring_t *ring_new(void) {
    audio_ring_t *r = malloc(sizeof *r);
    audio_ring_init(r);
    return r;
}

/* n frames numbered from `first`: left = number, right = -number. */
static void numbered(int16_t *f, size_t n, int first) {
    for (size_t i = 0; i < n; i++) {
        f[i * 2] = (int16_t)(first + (int)i);
        f[i * 2 + 1] = (int16_t)-(first + (int)i);
    }
}

TEST(audio_ring_pushes_pops_and_counts) {
    audio_ring_t *r = ring_new();
    int16_t in[100 * 2], out[100 * 2];
    numbered(in, 100, 1);
    CHECK_EQ(audio_ring_push(r, in, 100), 100);
    CHECK_EQ(audio_ring_queued(r), 100);
    CHECK_EQ(audio_ring_pop(r, out, 60), 60);
    CHECK_EQ(audio_ring_queued(r), 40);
    CHECK_EQ(out[0], 1);
    CHECK(out[1] == -1);
    CHECK_EQ(out[59 * 2], 60);
    CHECK_EQ(audio_ring_pop(r, out, 100), 40); /* only what is there */
    CHECK_EQ(out[0], 61);
    free(r);
}

TEST(audio_ring_wraps_around_the_end) {
    audio_ring_t *r = ring_new();
    static int16_t in[6000 * 2], out[6000 * 2];
    numbered(in, 6000, 0);
    audio_ring_push(r, in, 6000);
    audio_ring_pop(r, out, 6000);
    numbered(in, 6000, 10000); /* 6000..11999 in the ring: crosses the end at 8192 */
    CHECK_EQ(audio_ring_push(r, in, 6000), 6000);
    CHECK_EQ(audio_ring_pop(r, out, 6000), 6000);
    CHECK(memcmp(in, out, sizeof in) == 0);
    free(r);
}

TEST(audio_ring_full_drops_the_extra_and_keeps_the_oldest) {
    audio_ring_t *r = ring_new();
    static int16_t in[9000 * 2], out[9000 * 2];
    numbered(in, 9000, 0);
    CHECK_EQ(audio_ring_push(r, in, 9000), AUDIO_RING_FRAMES);
    CHECK_EQ(audio_ring_push(r, in, 10), 0);
    CHECK_EQ(audio_ring_pop(r, out, 9000), AUDIO_RING_FRAMES);
    CHECK(memcmp(in, out, AUDIO_RING_FRAMES * 4) == 0);
    free(r);
}

TEST(audio_ring_clear_empties_it) {
    audio_ring_t *r = ring_new();
    int16_t in[50 * 2];
    numbered(in, 50, 0);
    audio_ring_push(r, in, 50);
    audio_ring_clear(r);
    CHECK_EQ(audio_ring_queued(r), 0);
    free(r);
}

#define THREAD_FRAMES 1000000

static void *ring_producer(void *arg) {
    audio_ring_t *r = arg;
    int16_t chunk[700 * 2];
    int next = 0;
    size_t size = 1;
    while (next < THREAD_FRAMES) {
        size_t n = size < (size_t)(THREAD_FRAMES - next) ? size : (size_t)(THREAD_FRAMES - next);
        for (size_t i = 0; i < n; i++) {
            chunk[i * 2] = (int16_t)(next + (int)i);         /* low 16 bits of the frame number */
            chunk[i * 2 + 1] = (int16_t)((next + (int)i) >> 16); /* high bits */
        }
        next += (int)audio_ring_push(r, chunk, n);
        size = size % 660 + 37; /* varying chunk sizes, at most 696 */
    }
    return NULL;
}

TEST(audio_ring_threads_lose_repeat_or_reorder_nothing) {
    audio_ring_t *r = ring_new();
    pthread_t t;
    pthread_create(&t, NULL, ring_producer, r);
    int16_t chunk[512 * 2];
    int expect = 0;
    bool ok = true;
    while (expect < THREAD_FRAMES) {
        size_t n = audio_ring_pop(r, chunk, 512);
        for (size_t i = 0; i < n; i++) {
            int got = (uint16_t)chunk[i * 2] | ((int)(uint16_t)chunk[i * 2 + 1] << 16);
            ok &= got == expect;
            expect++;
        }
    }
    pthread_join(t, NULL);
    CHECK(ok);
    CHECK_EQ(audio_ring_queued(r), 0);
    free(r);
}
```

Append to `tests/unit/CMakeLists.txt`:

```cmake
target_sources(unit_tests PRIVATE ${PLATFORM_DIR}/audio_ring.c ${PLATFORM_DIR}/rate_control.c)
```

- [x] **Step 2: Run to see them fail**

```bash
bash tools/dev.sh configure && bash tools/dev.sh unit
```

Expected: CMake configure fails — `Cannot find source file` (`audio_ring.c`, `rate_control.c`).

- [x] **Step 3: Write the code**

`platform/android/rate_control.h`:

```c
#ifndef GBAEMU_RATE_CONTROL_H
#define GBAEMU_RATE_CONTROL_H

#define RATE_CONTROL_MAX_ADJUST 0.005 /* at most ±0.5 % away from the exact ratio */

/* Output frames per GBA frame (32,768 Hz input) for a stream at `rate` Hz, given how full the sound
 * buffer is: fill = queued / (2 × target), clamped to [0, 1]. Half full → the exact ratio; emptier →
 * slightly more output, fuller → slightly less. */
double rate_control_ratio(int rate, double fill);

#endif
```

`platform/android/rate_control.c`:

```c
#include "rate_control.h"

double rate_control_ratio(int rate, double fill) {
    if (fill < 0.0) fill = 0.0;
    if (fill > 1.0) fill = 1.0;
    return (rate / 32768.0) * (1.0 + RATE_CONTROL_MAX_ADJUST * (0.5 - fill) / 0.5);
}
```

`platform/android/audio_ring.h`:

```c
#ifndef GBAEMU_AUDIO_RING_H
#define GBAEMU_AUDIO_RING_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#define AUDIO_RING_FRAMES 8192u /* stereo frames; a power of two */

/* Stereo int16 frames from one producer (the emulation thread) to one consumer (AAudio's callback),
 * without locks. The counters only grow (wrapping at 2^32); their difference is the fill. */
typedef struct {
    int16_t data[AUDIO_RING_FRAMES * 2]; /* interleaved left, right */
    atomic_uint write;                   /* frames ever pushed */
    atomic_uint read;                    /* frames ever popped */
} audio_ring_t;

void audio_ring_init(audio_ring_t *r);
/* Producer: stores up to n frames; returns how many fitted (the rest is dropped, nothing is overwritten). */
size_t audio_ring_push(audio_ring_t *r, const int16_t *frames, size_t n);
/* Consumer: copies up to n frames out; returns how many there were. */
size_t audio_ring_pop(audio_ring_t *r, int16_t *out, size_t n);
size_t audio_ring_queued(audio_ring_t *r);
/* Empties the ring. Only while the consumer is stopped (the stream is paused or closed). */
void audio_ring_clear(audio_ring_t *r);

#endif
```

`platform/android/audio_ring.c`:

```c
#include "audio_ring.h"

#include <string.h>

#define MASK (AUDIO_RING_FRAMES - 1u)

void audio_ring_init(audio_ring_t *r) {
    memset(r->data, 0, sizeof r->data);
    atomic_init(&r->write, 0u);
    atomic_init(&r->read, 0u);
}

size_t audio_ring_push(audio_ring_t *r, const int16_t *frames, size_t n) {
    unsigned w = atomic_load_explicit(&r->write, memory_order_relaxed);
    unsigned rd = atomic_load_explicit(&r->read, memory_order_acquire);
    size_t room = AUDIO_RING_FRAMES - (size_t)(w - rd);
    if (n > room) n = room;
    for (size_t i = 0; i < n; i++) {
        unsigned slot = (w + (unsigned)i) & MASK;
        r->data[slot * 2] = frames[i * 2];
        r->data[slot * 2 + 1] = frames[i * 2 + 1];
    }
    atomic_store_explicit(&r->write, w + (unsigned)n, memory_order_release);
    return n;
}

size_t audio_ring_pop(audio_ring_t *r, int16_t *out, size_t n) {
    unsigned rd = atomic_load_explicit(&r->read, memory_order_relaxed);
    unsigned w = atomic_load_explicit(&r->write, memory_order_acquire);
    size_t have = (size_t)(w - rd);
    if (n > have) n = have;
    for (size_t i = 0; i < n; i++) {
        unsigned slot = (rd + (unsigned)i) & MASK;
        out[i * 2] = r->data[slot * 2];
        out[i * 2 + 1] = r->data[slot * 2 + 1];
    }
    atomic_store_explicit(&r->read, rd + (unsigned)n, memory_order_release);
    return n;
}

size_t audio_ring_queued(audio_ring_t *r) {
    unsigned w = atomic_load_explicit(&r->write, memory_order_acquire);
    unsigned rd = atomic_load_explicit(&r->read, memory_order_acquire);
    return (size_t)(w - rd);
}

void audio_ring_clear(audio_ring_t *r) {
    atomic_store_explicit(&r->read, atomic_load_explicit(&r->write, memory_order_acquire), memory_order_release);
}
```

- [x] **Step 4: Run the tests**

```bash
bash tools/dev.sh configure && bash tools/dev.sh unit
bash tools/dev.sh test
```

Expected: `344 tests, 0 failed` (336 + 8: rate control 3, sound buffer 5 — the two-thread test moves a million frames); CTest 56/56.

- [x] **Step 5: Commit**

```bash
git add tests/unit/test_audio.c tests/unit/CMakeLists.txt platform/android/rate_control.h platform/android/rate_control.c platform/android/audio_ring.h platform/android/audio_ring.c docs/PROGRESS.md docs/superpowers/plans/2026-09-26-milestone-4b-sound.md
git commit -m "feat(android): sound rate control and lock-free sound buffer" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 2: The resampler

**Files:**
- Create: `tests/unit/test_resampler.c`, `platform/android/resampler.h`, `platform/android/resampler.c`
- Modify: `tests/unit/CMakeLists.txt`

- [x] **Step 1: Write the failing tests**

`tests/unit/test_resampler.c`:

```c
/* The Android sound path's resampler (platform/android/resampler.c): 4-point cubic Hermite. */
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "resampler.h"
#include "test.h"

#define BASE_48K (48000.0 / 32768.0)
#define PI 3.14159265358979323846 /* M_PI is not standard C */

static int near(double a, double b, double eps) {
    return fabs(a - b) <= eps;
}

/* ---- resampler ---- */

#define SINE_FRAMES 32768 /* one second at the GBA's rate */

static int16_t *sine(double hz, double amplitude, size_t frames) {
    int16_t *s = malloc(frames * 4);
    for (size_t i = 0; i < frames; i++) {
        int16_t v = (int16_t)lrint(amplitude * sin(2.0 * PI * hz * (double)i / 32768.0));
        s[i * 2] = v;
        s[i * 2 + 1] = v;
    }
    return s;
}

/* Frequency of the left channel from its first and last upward zero crossings (linear interpolation). */
static double frequency(const int16_t *s, size_t frames, double rate) {
    double first = -1, last = -1;
    int count = 0;
    for (size_t k = 1; k < frames; k++) {
        double a = s[(k - 1) * 2], b = s[k * 2];
        if (a < 0 && b >= 0) {
            double pos = (double)(k - 1) + (-a) / (b - a);
            if (first < 0) first = pos;
            last = pos;
            count++;
        }
    }
    return (count - 1) / ((last - first) / rate);
}

static int peak(const int16_t *s, size_t frames) {
    int p = 0;
    for (size_t k = 0; k < frames; k++) p = abs(s[k * 2]) > p ? abs(s[k * 2]) : p;
    return p;
}

static int biggest_step(const int16_t *s, size_t frames) {
    int m = 0;
    for (size_t k = 1; k < frames; k++) {
        int d = abs(s[k * 2] - s[(k - 1) * 2]);
        m = d > m ? d : m;
    }
    return m;
}

TEST(resampler_keeps_a_440_hz_tone_at_440_hz_and_its_loudness) {
    int16_t *in = sine(440.0, 10000.0, SINE_FRAMES);
    int16_t *out = malloc(50000 * 4);
    resampler_t r;
    resampler_init(&r);
    size_t n = resampler_run(&r, in, SINE_FRAMES, BASE_48K, out, 50000);
    CHECK(near(frequency(out, n, 48000.0), 440.0, 0.5));
    CHECK(abs(peak(out, n) - 10000) <= 100); /* within 1 % */
    free(in);
    free(out);
}

TEST(resampler_constant_input_gives_exactly_that_constant) {
    int16_t *in = malloc(1000 * 4);
    for (int i = 0; i < 2000; i++) in[i] = 1234;
    int16_t out[2000 * 2];
    resampler_t r;
    resampler_init(&r);
    size_t n = resampler_run(&r, in, 1000, BASE_48K, out, 2000);
    bool exact = true;
    /* The window starts as silence: the first 4 input frames (about 6 output frames) mix zeros in. */
    for (size_t k = 8; k < n; k++) exact &= out[k * 2] == 1234 && out[k * 2 + 1] == 1234;
    CHECK(exact);
    free(in);
}

TEST(resampler_in_pieces_gives_the_same_samples_as_all_at_once) {
    size_t frames = 10000;
    int16_t *in = malloc(frames * 4);
    uint32_t x = 12345;
    for (size_t i = 0; i < frames * 2; i++) {
        x = x * 1103515245u + 12345u;
        in[i] = (int16_t)(x >> 16);
    }
    int16_t *whole = malloc(20000 * 4), *pieces = malloc(20000 * 4);
    resampler_t a, b;
    resampler_init(&a);
    resampler_init(&b);
    size_t n_whole = resampler_run(&a, in, frames, BASE_48K, whole, 20000);
    size_t n_pieces = 0, done = 0, size = 1;
    while (done < frames) {
        size_t n = size < frames - done ? size : frames - done;
        n_pieces += resampler_run(&b, in + done * 2, n, BASE_48K, pieces + n_pieces * 2, 20000 - n_pieces);
        done += n;
        size = size * 3 % 997 + 1;
    }
    CHECK_EQ(n_pieces, n_whole);
    CHECK(memcmp(whole, pieces, n_whole * 4) == 0);
    free(in);
    free(whole);
    free(pieces);
}

TEST(resampler_output_count_follows_the_ratio) {
    int16_t *in = sine(100.0, 1000.0, SINE_FRAMES);
    int16_t *out = malloc(50000 * 4);
    resampler_t r;
    resampler_init(&r);
    size_t n = resampler_run(&r, in, SINE_FRAMES, BASE_48K, out, 50000);
    CHECK(n >= 47998 && n <= 48002);
    free(in);
    free(out);
}

TEST(resampler_ratio_change_mid_stream_makes_no_jump) {
    int16_t *in = sine(440.0, 10000.0, SINE_FRAMES);
    int16_t *out = malloc(50000 * 4);
    resampler_t r;
    resampler_init(&r);
    size_t n = resampler_run(&r, in, SINE_FRAMES, BASE_48K, out, 50000);
    int steady = biggest_step(out + 8, n - 4);
    resampler_init(&r);
    size_t half = SINE_FRAMES / 2;
    size_t n1 = resampler_run(&r, in, half, BASE_48K, out, 50000);
    size_t n2 = resampler_run(&r, in + half * 2, SINE_FRAMES - half, BASE_48K * 1.005, out + n1 * 2, 50000 - n1);
    int changed = biggest_step(out + 8, n1 + n2 - 4);
    CHECK(changed <= steady * 102 / 100 + 2);
    free(in);
    free(out);
}
```

Append to `tests/unit/CMakeLists.txt`:

```cmake
target_sources(unit_tests PRIVATE ${PLATFORM_DIR}/resampler.c)
```

- [x] **Step 2: Run to see them fail**

```bash
bash tools/dev.sh configure && bash tools/dev.sh unit
```

Expected: CMake configure fails — `Cannot find source file` (`resampler.c`).

- [x] **Step 3: Write the code**

`platform/android/resampler.h`:

```c
#ifndef GBAEMU_RESAMPLER_H
#define GBAEMU_RESAMPLER_H

#include <stddef.h>
#include <stdint.h>

/* Stereo int16 rate conversion with 4-point cubic Hermite (Catmull-Rom) interpolation. It keeps its
 * place between calls, so converting in pieces gives exactly the samples of converting all at once. */
typedef struct {
    int16_t w[4][2]; /* the last four input frames: x[n-1], x[n], x[n+1], x[n+2] */
    double t;        /* position between x[n] and x[n+1], in input frames; >= 1 means "take more input" */
} resampler_t;

void resampler_init(resampler_t *r);
/* Converts all `in_frames` input frames at `ratio` output frames per input frame into `out` (room for
 * `out_cap` frames — give it at least in_frames × ratio + 4); returns the frames written. The ratio
 * may change from call to call. */
size_t resampler_run(resampler_t *r, const int16_t *in, size_t in_frames, double ratio, int16_t *out,
                     size_t out_cap);

#endif
```

`platform/android/resampler.c`:

```c
#include "resampler.h"

#include <string.h>

void resampler_init(resampler_t *r) {
    memset(r->w, 0, sizeof r->w);
    r->t = 1.0; /* the first call starts by taking input */
}

static int16_t hermite(double xm1, double x0, double x1, double x2, double t) {
    double y = x0 + 0.5 * t * (x1 - xm1 + t * (2.0 * xm1 - 5.0 * x0 + 4.0 * x1 - x2 + t * (3.0 * (x0 - x1) + x2 - xm1)));
    double rounded = y >= 0.0 ? y + 0.5 : y - 0.5;
    if (rounded > 32767.0) return 32767;
    if (rounded < -32768.0) return -32768;
    return (int16_t)rounded;
}

size_t resampler_run(resampler_t *r, const int16_t *in, size_t in_frames, double ratio, int16_t *out,
                     size_t out_cap) {
    double step = 1.0 / ratio;
    size_t i = 0, n = 0;
    for (;;) {
        while (r->t >= 1.0) {
            if (i == in_frames) return n;
            memmove(r->w[0], r->w[1], sizeof r->w[0] * 3);
            r->w[3][0] = in[i * 2];
            r->w[3][1] = in[i * 2 + 1];
            i++;
            r->t -= 1.0;
        }
        if (n < out_cap) {
            for (int c = 0; c < 2; c++) out[n * 2 + c] = hermite(r->w[0][c], r->w[1][c], r->w[2][c], r->w[3][c], r->t);
            n++;
        } /* out of room: the input is still consumed (and those outputs dropped) */
        r->t += step;
    }
}
```

(The window starts as silence, so the first ~6 output frames mix zeros in — the constant-input test skips 8.)

- [x] **Step 4: Run the tests**

```bash
bash tools/dev.sh configure && bash tools/dev.sh unit
bash tools/dev.sh test
```

Expected: `349 tests, 0 failed` (+5 resampler); CTest 56/56.

- [x] **Step 5: Commit**

```bash
git add tests/unit/test_resampler.c tests/unit/CMakeLists.txt platform/android/resampler.h platform/android/resampler.c docs/PROGRESS.md docs/superpowers/plans/2026-09-26-milestone-4b-sound.md
git commit -m "feat(android): cubic Hermite resampler for 32,768 Hz to the phone's rate" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

**Batch 1 (Tasks 1-2) done — checkpoint with the user.**

---
### Task 3: Sound output and sound pacing

**Files:**
- Create: `platform/android/audio_out.h`, `platform/android/audio_out.c`, `app/src/androidTest/java/com/tengen/gbaemu/AudioTest.kt`
- Modify: `platform/android/emu_thread.h`, `platform/android/emu_thread.c`, `platform/android/jni_bridge.c`, `app/src/main/java/com/tengen/gbaemu/NativeBridge.kt`, `platform/android/CMakeLists.txt`

- [x] **Step 1: Write the failing tests**

`app/src/androidTest/java/com/tengen/gbaemu/AudioTest.kt` (Task 4 adds two tests):

```kotlin
package com.tengen.gbaemu

import android.os.SystemClock
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlin.math.abs
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Sound through AAudio: the stream, sound pacing, pause, the clock fallback (Task 4 adds reopening and the watchdog). */
@RunWith(AndroidJUnit4::class)
class AudioTest {
    private val ACTIVE = 0
    private val RATE = 1
    private val BURST = 2
    private val PLAYED = 3
    private val SILENT = 4
    private val LOUD = 5

    @After
    fun stopSession() = NativeBridge.stop()

    private fun load(rom: String) = assertEquals(0, NativeBridge.loadRom(TestRoms.rom(rom)))

    private fun stats() = NativeBridge.debugAudioStats()

    /** Frames the emulation runs in 5 s, measured after a warm-up, against 59.7275 per second. */
    private fun assertGbaSpeed() {
        Thread.sleep(500)
        val startFrames = NativeBridge.debugFrameCount()
        val startMs = SystemClock.elapsedRealtime()
        Thread.sleep(5000)
        val frames = NativeBridge.debugFrameCount() - startFrames
        val expected = (SystemClock.elapsedRealtime() - startMs) * 59.7275 / 1000.0
        assertTrue("ran $frames frames, expected about $expected", abs(frames - expected) <= 3.0)
    }

    @Test
    fun streamOpensOnTheEmulator() {
        load("tonc/m7_demo.gba")
        NativeBridge.start()
        Thread.sleep(300)
        val s = stats()
        assertEquals("sound active", 1L, s[ACTIVE])
        assertTrue("sample rate ${s[RATE]}", s[RATE] > 0)
        assertTrue("burst ${s[BURST]}", s[BURST] > 0)
    }

    @Test
    fun runsAtGbaSpeedPacedBySound() {
        load("tonc/m7_demo.gba")
        NativeBridge.start()
        assertGbaSpeed()
        assertEquals("sound active", 1L, stats()[ACTIVE])
    }

    @Test
    fun soundReallyPlays() {
        load("../own_roms/snd_psg.gba")
        NativeBridge.start()
        Thread.sleep(500)
        val a = stats()
        val startMs = SystemClock.elapsedRealtime()
        Thread.sleep(2500)
        val b = stats()
        val expected = (SystemClock.elapsedRealtime() - startMs) * a[RATE] / 1000.0
        val played = b[PLAYED] - a[PLAYED]
        assertTrue("played $played frames, expected about $expected", abs(played - expected) <= expected * 0.05)
        assertTrue("nothing but silence", b[LOUD] > 0)
        assertEquals("silence-filled frames while playing", 0L, b[SILENT] - a[SILENT])
    }

    @Test
    fun pauseStopsTheSoundAndResumeRestartsIt() {
        load("../own_roms/snd_psg.gba")
        NativeBridge.start()
        Thread.sleep(500)
        NativeBridge.pause()
        Thread.sleep(100)
        val paused = stats()[PLAYED]
        Thread.sleep(300)
        assertEquals("frames played while paused", paused, stats()[PLAYED])
        NativeBridge.resume()
        Thread.sleep(300)
        assertTrue("no sound after resume", stats()[PLAYED] > paused)
    }

    @Test
    fun withoutSoundTheClockPacesTheGame() {
        load("tonc/m7_demo.gba")
        NativeBridge.debugSetAudioEnabled(false)
        NativeBridge.start()
        assertGbaSpeed()
        assertEquals("sound active", 0L, stats()[ACTIVE])
    }
}
```

- [x] **Step 2: Run to see them fail**

```bash
bash tools/dev.sh androidtest
```

Expected: FAIL — `Unresolved reference 'debugAudioStats'` (and `debugSetAudioEnabled`).

- [x] **Step 3: The AAudio wrapper**

`platform/android/audio_out.h`:

```c
#ifndef GBAEMU_AUDIO_OUT_H
#define GBAEMU_AUDIO_OUT_H

#include <stdatomic.h>
#include <stdbool.h>

#include "audio_ring.h"

/* Counters the AAudio callback keeps (whole session, across reopens). */
typedef struct {
    atomic_llong played; /* frames handed to AAudio */
    atomic_llong silent; /* of those, filled with silence because the ring ran short */
    atomic_llong loud;   /* of those, frames with any non-zero sample */
} audio_stats_t;

/* One AAudio output stream (shared, low latency, stereo int16, the device's own rate) that plays
 * from `ring`. All calls except the callbacks come from the emulation thread. */
typedef struct audio_out audio_out_t;

audio_out_t *audio_out_open(audio_ring_t *ring, audio_stats_t *stats); /* NULL if AAudio cannot open */
bool audio_out_start(audio_out_t *a);
void audio_out_pause(audio_out_t *a); /* returns once the callback has stopped (or after 200 ms) */
bool audio_out_disconnected(audio_out_t *a); /* the error callback reported a lost device */
int audio_out_rate(const audio_out_t *a);
int audio_out_burst(const audio_out_t *a);
void audio_out_close(audio_out_t *a);

#endif
```

`platform/android/audio_out.c`:

```c
#include "audio_out.h"

#include <aaudio/AAudio.h>
#include <android/log.h>
#include <stdlib.h>
#include <string.h>

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "gbaemu", __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "gbaemu", __VA_ARGS__)

struct audio_out {
    AAudioStream *stream;
    audio_ring_t *ring;
    audio_stats_t *stats;
    atomic_bool disconnected;
    int rate, burst;
};

/* AAudio's high-priority thread: copy from the ring, silence for what is missing. No locks, no allocation. */
static aaudio_data_callback_result_t on_data(AAudioStream *stream, void *user, void *audio, int32_t frames) {
    (void)stream;
    audio_out_t *a = user;
    int16_t *out = audio;
    size_t got = audio_ring_pop(a->ring, out, (size_t)frames);
    if (got < (size_t)frames) memset(out + got * 2, 0, ((size_t)frames - got) * 4);
    long long loud = 0;
    for (size_t i = 0; i < got; i++) loud += (out[i * 2] | out[i * 2 + 1]) != 0;
    atomic_fetch_add_explicit(&a->stats->played, frames, memory_order_relaxed);
    atomic_fetch_add_explicit(&a->stats->silent, (long long)((size_t)frames - got), memory_order_relaxed);
    atomic_fetch_add_explicit(&a->stats->loud, loud, memory_order_relaxed);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

/* Called on its own thread when the stream breaks (e.g. headphones unplugged). Reopening is not
 * allowed here: only raise the flag; the emulation thread reopens at the next frame. */
static void on_error(AAudioStream *stream, void *user, aaudio_result_t error) {
    (void)stream;
    (void)error;
    audio_out_t *a = user;
    atomic_store(&a->disconnected, true);
}

audio_out_t *audio_out_open(audio_ring_t *ring, audio_stats_t *stats) {
    audio_out_t *a = calloc(1, sizeof *a);
    if (!a) return NULL;
    a->ring = ring;
    a->stats = stats;
    atomic_init(&a->disconnected, false);
    AAudioStreamBuilder *b = NULL;
    aaudio_result_t res = AAudio_createStreamBuilder(&b);
    if (res != AAUDIO_OK) {
        LOGE("AAudio builder: %s", AAudio_convertResultToText(res));
        free(a);
        return NULL;
    }
    AAudioStreamBuilder_setDirection(b, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setChannelCount(b, 2);
    AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    if (__builtin_available(android 28, *)) {
        AAudioStreamBuilder_setUsage(b, AAUDIO_USAGE_GAME);
        AAudioStreamBuilder_setContentType(b, AAUDIO_CONTENT_TYPE_MUSIC);
    }
    AAudioStreamBuilder_setDataCallback(b, on_data, a);
    AAudioStreamBuilder_setErrorCallback(b, on_error, a);
    res = AAudioStreamBuilder_openStream(b, &a->stream);
    AAudioStreamBuilder_delete(b);
    if (res != AAUDIO_OK) {
        LOGE("AAudio open: %s", AAudio_convertResultToText(res));
        free(a);
        return NULL;
    }
    a->rate = AAudioStream_getSampleRate(a->stream);
    a->burst = AAudioStream_getFramesPerBurst(a->stream);
    /* Two bursts of AAudio's own buffering is enough (Google's advice); the phone emulator's default
     * was 4,360 frames = 91 ms of extra delay. */
    AAudioStream_setBufferSizeInFrames(a->stream, a->burst * 2);
    LOGI("AAudio stream: %d Hz, burst %d frames, buffer %d frames", a->rate, a->burst,
         AAudioStream_getBufferSizeInFrames(a->stream));
    return a;
}

bool audio_out_start(audio_out_t *a) {
    aaudio_result_t res = AAudioStream_requestStart(a->stream);
    if (res != AAUDIO_OK) LOGE("AAudio start: %s", AAudio_convertResultToText(res));
    return res == AAUDIO_OK;
}

void audio_out_pause(audio_out_t *a) {
    if (AAudioStream_requestPause(a->stream) != AAUDIO_OK) return;
    aaudio_stream_state_t next = AAUDIO_STREAM_STATE_UNINITIALIZED;
    AAudioStream_waitForStateChange(a->stream, AAUDIO_STREAM_STATE_PAUSING, &next, 200 * 1000000LL);
}

bool audio_out_disconnected(audio_out_t *a) {
    return atomic_load(&a->disconnected);
}

int audio_out_rate(const audio_out_t *a) {
    return a->rate;
}

int audio_out_burst(const audio_out_t *a) {
    return a->burst;
}

void audio_out_close(audio_out_t *a) {
    if (!a) return;
    AAudioStream_requestStop(a->stream);
    AAudioStream_close(a->stream);
    free(a);
}
```

In `platform/android/CMakeLists.txt` replace the `target_link_libraries(gbaemu ...)` line with:

```cmake
target_link_libraries(gbaemu PRIVATE gbacore android log EGL GLESv3 aaudio)
```

- [x] **Step 4: The emulation thread with sound**

Replace `platform/android/emu_thread.h` with:

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

/* Creates the core, loads the ROM (copied), sets the clock from the phone; NULL + *err on failure. */
emu_t *emu_create(const uint8_t *rom, size_t size, gba_err_t *err);
void emu_start(emu_t *e);
void emu_pause(emu_t *e);
void emu_resume(emu_t *e); /* timing starts afresh: no rush of catch-up frames */
void emu_set_keys(emu_t *e, uint16_t pressed); /* GBA_KEY_* mask */
triple_buffer_t *emu_frames(emu_t *e);
uint64_t emu_frame_count(const emu_t *e);
void emu_destroy(emu_t *e); /* stops the thread (if started) and frees everything */

/* ---- test hooks ---- */
void emu_set_audio_enabled(emu_t *e, bool enabled); /* before emu_start: false = no stream, clock pacing */
/* active (1/0), sample rate, frames per burst, frames played, frames silence-filled, loud frames, reopens */
void emu_audio_stats(emu_t *e, int64_t out[7]);

#endif
```

Replace `platform/android/emu_thread.c` with:

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
#include "rate_control.h"
#include "resampler.h"

#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "gbaemu", __VA_ARGS__)

#define AUDIO_TARGET_MS 30        /* the sound buffer is kept at about this much */
#define CONVERTED_FRAMES 8192     /* room for 1,024 GBA frames converted at up to ~8x */

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

    /* Sound. The emulation thread alone touches out/rs/converted; the rest is shared as marked. */
    bool audio_wanted;                 /* set before start (test hook) */
    audio_out_t *out;                  /* NULL: no sound, the clock paces the game */
    audio_ring_t ring;                 /* emulation thread → AAudio callback */
    resampler_t rs;
    int16_t converted[CONVERTED_FRAMES * 2];
    audio_stats_t stats;               /* kept by the callback */
    atomic_bool sound_on;
    atomic_int rate, burst, reopens;
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

/* Sound pacing: wait (1 ms at a time) while the ring holds more than the target. */
static void sound_pace(emu_t *e) {
    size_t target = target_frames(e);
    while (audio_ring_queued(&e->ring) > target && !paused_or_quitting(e)) {
        struct timespec ms = {0, 1000000};
        nanosleep(&ms, NULL);
    }
}

static void *emu_main(void *arg) {
    emu_t *e = arg;
    frame_pacer_t pacer;
    pacer_reset(&pacer);
    if (e->audio_wanted) sound_open(e);
    for (;;) {
        pthread_mutex_lock(&e->lock);
        if (e->paused && !e->quit) {
            if (e->out) { /* stop the callback, then drop the stale sound */
                audio_out_pause(e->out);
                audio_ring_clear(&e->ring);
                resampler_init(&e->rs);
            }
            while (e->paused && !e->quit) pthread_cond_wait(&e->wake, &e->lock);
            if (e->out && !e->quit) audio_out_start(e->out);
            pacer_reset(&pacer);
        }
        bool quit = e->quit;
        pthread_mutex_unlock(&e->lock);
        if (quit) break;

        gba_set_keys(e->g, (uint16_t)atomic_load(&e->keys));
        gba_run_frame(e->g);
        memcpy(tb_back(&e->tb), gba_framebuffer(e->g), sizeof(uint16_t) * TB_PIXELS);
        tb_publish(&e->tb);
        feed_sound(e);
        atomic_fetch_add(&e->frames, 1);

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
    sound_close(e);
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

void emu_set_audio_enabled(emu_t *e, bool enabled) {
    if (!e->started) e->audio_wanted = enabled;
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
    free(e);
}
```

Replace `platform/android/jni_bridge.c` with (4a's file + `debugAudioStats` and `debugSetAudioEnabled`):

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

    /** Sound since loadRom: [active 1/0, sample rate, frames per burst, played, silence-filled, loud, reopens]. */
    external fun debugAudioStats(): LongArray

    /** Before start(): false = no sound stream, the clock paces the game. */
    external fun debugSetAudioEnabled(enabled: Boolean)

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

Expected: `tests="13" failures="0"` three times (the 8 from 4a + 5 new); `0` C warnings. `logcat -s gbaemu` shows `AAudio stream: 48000 Hz, burst 960 frames, buffer 1920 frames` on the emulator.

- [x] **Step 6: Commit**

```bash
git add platform/android/audio_out.h platform/android/audio_out.c app/src/androidTest/java/com/tengen/gbaemu/AudioTest.kt platform/android/emu_thread.h platform/android/emu_thread.c platform/android/jni_bridge.c app/src/main/java/com/tengen/gbaemu/NativeBridge.kt platform/android/CMakeLists.txt docs/PROGRESS.md docs/superpowers/plans/2026-09-26-milestone-4b-sound.md
git commit -m "feat(android): AAudio sound output; the sound paces the emulation" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 4: Device changes and the watchdog

**Files:**
- Modify: `app/src/androidTest/java/com/tengen/gbaemu/AudioTest.kt`, `platform/android/emu_thread.h`, `platform/android/emu_thread.c`, `platform/android/jni_bridge.c`, `app/src/main/java/com/tengen/gbaemu/NativeBridge.kt`

- [x] **Step 1: Write the failing tests**

Replace `app/src/androidTest/java/com/tengen/gbaemu/AudioTest.kt` with (adds `REOPENS` and the tests `aLostSoundDeviceReopensTheStream`, `aStuckStreamIsReopenedByTheWatchdog`):

```kotlin
package com.tengen.gbaemu

import android.os.SystemClock
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlin.math.abs
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Sound through AAudio: the stream, sound pacing, pause, the clock fallback, reopening, the watchdog. */
@RunWith(AndroidJUnit4::class)
class AudioTest {
    private val ACTIVE = 0
    private val RATE = 1
    private val BURST = 2
    private val PLAYED = 3
    private val SILENT = 4
    private val LOUD = 5
    private val REOPENS = 6

    @After
    fun stopSession() = NativeBridge.stop()

    private fun load(rom: String) = assertEquals(0, NativeBridge.loadRom(TestRoms.rom(rom)))

    private fun stats() = NativeBridge.debugAudioStats()

    /** Frames the emulation runs in 5 s, measured after a warm-up, against 59.7275 per second. */
    private fun assertGbaSpeed() {
        Thread.sleep(500)
        val startFrames = NativeBridge.debugFrameCount()
        val startMs = SystemClock.elapsedRealtime()
        Thread.sleep(5000)
        val frames = NativeBridge.debugFrameCount() - startFrames
        val expected = (SystemClock.elapsedRealtime() - startMs) * 59.7275 / 1000.0
        assertTrue("ran $frames frames, expected about $expected", abs(frames - expected) <= 3.0)
    }

    @Test
    fun streamOpensOnTheEmulator() {
        load("tonc/m7_demo.gba")
        NativeBridge.start()
        Thread.sleep(300)
        val s = stats()
        assertEquals("sound active", 1L, s[ACTIVE])
        assertTrue("sample rate ${s[RATE]}", s[RATE] > 0)
        assertTrue("burst ${s[BURST]}", s[BURST] > 0)
    }

    @Test
    fun runsAtGbaSpeedPacedBySound() {
        load("tonc/m7_demo.gba")
        NativeBridge.start()
        assertGbaSpeed()
        assertEquals("sound active", 1L, stats()[ACTIVE])
    }

    @Test
    fun soundReallyPlays() {
        load("../own_roms/snd_psg.gba")
        NativeBridge.start()
        Thread.sleep(500)
        val a = stats()
        val startMs = SystemClock.elapsedRealtime()
        Thread.sleep(2500)
        val b = stats()
        val expected = (SystemClock.elapsedRealtime() - startMs) * a[RATE] / 1000.0
        val played = b[PLAYED] - a[PLAYED]
        assertTrue("played $played frames, expected about $expected", abs(played - expected) <= expected * 0.05)
        assertTrue("nothing but silence", b[LOUD] > 0)
        assertEquals("silence-filled frames while playing", 0L, b[SILENT] - a[SILENT])
    }

    @Test
    fun pauseStopsTheSoundAndResumeRestartsIt() {
        load("../own_roms/snd_psg.gba")
        NativeBridge.start()
        Thread.sleep(500)
        NativeBridge.pause()
        Thread.sleep(100)
        val paused = stats()[PLAYED]
        Thread.sleep(300)
        assertEquals("frames played while paused", paused, stats()[PLAYED])
        NativeBridge.resume()
        Thread.sleep(300)
        assertTrue("no sound after resume", stats()[PLAYED] > paused)
    }

    @Test
    fun withoutSoundTheClockPacesTheGame() {
        load("tonc/m7_demo.gba")
        NativeBridge.debugSetAudioEnabled(false)
        NativeBridge.start()
        assertGbaSpeed()
        assertEquals("sound active", 0L, stats()[ACTIVE])
    }

    @Test
    fun aLostSoundDeviceReopensTheStream() {
        load("../own_roms/snd_psg.gba")
        NativeBridge.start()
        Thread.sleep(500)
        NativeBridge.debugSimulateAudioDisconnect()
        Thread.sleep(500)
        val a = stats()
        assertEquals("reopens", 1L, a[REOPENS])
        assertEquals("sound active", 1L, a[ACTIVE])
        Thread.sleep(300)
        assertTrue("no sound after reopening", stats()[PLAYED] > a[PLAYED])
    }

    @Test
    fun aStuckStreamIsReopenedByTheWatchdog() {
        load("../own_roms/snd_psg.gba")
        NativeBridge.start()
        Thread.sleep(2500) /* past the watchdog's 2 s start-up grace */
        NativeBridge.debugSimulateStuckStream()
        Thread.sleep(800) /* 250 ms without draining, then the reopen */
        val a = stats()
        assertEquals("reopens", 1L, a[REOPENS])
        assertEquals("sound active", 1L, a[ACTIVE])
        Thread.sleep(300)
        assertTrue("no sound after reopening", stats()[PLAYED] > a[PLAYED])
    }
}
```

- [x] **Step 2: Run to see them fail**

```bash
bash tools/dev.sh androidtest
```

Expected: FAIL — `Unresolved reference 'debugSimulateAudioDisconnect'` (and `debugSimulateStuckStream`).

- [x] **Step 3: Reopen and watchdog**

Replace `platform/android/emu_thread.h` with the final version:

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

/* Creates the core, loads the ROM (copied), sets the clock from the phone; NULL + *err on failure. */
emu_t *emu_create(const uint8_t *rom, size_t size, gba_err_t *err);
void emu_start(emu_t *e);
void emu_pause(emu_t *e);
void emu_resume(emu_t *e); /* timing starts afresh: no rush of catch-up frames */
void emu_set_keys(emu_t *e, uint16_t pressed); /* GBA_KEY_* mask */
triple_buffer_t *emu_frames(emu_t *e);
uint64_t emu_frame_count(const emu_t *e);
void emu_destroy(emu_t *e); /* stops the thread (if started) and frees everything */

/* ---- test hooks ---- */
void emu_set_audio_enabled(emu_t *e, bool enabled); /* before emu_start: false = no stream, clock pacing */
void emu_simulate_audio_disconnect(emu_t *e);        /* as if the sound device went away */
void emu_simulate_stuck_stream(emu_t *e);            /* the stream stops taking sound, unannounced */
/* active (1/0), sample rate, frames per burst, frames played, frames silence-filled, loud frames, reopens */
void emu_audio_stats(emu_t *e, int64_t out[7]);

#endif
```

Replace `platform/android/emu_thread.c` with the final version:

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
#include "rate_control.h"
#include "resampler.h"

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
    atomic_uint keys;
    atomic_ullong frames;

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

static void *emu_main(void *arg) {
    emu_t *e = arg;
    frame_pacer_t pacer;
    pacer_reset(&pacer);
    if (e->audio_wanted) sound_open(e);
    for (;;) {
        pthread_mutex_lock(&e->lock);
        if (e->paused && !e->quit) {
            if (e->out) { /* stop the callback, then drop the stale sound */
                audio_out_pause(e->out);
                audio_ring_clear(&e->ring);
                resampler_init(&e->rs);
            }
            while (e->paused && !e->quit) pthread_cond_wait(&e->wake, &e->lock);
            if (e->out && !e->quit) {
                audio_out_start(e->out);
                e->sound_started_ns = now_ns();
            }
            pacer_reset(&pacer);
        }
        bool quit = e->quit;
        pthread_mutex_unlock(&e->lock);
        if (quit) break;

        gba_set_keys(e->g, (uint16_t)atomic_load(&e->keys));
        gba_run_frame(e->g);
        memcpy(tb_back(&e->tb), gba_framebuffer(e->g), sizeof(uint16_t) * TB_PIXELS);
        tb_publish(&e->tb);
        feed_sound(e);
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
    sound_close(e);
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

void emu_set_audio_enabled(emu_t *e, bool enabled) {
    if (!e->started) e->audio_wanted = enabled;
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
    free(e);
}
```

(The watchdog's 2 s start-up grace matters: without it the prototype mistook the emulator's 350 ms cold start for a stuck stream.)

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

    /** Sound since loadRom: [active 1/0, sample rate, frames per burst, played, silence-filled, loud, reopens]. */
    external fun debugAudioStats(): LongArray

    /** Before start(): false = no sound stream, the clock paces the game. */
    external fun debugSetAudioEnabled(enabled: Boolean)

    /** As if the sound device went away (headphones unplugged): the stream must reopen. */
    external fun debugSimulateAudioDisconnect()

    /** The stream silently stops taking sound: the watchdog must reopen it (after its 2 s start-up grace). */
    external fun debugSimulateStuckStream()

    /** Runs a ROM headless for n frames on this thread: "<frame hash> <audio hash>" like the PC test runner. */
    external fun debugRunFrames(rom: ByteBuffer, frames: Int): String?

    /** Draws one 240x160 BGR555 frame offscreen; RGBA pixels (r | g<<8 | b<<16 | a<<24), top row first. */
    external fun debugDrawFrame(pixels: ShortArray, width: Int, height: Int): IntArray?
}
```

- [x] **Step 4: Run the tests (three times)**

```bash
for i in 1 2 3; do rm -rf app/build/outputs/androidTest-results; bash tools/dev.sh androidtest 2>&1 | grep -E "^e: |Starting|BUILD FAILED"; grep -h -o 'tests="[0-9]*" failures="[0-9]*"' app/build/outputs/androidTest-results/connected/debug/*.xml; done
echo "C warnings: $(bash tools/dev.sh apk --rerun-tasks 2>&1 | grep -c 'warning:.*\.[ch]:')"
```

Expected: `tests="15" failures="0"` three times; `0` C warnings; `logcat -s gbaemu:W` shows exactly one `sound device changed` and one `sound stream stuck: reopening it` per run (from the two new tests) and nothing else.

- [x] **Step 5: Commit**

```bash
git add app/src/androidTest/java/com/tengen/gbaemu/AudioTest.kt platform/android/emu_thread.h platform/android/emu_thread.c platform/android/jni_bridge.c app/src/main/java/com/tengen/gbaemu/NativeBridge.kt docs/PROGRESS.md docs/superpowers/plans/2026-09-26-milestone-4b-sound.md
git commit -m "feat(android): reopen the sound stream after a device change; watchdog for a stuck stream" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

**Batch 2 (Tasks 3-4) done — checkpoint with the user.**

---
### Task 5: Short key presses

**Why (found in the listening check, 2026-09-26):** in the user's game A hack only the arrows and Enter worked. A `getevent -lt` recording on the phone emulator showed its window sends **letter keys (X = A, Z = B, …) as down+up pairs 0.1 ms apart — even while the key is held** (holding X for 2 s gave a stream of 0.1 ms taps), while Enter is one real press (256 ms) and a held arrow repeats as ~34 ms presses. The emulation thread reads the held-keys mask once per frame (every 16.7 ms), so a 0.1 ms tap is almost never seen. Fix: every button pressed since the last frame counts as held for that frame (a latch), so no press is lost — also useful for short touch taps in 4c. Limit on the PC emulator: holding a letter key acts like fast repeated taps (e.g. holding Z to run in game A stutters); real phones, gamepads and the arrow keys are not affected.

**Files:**
- Create: `platform/android/key_latch.h`, `platform/android/key_latch.c`, `tests/unit/test_keys.c`, `app/src/androidTest/java/com/tengen/gbaemu/KeyTest.kt`
- Modify: `tests/unit/CMakeLists.txt`, `platform/android/emu_thread.h`, `platform/android/emu_thread.c`, `platform/android/jni_bridge.c`, `app/src/main/java/com/tengen/gbaemu/NativeBridge.kt`

- [x] **Step 1: Write the failing PC tests**

`tests/unit/test_keys.c`:

```c
/* Short key presses (platform/android/key_latch.c): a press shorter than a frame still reaches the game. */
#include "key_latch.h"
#include "test.h"

#define KA 0x001u  /* GBA_KEY_A */
#define KUP 0x040u /* GBA_KEY_UP */

TEST(key_latch_nothing_pressed_gives_nothing) {
    key_latch_t k;
    key_latch_init(&k);
    CHECK_EQ(key_latch_take(&k), 0);
}

TEST(key_latch_a_tap_between_frames_counts_for_one_frame) {
    key_latch_t k;
    key_latch_init(&k);
    key_latch_set(&k, KA);
    key_latch_set(&k, 0);
    CHECK_EQ(key_latch_take(&k), KA);
    CHECK_EQ(key_latch_take(&k), 0);
}

TEST(key_latch_a_held_button_stays_until_released) {
    key_latch_t k;
    key_latch_init(&k);
    key_latch_set(&k, KA);
    CHECK_EQ(key_latch_take(&k), KA);
    CHECK_EQ(key_latch_take(&k), KA);
    key_latch_set(&k, 0);
    CHECK_EQ(key_latch_take(&k), 0);
}

TEST(key_latch_a_second_tap_after_a_frame_counts_again) {
    key_latch_t k;
    key_latch_init(&k);
    key_latch_set(&k, KA);
    CHECK_EQ(key_latch_take(&k), KA);
    key_latch_set(&k, 0);
    key_latch_set(&k, KA);
    key_latch_set(&k, 0);
    CHECK_EQ(key_latch_take(&k), KA);
    CHECK_EQ(key_latch_take(&k), 0);
}

TEST(key_latch_other_buttons_keep_their_own_state) {
    key_latch_t k;
    key_latch_init(&k);
    key_latch_set(&k, KUP);
    key_latch_set(&k, KUP | KA);
    key_latch_set(&k, KUP);
    CHECK_EQ(key_latch_take(&k), KUP | KA);
    CHECK_EQ(key_latch_take(&k), KUP);
    key_latch_set(&k, 0);
    CHECK_EQ(key_latch_take(&k), 0);
}
```

Append to `tests/unit/CMakeLists.txt`:

```cmake
target_sources(unit_tests PRIVATE ${PLATFORM_DIR}/key_latch.c)
```

- [x] **Step 2: Run to see them fail**

```bash
bash tools/dev.sh configure && bash tools/dev.sh unit
```

Expected: CMake configure fails — `Cannot find source file` (`key_latch.c`).

- [x] **Step 3: Write the latch**

`platform/android/key_latch.h`:

```c
#ifndef GBAEMU_KEY_LATCH_H
#define GBAEMU_KEY_LATCH_H

#include <stdatomic.h>

/* Held GBA buttons from the UI thread to the emulation thread, where a press shorter than a frame
 * still reaches the game: every button pressed since the last frame counts as held for that frame.
 * (The phone emulator's window sends letter keys as 0.1 ms taps; the game reads the keys once a frame.) */
typedef struct {
    atomic_uint held;    /* buttons held right now (GBA_KEY_* bits) */
    atomic_uint pressed; /* buttons that went down since the last key_latch_take */
} key_latch_t;

void key_latch_init(key_latch_t *k);
/* UI thread: the buttons now held. */
void key_latch_set(key_latch_t *k, unsigned held);
/* Emulation thread, once per frame: the buttons for this frame (held now, or pressed since the last call). */
unsigned key_latch_take(key_latch_t *k);

#endif
```

`platform/android/key_latch.c`:

```c
#include "key_latch.h"

void key_latch_init(key_latch_t *k) {
    atomic_init(&k->held, 0u);
    atomic_init(&k->pressed, 0u);
}

void key_latch_set(key_latch_t *k, unsigned held) {
    unsigned before = atomic_exchange(&k->held, held);
    atomic_fetch_or(&k->pressed, held & ~before);
}

unsigned key_latch_take(key_latch_t *k) {
    return atomic_load(&k->held) | atomic_exchange(&k->pressed, 0u);
}
```

- [x] **Step 4: Run the PC tests**

```bash
bash tools/dev.sh configure && bash tools/dev.sh unit && bash tools/dev.sh test
```

Expected: `354 tests, 0 failed` (349 + 5); CTest 56/56.

- [x] **Step 5: The on-device test and its hook — fails before the latch is wired in**

`app/src/androidTest/java/com/tengen/gbaemu/KeyTest.kt`:

```kotlin
package com.tengen.gbaemu

import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith

/** Buttons reach the game — even a tap shorter than a frame (the phone emulator sends letter keys that way). */
@RunWith(AndroidJUnit4::class)
class KeyTest {
    @After
    fun stopSession() = NativeBridge.stop()

    private fun startGame() {
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba")))
        NativeBridge.start()
        Thread.sleep(300)
    }

    @Test
    fun aTapShorterThanAFrameReachesTheGame() {
        startGame()
        repeat(20) {
            NativeBridge.debugKeysSeen() // forget what came before
            NativeBridge.setKeys(KeyMap.A)
            NativeBridge.setKeys(0)
            Thread.sleep(50) // three frames
            assertEquals("tap ${it + 1}", KeyMap.A, NativeBridge.debugKeysSeen())
        }
    }

    @Test
    fun aHeldButtonIsSeenAndAReleasedOneIsNot() {
        startGame()
        NativeBridge.setKeys(KeyMap.UP)
        Thread.sleep(100)
        NativeBridge.debugKeysSeen()
        Thread.sleep(100)
        assertEquals("held", KeyMap.UP, NativeBridge.debugKeysSeen())
        NativeBridge.setKeys(0)
        Thread.sleep(100)
        NativeBridge.debugKeysSeen()
        Thread.sleep(100)
        assertEquals("released", 0, NativeBridge.debugKeysSeen())
    }
}
```

The hook first, without the latch: in `emu_thread.h` (test hooks) add

```c
unsigned emu_keys_seen(emu_t *e); /* buttons the game was given in any frame since the last call */
```

in `emu_thread.c` add the field `atomic_uint keys_seen; /* test hook */` after `atomic_ullong frames;`, `atomic_init(&e->keys_seen, 0u);` after `atomic_init(&e->frames, 0ull);`, replace the frame's `gba_set_keys(e->g, (uint16_t)atomic_load(&e->keys));` with

```c
        unsigned keys = atomic_load(&e->keys);
        atomic_fetch_or(&e->keys_seen, keys);
        gba_set_keys(e->g, (uint16_t)keys);
```

and add after `emu_set_audio_enabled`:

```c
unsigned emu_keys_seen(emu_t *e) {
    return atomic_exchange(&e->keys_seen, 0u);
}
```

`jni_bridge.c`, after `debugSimulateStuckStream`:

```c
JNIEXPORT jint JNICALL JNI_FN(debugKeysSeen)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    return s_emu ? (jint)emu_keys_seen(s_emu) : 0;
}
```

`NativeBridge.kt`, after `debugSimulateStuckStream`:

```kotlin
    /** Buttons the game was given in any frame since the last call (KeyMap bits). */
    external fun debugKeysSeen(): Int
```

```bash
bash tools/dev.sh androidtest
```

Expected: 17 tests, **1 failure** — `aTapShorterThanAFrameReachesTheGame` (`tap 1 expected:<1> but was:<0>`): the bug, reproduced on the device.

- [x] **Step 6: Wire in the latch**

In `emu_thread.c`: `#include "key_latch.h"`; the field `atomic_uint keys;` → `key_latch_t keys;`; `atomic_init(&e->keys, 0u);` → `key_latch_init(&e->keys);`; the frame's `unsigned keys = atomic_load(&e->keys);` → `unsigned keys = key_latch_take(&e->keys);`; `emu_set_keys` body → `key_latch_set(&e->keys, pressed);`.

- [x] **Step 7: Run the tests (three times)**

```bash
for i in 1 2 3; do rm -rf app/build/outputs/androidTest-results; bash tools/dev.sh androidtest 2>&1 | grep -E "^e: |Starting|BUILD FAILED"; grep -h -o 'tests="[0-9]*" failures="[0-9]*"' app/build/outputs/androidTest-results/connected/debug/*.xml; done
echo "C warnings: $(bash tools/dev.sh apk --rerun-tasks 2>&1 | grep -c 'warning:.*\.[ch]:')"
```

Expected: `tests="17" failures="0"` three times; `0` C warnings.

- [x] **Step 8: Commit**

```bash
git add platform/android/key_latch.h platform/android/key_latch.c tests/unit/test_keys.c tests/unit/CMakeLists.txt app/src/androidTest/java/com/tengen/gbaemu/KeyTest.kt platform/android/emu_thread.h platform/android/emu_thread.c platform/android/jni_bridge.c app/src/main/java/com/tengen/gbaemu/NativeBridge.kt docs
git commit -m "fix(android): a key tap shorter than a frame still reaches the game" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

Then the user tries X/Z in their game A hack again (install + open it).

---
### Task 6: Listening check and milestone 4b exit

- [x] **Step 1: Full run**

```bash
bash tools/dev.sh test && bash tools/dev.sh unit && bash tools/dev.sh jvmtest && bash tools/dev.sh androidtest
```

Expected: CTest 56/56 (sanitizers on, no reports), 354 unit tests, Kotlin 4, on-device 17 (emulator only).

- [x] **Step 2: Claude's check**

```bash
bash tools/dev.sh push tests/own_roms/snd_psg.gba
bash tools/dev.sh push tests/roms/tonc/snd1_demo.gba
bash tools/dev.sh install && bash tools/dev.sh launch
```

Open `snd_psg.gba` through the picker with Task 8 (4a)'s helpers (`tap_on`, `pick_download` — `docs/superpowers/plans/2026-09-26-milestone-4a-app-picture.md`, Task 8 Step 3), let it play 6 s, then check `ADB logcat -d -s gbaemu:*`: one `AAudio stream: ... Hz, burst ..., buffer ...` line and no warnings; `ADB logcat -d -s AndroidRuntime:E` shows no crash.

- [x] **Step 3: User's listening check** (the app is open on the emulator)

The user opens and listens to: `snd_psg.gba` (beep, higher beep, softer beep, hiss, rising tone — as approved in 3a), `snd1_demo.gba` (SOS twice), and their game A hack (already on the emulator): title music, menu blips when pressing keys feel immediate, a battle; Home and back again plays no stale sound. Clear, right pitch, no crackle.

- [x] **Step 4: Docs**

`docs/PROGRESS.md` (4b ✅ with the numbers and the user's check; Next = milestone 4c brainstorming — touch controls, save files, full lifecycle); spec status → `Implemented — milestone 4b complete <date> (tag milestone-4b)`; parent spec §10 row 4 → 4b ✅; `CLAUDE.md` key files → 4c needs brainstorming; tick every box here.

- [x] **Step 5: Commit, tag, merge** — ask the user how to integrate (so far always a fast-forward merge into `main`); then superpowers:finishing-a-development-branch, `bash tools/dev.sh test` and `bash tools/dev.sh androidtest` on `main`, delete the branch, record the merge in PROGRESS.md; delete the prototype (`git worktree remove --force <scratchpad>/p4b` or `rm -rf` + `git worktree prune`; `git branch -D proto-4b`) with the user's OK.

```bash
git add -A docs CLAUDE.md
git commit -m "docs: milestone 4b complete - sound" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git tag milestone-4b
```
