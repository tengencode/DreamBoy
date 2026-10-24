/* The warning log (milestone 13). Only in builds with GBA_NOTES (build/host); the phone's unit tests skip it. */
#ifdef GBA_NOTES
#include <string.h>

#include "bios/hle.h"
#include "gba_debug.h"
#include "test.h"
#include "testutil.h"

static gba_t *spinning(void) {
    const uint32_t ops[] = {0xEAFFFFFE}; /* b . */
    return tu_arm(ops, TU_LEN(ops));
}

/* The entry of that kind (and detail, unless detail is ANY), or NULL. */
#define ANY 0xFFFFFFFFu
static const gba_note_t *find(const gba_t *g, const char *kind, uint32_t detail) {
    static gba_note_t notes[GBA_DEBUG_NOTES_MAX];
    size_t n = gba_debug_notes(g, notes, GBA_DEBUG_NOTES_MAX);
    for (size_t i = 0; i < n; i++)
        if (!strcmp(notes[i].kind, kind) && (detail == ANY || notes[i].detail == detail)) return &notes[i];
    return NULL;
}

static size_t note_count(const gba_t *g) {
    gba_note_t notes[GBA_DEBUG_NOTES_MAX];
    return gba_debug_notes(g, notes, GBA_DEBUG_NOTES_MAX);
}

TEST(notes_are_on_in_this_build_and_empty_after_power_on) {
    CHECK_EQ(gba_debug_notes_enabled(), 1);
    gba_t *g = spinning();
    gba_run_frame(g);
    gba_run_frame(g);
    CHECK_EQ(note_count(g), 0); /* a plain program: nothing */
    gba_destroy(g);
}

TEST(notes_keep_each_bios_call_once_with_its_first_frame_and_count) {
    gba_t *g = spinning();
    gba_run_frame(g);
    gba_run_frame(g);
    hle_swi(g, 0x1A); /* SoundDriverInit: skipped by the built-in BIOS */
    hle_swi(g, 0x1A);
    hle_swi(g, 0x20);
    const gba_note_t *n = find(g, "bios-call", 0x1A);
    CHECK(n != NULL);
    if (n) {
        CHECK_EQ(n->count, 2);
        CHECK_EQ(n->first_frame, 2);
    }
    CHECK(find(g, "bios-call", 0x20) != NULL);
    hle_swi(g, 0x0D); /* GetBiosChecksum is done by the built-in BIOS: no note */
    CHECK_EQ(note_count(g), 2);
    gba_destroy(g);
}

TEST(notes_see_picture_writes_only_while_a_visible_line_is_drawn) {
    gba_t *g = spinning();
    g->ppu.vcount = 10;
    g->ppu.dispstat &= (uint16_t)~DISPSTAT_HBLANK;
    tu_io16(g, 0x010, 5); /* BG0HOFS while drawing */
    tu_pal(g, 1, 0x7FFF);
    CHECK(find(g, "midline-io", 0x010) != NULL);
    CHECK(find(g, "midline-palette", ANY) != NULL);
    tu_io16(g, 0x004, 0); /* DISPSTAT is not a picture register */
    CHECK(find(g, "midline-io", 0x004) == NULL);
    g->ppu.dispstat |= DISPSTAT_HBLANK; /* H-blank: fine */
    tu_io16(g, 0x012, 5);
    g->ppu.dispstat &= (uint16_t)~DISPSTAT_HBLANK;
    g->ppu.vcount = 200; /* V-blank: fine */
    tu_io16(g, 0x014, 5);
    g->ppu.vcount = 10;
    g->ppu.dispcnt = 0x0080; /* forced blank: nothing is drawn */
    tu_io16(g, 0x016, 5);
    CHECK(find(g, "midline-io", 0x012) == NULL);
    CHECK(find(g, "midline-io", 0x014) == NULL);
    CHECK(find(g, "midline-io", 0x016) == NULL);
    gba_destroy(g);
}

TEST(notes_see_palette_stores_from_the_fast_cpu_loop) {
    uint32_t ops[0x44] = {0xEA00003E}; /* b 0x08000100: the fast loop leaves the cartridge header to cpu_step */
    ops[0x40] = 0xE3A00405;              /* mov r0, #0x05000000 */
    ops[0x41] = 0xE3A01C7F;              /* mov r1, #0x7F00 */
    ops[0x42] = 0xE5801000;              /* str r1, [r0] */
    ops[0x43] = 0xEAFFFFFD;              /* b to the str */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    g->ppu.dispcnt = 0; /* not forced blank: lines are drawn */
    gba_run_frame(g); /* the loop runs all frame, mostly through the fast path */
    const gba_note_t *n = find(g, "midline-palette", ANY);
    CHECK(n != NULL);
    if (n) CHECK(n->count > 3000); /* every store while a visible line is drawn, not only the slow-path ones */
    gba_destroy(g);
}

