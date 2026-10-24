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

TEST(save_file_replace_swaps_in_the_new_bytes_and_keeps_no_backup) {
    clean();
    const uint8_t first[4] = {1, 2, 3, 4}, second[4] = {9, 8, 7, 6};
    CHECK(save_file_replace(SAV, first, 4));
    CHECK(save_file_replace(SAV, second, 4));
    CHECK(same(SAV, second, 4));
    size_t size = 0;
    CHECK(save_file_load(SAV ".bak", &size) == NULL);
    CHECK(save_file_load(SAV ".tmp", &size) == NULL);
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
