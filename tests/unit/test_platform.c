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

TEST(fit_rect_mode_fit_is_fit_rect) {
    fit_rect_t a = fit_rect(1080, 2424), b = fit_rect_mode(1080, 2424, FIT_ASPECT);
    CHECK_EQ(b.x, a.x);
    CHECK_EQ(b.y, a.y);
    CHECK_EQ(b.w, a.w);
    CHECK_EQ(b.h, a.h);
}

TEST(fit_rect_mode_integer_takes_the_largest_whole_multiple_centred) {
    fit_rect_t r = fit_rect_mode(1080, 720, FIT_INTEGER); /* Pixel 9 upright: 4.5x → 4x */
    CHECK_EQ(r.w, 960);
    CHECK_EQ(r.h, 640);
    CHECK_EQ(r.x, 60);
    CHECK_EQ(r.y, 40);
    r = fit_rect_mode(2424, 1080, FIT_INTEGER); /* sideways: 6x by height */
    CHECK_EQ(r.w, 1440);
    CHECK_EQ(r.h, 960);
    CHECK_EQ(r.x, 492);
    CHECK_EQ(r.y, 60);
    r = fit_rect_mode(480, 320, FIT_INTEGER); /* exactly 2x */
    CHECK_EQ(r.w, 480);
    CHECK_EQ(r.h, 320);
    CHECK_EQ(r.x, 0);
    CHECK_EQ(r.y, 0);
}

TEST(fit_rect_mode_integer_smaller_than_1x_falls_back_to_fit) {
    fit_rect_t r = fit_rect_mode(200, 100, FIT_INTEGER);
    fit_rect_t f = fit_rect(200, 100);
    CHECK_EQ(r.w, f.w);
    CHECK_EQ(r.h, f.h);
    CHECK_EQ(r.x, f.x);
    CHECK_EQ(r.y, f.y);
}

TEST(fit_rect_mode_stretch_fills_the_surface) {
    fit_rect_t r = fit_rect_mode(2424, 1080, FIT_STRETCH);
    CHECK_EQ(r.x, 0);
    CHECK_EQ(r.y, 0);
    CHECK_EQ(r.w, 2424);
    CHECK_EQ(r.h, 1080);
    r = fit_rect_mode(0, 50, FIT_STRETCH);
    CHECK_EQ(r.w, 0);
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
