/* Rewind (core/src/rewind.c): stepping back gives exactly the machine of an earlier snapshot. */
#include <stdlib.h>
#include <string.h>

#include "gba_debug.h"
#include "test.h"
#include "testutil.h"

/* Keeps changing the machine: counts in r0, stores it to EWRAM, loops. */
static const uint32_t counter_ops[] = {0xE3A01402, 0xE2800001, 0xE5810000, 0xEAFFFFFC};

#define BUDGET (4u * 1024 * 1024)
#define EVERY 4
#define MAX_SNAPS 16

static uint8_t *state_of(const gba_t *g) {
    size_t n = gba_state_size(g);
    uint8_t *buf = malloc(n);
    CHECK_EQ(gba_state_save(g, buf, n), n);
    return buf;
}

static int same_as(const gba_t *g, const uint8_t *want) {
    uint8_t *now = state_of(g);
    int same = memcmp(now, want, gba_state_size(g)) == 0;
    free(now);
    return same;
}

/* Runs `snaps` × EVERY frames, keeping a copy of the machine at each snapshot (after frames 4, 8, ...). */
static void run_keeping(gba_t *g, int snaps, uint8_t **kept) {
    for (int s = 0; s < snaps; s++) {
        for (int f = 0; f < EVERY; f++) gba_run_frame(g);
        kept[s] = state_of(g);
    }
}

static void free_all(uint8_t **kept, int n) {
    for (int i = 0; i < n; i++) free(kept[i]);
}

TEST(rewind_steps_back_through_every_snapshot_exactly) {
    gba_t *g = tu_arm(counter_ops, TU_LEN(counter_ops));
    CHECK_EQ(gba_rewind_configure(g, BUDGET, EVERY), GBA_OK);
    uint8_t *kept[MAX_SNAPS];
    run_keeping(g, 6, kept);
    size_t snaps, bytes;
    gba_debug_rewind_stats(g, &snaps, &bytes);
    CHECK_EQ(snaps, 6);
    for (int s = 4; s >= 0; s--) { /* at the newest: the first step goes to the one before */
        CHECK_EQ(gba_rewind_step_back(g), 1);
        CHECK(same_as(g, kept[s]));
    }
    uint8_t *oldest = state_of(g);
    CHECK_EQ(gba_rewind_step_back(g), 0); /* nothing older: the machine stays */
    CHECK(same_as(g, oldest));
    free(oldest);
    free_all(kept, 6);
    gba_destroy(g);
}

TEST(rewind_between_snapshots_goes_to_the_newest_first) {
    gba_t *g = tu_arm(counter_ops, TU_LEN(counter_ops));
    CHECK_EQ(gba_rewind_configure(g, BUDGET, EVERY), GBA_OK);
    uint8_t *kept[MAX_SNAPS];
    run_keeping(g, 3, kept);
    gba_run_frame(g);
    gba_run_frame(g);
    CHECK_EQ(gba_rewind_step_back(g), 1);
    CHECK(same_as(g, kept[2]));
    CHECK_EQ(gba_rewind_step_back(g), 1);
    CHECK(same_as(g, kept[1]));
    free_all(kept, 3);
    gba_destroy(g);
}

TEST(rewind_playing_on_after_a_step_back_keeps_snapshots_every_4_frames) {
    gba_t *g = tu_arm(counter_ops, TU_LEN(counter_ops));
    CHECK_EQ(gba_rewind_configure(g, BUDGET, EVERY), GBA_OK);
    uint8_t *kept[MAX_SNAPS];
    run_keeping(g, 4, kept);
    CHECK_EQ(gba_rewind_step_back(g), 1); /* back to the 3rd snapshot */
    uint8_t *again[MAX_SNAPS];
    run_keeping(g, 2, again); /* two new snapshots after it */
    CHECK(memcmp(again[0], kept[3], gba_state_size(g)) == 0); /* the same machine as the first time */
    CHECK_EQ(gba_rewind_step_back(g), 1);
    CHECK(same_as(g, again[0]));
    CHECK_EQ(gba_rewind_step_back(g), 1);
    CHECK(same_as(g, kept[2]));
    free_all(kept, 4);
    free_all(again, 2);
    gba_destroy(g);
}

TEST(rewind_drops_the_oldest_when_the_budget_is_full) {
    gba_t *g = tu_arm(counter_ops, TU_LEN(counter_ops));
    CHECK_EQ(gba_rewind_configure(g, BUDGET, EVERY), GBA_OK);
    uint8_t *kept[MAX_SNAPS];
    run_keeping(g, 2, kept);
    size_t snaps, one;
    gba_debug_rewind_stats(g, &snaps, &one); /* one packed difference */
    free_all(kept, 2);
    CHECK_EQ(gba_rewind_configure(g, 3 * one, EVERY), GBA_OK); /* room for about two */
    run_keeping(g, 8, kept);
    size_t bytes;
    gba_debug_rewind_stats(g, &snaps, &bytes);
    CHECK(snaps >= 2 && snaps <= 4);
    CHECK(bytes <= 3 * one);
    for (size_t s = 1; s < snaps; s++) {
        CHECK_EQ(gba_rewind_step_back(g), 1);
        CHECK(same_as(g, kept[7 - s]));
    }
    CHECK_EQ(gba_rewind_step_back(g), 0);
    free_all(kept, 8);
    gba_destroy(g);
}

TEST(rewind_history_is_forgotten_by_a_loaded_state_a_reset_and_configuring) {
    gba_t *g = tu_arm(counter_ops, TU_LEN(counter_ops));
    CHECK_EQ(gba_rewind_configure(g, BUDGET, EVERY), GBA_OK);
    uint8_t *kept[MAX_SNAPS];
    run_keeping(g, 3, kept);
    size_t snaps, bytes;
    CHECK_EQ(gba_state_load(g, kept[0], gba_state_size(g)), GBA_OK);
    CHECK_EQ(gba_rewind_step_back(g), 0);
    gba_debug_rewind_stats(g, &snaps, &bytes);
    CHECK_EQ(snaps, 0);
    free_all(kept, 3);

    run_keeping(g, 3, kept);
    gba_reset(g);
    CHECK_EQ(gba_rewind_step_back(g), 0);
    free_all(kept, 3);

    run_keeping(g, 3, kept);
    CHECK_EQ(gba_rewind_configure(g, BUDGET, EVERY), GBA_OK);
    CHECK_EQ(gba_rewind_step_back(g), 0);
    free_all(kept, 3);
    gba_destroy(g);
}

TEST(rewind_off_without_a_budget) {
    gba_t *g = tu_arm(counter_ops, TU_LEN(counter_ops));
    for (int f = 0; f < 8; f++) gba_run_frame(g);
    CHECK_EQ(gba_rewind_step_back(g), 0); /* never configured */
    CHECK_EQ(gba_rewind_configure(g, BUDGET, EVERY), GBA_OK);
    CHECK_EQ(gba_rewind_configure(g, 0, EVERY), GBA_OK);
    for (int f = 0; f < 8; f++) gba_run_frame(g);
    CHECK_EQ(gba_rewind_step_back(g), 0);
    size_t snaps, bytes;
    gba_debug_rewind_stats(g, &snaps, &bytes);
    CHECK_EQ(snaps, 0);
    gba_destroy(g);
}
