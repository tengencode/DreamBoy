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