TEST(notes_see_green_swap_and_video_capture) {
    gba_t *g = spinning();
    g->ppu.vcount = 200;
    tu_io16(g, 0x002, 0); /* off: fine */
    CHECK(find(g, "green-swap", ANY) == NULL);
    tu_io16(g, 0x002, 1);
    CHECK(find(g, "green-swap", ANY) != NULL);
    tu_io16(g, 0x0DE, 0xB000); /* DMA3 on, timing 3 (special) = video capture */
    CHECK(find(g, "video-capture", ANY) != NULL);
    gba_destroy(g);
}

TEST(notes_see_writes_to_io_addresses_without_a_register) {
    gba_t *g = spinning();
    g->ppu.vcount = 200;
    tu_io32(g, 0x064, 0); /* SOUND1CNT_X + the unused 0x066 in one word: fine */
    tu_io16(g, 0x050, 0); /* BLDCNT: fine */
    CHECK_EQ(note_count(g), 0);
    tu_io16(g, 0x0E0, 0);
    const gba_note_t *n = find(g, "io-unused", ANY);
    CHECK(n != NULL);
    if (n) CHECK_EQ(n->detail, 0x0E0);
    bus_write32(g, 0x04000800, 0x0D000020, false); /* memory control: ignored by the emulator */
    n = find(g, "io-unused", ANY);
    if (n) CHECK_EQ(n->count, 2); /* one entry for the kind; the first offset kept */
    gba_destroy(g);
}

TEST(notes_see_code_running_from_empty_memory) {
    const uint32_t ops[] = {
        0xE3A00201, /* mov r0, #0x10000000 */
        0xE12FFF10, /* bx r0 */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    tu_steps(g, 3);
    const gba_note_t *n = find(g, "exec-empty", ANY);
    CHECK(n != NULL);
    if (n) CHECK_EQ(n->detail, 0x10000000);
    gba_destroy(g);
}

TEST(notes_see_undefined_instructions) {
    const uint32_t ops[] = {0xE7F000F0}; /* undefined (the HLE trap's code, but not in the BIOS) */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    tu_steps(g, 1);
    const gba_note_t *n = find(g, "undefined", ANY);
    CHECK(n != NULL);
    if (n) CHECK_EQ(n->detail, 0x08000000);
    gba_destroy(g);
    const uint16_t tops[] = {0xDE00}; /* Thumb undefined */
    g = tu_thumb(tops, TU_LEN(tops));
    tu_steps(g, 1);
    n = find(g, "undefined", ANY);
    CHECK(n != NULL);
    if (n) CHECK_EQ(n->detail, 0x08000000);
    gba_destroy(g);
}

/* A message through mGBA's debug-print registers: the text at 0x04FFF600, then level | 0x100 at 0x04FFF700. */
static void game_print(gba_t *g, const char *text, uint16_t level) {
    for (size_t i = 0; text[i]; i++) bus_write8(g, 0x04FFF600 + (uint32_t)i, (uint8_t)text[i], false);
    bus_write16(g, 0x04FFF700, (uint16_t)(0x100 | level), false);
}

TEST(notes_keep_the_games_own_debug_prints) {
    gba_t *g = spinning();
    gba_run_frame(g);
    bus_write16(g, 0x04FFF780, 0xC0DE, false); /* "is this mGBA?" */
    game_print(g, "ASSERTION FAILED", 1);
    game_print(g, "ASSERTION FAILED", 1);
    game_print(g, "hello", 4);
    gba_print_t p[4];
    CHECK_EQ(gba_debug_prints(g, p, 4), 2);
    CHECK(!strcmp(p[0].text, "ASSERTION FAILED"));
    CHECK_EQ(p[0].level, 1);
    CHECK_EQ(p[0].count, 2);
    CHECK_EQ(p[0].first_frame, 1);
    CHECK(!strcmp(p[1].text, "hello")); /* the area is cleared after each message */
    CHECK(find(g, "io-unused", ANY) == NULL); /* the print registers are understood, not unused */
    gba_reset(g);
    CHECK_EQ(gba_debug_prints(g, p, 4), 0);
    gba_destroy(g);
}

TEST(notes_a_full_log_counts_what_it_leaves_out_and_a_reset_clears_it) {
    gba_t *g = spinning();
    for (uint32_t k = 0; k < GBA_DEBUG_NOTES_MAX + 5; k++) hle_swi(g, 0x40 + k); /* unknown calls */
    CHECK_EQ(note_count(g), GBA_DEBUG_NOTES_MAX);
    const gba_note_t *n = find(g, "notes-dropped", ANY);
    CHECK(n != NULL);
    if (n) CHECK_EQ(n->count, 6); /* 63 kept, 6 left out */
    gba_reset(g);
    CHECK_EQ(note_count(g), 0);
    gba_destroy(g);
}
#endif
