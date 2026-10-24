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
