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
