/* Cheats: what the steps do (core/src/cheats/apply.c). The Action Replay cases are mGBA's tests (same codes, same
 * expected memory) rewritten here, scrambled first so the whole path is used. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cheats/cheats.h"
#include "gba_internal.h"
#include "test.h"
#include "testutil.h"

static gba_t *machine(void) {
    static const uint32_t spin[] = {0xEAFFFFFE}; /* b . : the program never touches memory */
    return tu_arm(spin, TU_LEN(spin));
}

static uint32_t peek(gba_t *g, uint32_t addr, unsigned width) {
    return bus_peek(g, addr, width);
}

static void set(gba_t *g, const char *text, gba_cheat_type_t type) {
    const char *texts[1] = {text};
    CHECK_EQ(gba_cheats_set(g, texts, &type, 1), GBA_OK);
}

/* Plain Action Replay / GameShark codes → scrambled text, one per line. */
static char *scrambled_text(const uint32_t (*codes)[2], size_t n, const uint32_t seeds[4]) {
    char *text = malloc(n * 18 + 1), *p = text;
    for (size_t i = 0; i < n; i++) {
        uint32_t a = codes[i][0], b = codes[i][1];
        cheat_tea_encrypt(&a, &b, seeds);
        p += sprintf(p, "%08X %08X\n", (unsigned)a, (unsigned)b);
    }
    *p = '\0';
    return text;
}

static void set_ar(gba_t *g, const uint32_t (*codes)[2], size_t n) {
    char *text = scrambled_text(codes, n, CHEAT_AR3_SEEDS);
    set(g, text, GBA_CHEAT_ACTION_REPLAY);
    free(text);
}

#define IW 0x03000000u

TEST(cheat_ar_writes_bytes_halfwords_words) {
    static const uint32_t c[][2] = {{0x00300000, 0x00000078}, {0x02300002, 0x00005678}, {0x04300004, 0x12345678}};
    gba_t *g = machine();
    set_ar(g, c, TU_LEN(c));
    CHECK_EQ(peek(g, IW, 1), 0);
    cheats_apply(g);
    CHECK_EQ(peek(g, IW, 1), 0x78);
    CHECK_EQ(peek(g, IW + 2, 2), 0x5678);
    CHECK_EQ(peek(g, IW + 4, 4), 0x12345678);
    gba_destroy(g);
}

TEST(cheat_ar_fills_with_steps) {
    static const uint32_t c1[][2] = {{0x00000000, 0x80300000}, {0x00000001, 0x01020002}};
    static const uint32_t c4[][2] = {{0x00000000, 0x84300000}, {0x00000001, 0x01020002}};
    gba_t *g = machine();
    set_ar(g, c1, TU_LEN(c1));
    cheats_apply(g);
    CHECK_EQ(peek(g, IW, 1), 1);
    CHECK_EQ(peek(g, IW + 1, 1), 0);
    CHECK_EQ(peek(g, IW + 2, 1), 2);
    CHECK_EQ(peek(g, IW + 3, 1), 0);
    gba_reset(g);
    set_ar(g, c4, TU_LEN(c4));
    cheats_apply(g);
    CHECK_EQ(peek(g, IW, 2), 1);
    CHECK_EQ(peek(g, IW + 4, 2), 0);
    CHECK_EQ(peek(g, IW + 8, 2), 2);
    CHECK_EQ(peek(g, IW + 12, 2), 0);
    gba_destroy(g);
}

TEST(cheat_ar_if_guards_the_next_code) {
    static const uint32_t c[][2] = {
        {0x00300001, 0x00000011}, {0x08300000, 0x00000000}, {0x00300001, 0x00000012}, {0x00300000, 0x00000001}};
    gba_t *g = machine();
    set_ar(g, c, TU_LEN(c));
    cheats_apply(g);
    CHECK_EQ(peek(g, IW, 1), 1);
    CHECK_EQ(peek(g, IW + 1, 1), 0x12);
    gba_reset(g);
    bus_poke(g, IW, 1, 1);
    cheats_apply(g);
    CHECK_EQ(peek(g, IW + 1, 1), 0x11);
    gba_destroy(g);
}

TEST(cheat_ar_if_blocks_with_and_without_else) {
    static const uint32_t block[][2] = {{0x00300001, 0x00000011}, {0x88300000, 0x00000000}, {0x00300001, 0x00000012},
                                        {0x00000000, 0x40000000}, {0x00300000, 0x00000001}};
    static const uint32_t with_else[][2] = {{0x00300001, 0x00000011}, {0x00300002, 0x00000021},
                                            {0x88300000, 0x00000000}, {0x00300001, 0x00000012},
                                            {0x00000000, 0x60000000}, {0x00300002, 0x00000022},
                                            {0x00000000, 0x40000000}, {0x00300000, 0x00000001}};
    gba_t *g = machine();
    set_ar(g, block, TU_LEN(block));
    cheats_apply(g);
    CHECK_EQ(peek(g, IW + 1, 1), 0x12);
    gba_reset(g);
    bus_poke(g, IW, 1, 1);
    cheats_apply(g);
    CHECK_EQ(peek(g, IW + 1, 1), 0x11);

    gba_reset(g);
    set_ar(g, with_else, TU_LEN(with_else));
    cheats_apply(g);
    CHECK_EQ(peek(g, IW + 1, 1), 0x12);
    CHECK_EQ(peek(g, IW + 2, 1), 0x21);
    gba_reset(g);
    bus_poke(g, IW, 1, 1);
    cheats_apply(g);
    CHECK_EQ(peek(g, IW + 1, 1), 0x11);
    CHECK_EQ(peek(g, IW + 2, 1), 0x22);
    gba_destroy(g);
}

TEST(cheat_ar_nested_conditions_inside_if_and_else) {
    static const uint32_t c[][2] = {
        {0x00300003, 0x00000031}, {0x00300004, 0x00000041}, {0x00300005, 0x00000051}, {0x00300006, 0x00000061},
        {0x00300007, 0x00000071}, {0x00300008, 0x00000081}, {0x88300000, 0x00000000}, {0x00300003, 0x00000032},
        {0x08300001, 0x00000000}, {0x00300004, 0x00000042}, {0x00300005, 0x00000052}, {0x00000000, 0x60000000},
        {0x00300006, 0x00000062}, {0x08300002, 0x00000000}, {0x00300007, 0x00000072}, {0x00300008, 0x00000082},
        {0x00000000, 0x40000000}, {0x00300000, 0x00000001}};
    /* {IW+0, IW+1, IW+2} before → the bytes at IW+3..IW+8 after (mGBA's expected values) */
    static const struct {
        uint8_t in[3];
        uint8_t out[6];
    } cases[] = {
        {{0, 0, 0}, {0x32, 0x42, 0x52, 0x61, 0x71, 0x81}}, {{1, 0, 0}, {0x31, 0x41, 0x51, 0x62, 0x72, 0x82}},
        {{0, 1, 0}, {0x32, 0x41, 0x52, 0x61, 0x71, 0x81}}, {{0, 0, 1}, {0x32, 0x42, 0x52, 0x61, 0x71, 0x81}},
        {{1, 1, 0}, {0x31, 0x41, 0x51, 0x62, 0x72, 0x82}}, {{1, 0, 1}, {0x31, 0x41, 0x51, 0x62, 0x71, 0x82}},
        {{0, 1, 1}, {0x32, 0x41, 0x52, 0x61, 0x71, 0x81}}, {{1, 1, 1}, {0x31, 0x41, 0x51, 0x62, 0x71, 0x82}},
    };
    gba_t *g = machine();
    set_ar(g, c, TU_LEN(c));
    for (size_t k = 0; k < TU_LEN(cases); k++) {
        gba_reset(g);
        for (int i = 0; i < 3; i++) bus_poke(g, IW + (uint32_t)i, cases[k].in[i], 1);
        cheats_apply(g);
        for (int i = 0; i < 6; i++) CHECK_EQ(peek(g, IW + 3 + (uint32_t)i, 1), cases[k].out[i]);
    }
    gba_destroy(g);
}

TEST(cheat_ar_two_code_condition_and_never) {
    static const uint32_t two[][2] = {{0x00300002, 0x00000021}, {0x48300000, 0x00000000}, {0x08300001, 0x00000000},
                                      {0x00300002, 0x00000022}, {0x00300000, 0x00000001}};
    static const uint32_t never1[][2] = {
        {0x00300001, 0x00000011}, {0x0E300000, 0x00000000}, {0x00300001, 0x00000012}, {0x00300000, 0x00000001}};
    static const uint32_t never_block[][2] = {{0x00300001, 0x00000011}, {0x8E300000, 0x00000000},
                                              {0x00300001, 0x00000012}, {0x00000000, 0x40000000},
                                              {0x00300000, 0x00000001}};
    gba_t *g = machine();
    set_ar(g, two, TU_LEN(two));
    cheats_apply(g);
    CHECK_EQ(peek(g, IW + 2, 1), 0x22);
    gba_reset(g);
    bus_poke(g, IW + 1, 1, 1);
    cheats_apply(g);
    CHECK_EQ(peek(g, IW + 2, 1), 0x21);
    for (int k = 0; k < 2; k++) {
        gba_reset(g);
        if (k) set_ar(g, never_block, TU_LEN(never_block));
        else set_ar(g, never1, TU_LEN(never1));
        cheats_apply(g);
        CHECK_EQ(peek(g, IW + 1, 1), 0x11);
    }
    gba_destroy(g);
}

TEST(cheat_ar_device_button_codes_never_fire_and_master_lines_are_skipped) {
    static const uint32_t c[][2] = {{0x00000000, 0x001DC0DE}, {0xC4000100, 0x00000000}, /* game id, hook */
                                    {0x00000000, 0x10300000}, {0x00000001, 0x00000000}, /* while the button... */
                                    {0x00300001, 0x00000007}};
    gba_t *g = machine();
    set_ar(g, c, TU_LEN(c));
    cheats_apply(g);
    CHECK_EQ(peek(g, IW, 1), 0);
    CHECK_EQ(peek(g, IW + 1, 1), 7);
    gba_destroy(g);
}

TEST(cheat_gameshark_writes_conditions_and_lists) {
    static const uint32_t c[][2] = {
        {0x03000010, 0x000000AB}, {0x13000012, 0x0000BEEF}, {0x23000014, 0xCAFEF00D}, /* write 1, 2, 4 */
        {0xD3000020, 0x00000005}, {0x03000021, 0x00000055},                           /* if [20] == 5 */
        {0xE0020005, 0x03000030}, {0x03000031, 0x00000031}, {0x03000032, 0x00000032}, /* if [30] == 5: 2 codes */
        {0x30000003, 0x11223344}, {0x03000040, 0x03000044}, {0x03000048, 0x00000000}, /* list: 3 addresses */
        {0xF0000100, 0x00000001}};                                                     /* hook */
    gba_t *g = machine();
    char *text = scrambled_text(c, TU_LEN(c), CHEAT_GS_SEEDS);
    set(g, text, GBA_CHEAT_GAMESHARK);
    cheats_apply(g);
    CHECK_EQ(peek(g, IW + 0x10, 1), 0xAB);
    CHECK_EQ(peek(g, IW + 0x12, 2), 0xBEEF);
    CHECK_EQ(peek(g, IW + 0x14, 4), 0xCAFEF00D);
    CHECK_EQ(peek(g, IW + 0x21, 1), 0);
    CHECK_EQ(peek(g, IW + 0x31, 1), 0);
    CHECK_EQ(peek(g, IW + 0x40, 4), 0x11223344);
    CHECK_EQ(peek(g, IW + 0x44, 4), 0x11223344);
    CHECK_EQ(peek(g, IW + 0x48, 4), 0x11223344);
    bus_poke(g, IW + 0x20, 5, 2);
    bus_poke(g, IW + 0x30, 5, 2);
    cheats_apply(g);
    CHECK_EQ(peek(g, IW + 0x21, 1), 0x55);
    CHECK_EQ(peek(g, IW + 0x31, 1), 0x31);
    CHECK_EQ(peek(g, IW + 0x32, 1), 0x32);
    free(text);
    gba_destroy(g);
}

TEST(cheat_codebreaker_kinds) {
    gba_t *g = machine();
    set(g,
        "0000AAAA 0001\n"  /* game id: skipped */
        "13000100 0007\n"  /* hook: skipped */
        "33000000 0063\n"  /* write byte */
        "83000002 1234\n"  /* write halfword */
        "43000010 0101\n"  /* fill: 0x0101 ... */
        "00010003 0002\n"  /* ... value +1, 3 times, address +2 */
        "E3000020 0010\n"  /* add */
        "23000022 00F0\n"  /* or */
        "63000024 0F0F\n"  /* and */
        "73000026 0000\n"  /* if [26] == 0 ... */
        "33000028 0042\n", /* ... this */
        GBA_CHEAT_CODEBREAKER);
    bus_poke(g, IW + 0x20, 0x0005, 2);
    bus_poke(g, IW + 0x22, 0x000F, 2);
    bus_poke(g, IW + 0x24, 0xFFFF, 2);
    cheats_apply(g);
    CHECK_EQ(peek(g, IW, 1), 0x63);
    CHECK_EQ(peek(g, IW + 2, 2), 0x1234);
    CHECK_EQ(peek(g, IW + 0x10, 2), 0x0101);
    CHECK_EQ(peek(g, IW + 0x12, 2), 0x0102);
    CHECK_EQ(peek(g, IW + 0x14, 2), 0x0103);
    CHECK_EQ(peek(g, IW + 0x16, 2), 0);
    CHECK_EQ(peek(g, IW + 0x20, 2), 0x0015);
    CHECK_EQ(peek(g, IW + 0x22, 2), 0x00FF);
    CHECK_EQ(peek(g, IW + 0x24, 2), 0x0F0F);
    CHECK_EQ(peek(g, IW + 0x28, 1), 0x42);
    gba_destroy(g);
}

TEST(cheat_codebreaker_while_gba_buttons_are_held) {
    gba_t *g = machine();
    set(g, "D0000020 0005\n33000000 0001\n", GBA_CHEAT_CODEBREAKER); /* A (bit 0) + SELECT (bit 2) held */
    cheats_apply(g);
    CHECK_EQ(peek(g, IW, 1), 0);
    gba_set_keys(g, GBA_KEY_A);
    cheats_apply(g);
    CHECK_EQ(peek(g, IW, 1), 0);
    gba_set_keys(g, GBA_KEY_A | GBA_KEY_SELECT);
    cheats_apply(g);
    CHECK_EQ(peek(g, IW, 1), 1);
    gba_destroy(g);
}

/* A condition belongs to its own cheat: one at the end of cheat A never guards cheat B, and an Action Replay
 * block with no "end if" guards the rest of its own cheat only. */
TEST(cheat_conditions_stay_inside_their_own_cheat) {
    static const uint32_t open_block[][2] = {{0x88300000, 0x00000000}, {0x00300001, 0x00000012}}; /* if [IW] == 0x00 (block) */
    char *ar = scrambled_text(open_block, TU_LEN(open_block), CHEAT_AR3_SEEDS);
    const char *texts[3] = {"73000026 0001\n", ar, "33000000 0042\n"}; /* CB: if [26] == 1, then nothing */
    gba_cheat_type_t types[3] = {GBA_CHEAT_CODEBREAKER, GBA_CHEAT_ACTION_REPLAY, GBA_CHEAT_CODEBREAKER};
    gba_t *g = machine();
    CHECK_EQ(gba_cheats_set(g, texts, types, 3), GBA_OK);
    bus_poke(g, IW, 0x05, 1); /* both conditions false */
    cheats_apply(g);
    CHECK_EQ(peek(g, IW + 1, 1), 0);   /* the open block's own code stays guarded */
    CHECK_EQ(peek(g, IW, 1), 0x42);    /* the last cheat runs regardless */
    free(ar);
    gba_destroy(g);
}

TEST(cheat_codebreaker_scrambled_codes) {
    cb_crypt_t c;
    const uint32_t key1 = 0x9123ABCD;
    const uint16_t key2 = 0x5678;
    cheat_cb_reseed(&c, key1, key2);
    uint32_t a1 = 0x33000000;
    uint16_t b1 = 0x0063;
    uint32_t a2 = 0x83000002;
    uint16_t b2 = 0x1234;
    cheat_cb_encrypt(&c, &a1, &b1);
    cheat_cb_encrypt(&c, &a2, &b2);
    char text[80];
    snprintf(text, sizeof text, "%08X %04X\n%08X %04X\n%08X %04X\n", (unsigned)key1, key2, (unsigned)a1, b1,
             (unsigned)a2, b2);
    gba_t *g = machine();
    set(g, text, GBA_CHEAT_AUTO);
    cheats_apply(g);
    CHECK_EQ(peek(g, IW, 1), 0x63);
    CHECK_EQ(peek(g, IW + 2, 2), 0x1234);
    gba_destroy(g);
}

TEST(cheat_rom_patches_are_undone_when_switched_off) {
    gba_t *g = machine();
    uint32_t before = peek(g, 0x08000100, 4);
    set(g, "08000100:BEEF\n08000102:1234\n02000000:12345678", GBA_CHEAT_RAW);
    CHECK_EQ(peek(g, 0x08000100, 2), 0xBEEF);
    CHECK_EQ(peek(g, 0x08000102, 2), 0x1234);
    cheats_apply(g);
    CHECK_EQ(peek(g, 0x02000000, 4), 0x12345678);
    CHECK_EQ(gba_cheats_set(g, NULL, NULL, 0), GBA_OK); /* all off */
    CHECK_EQ(peek(g, 0x08000100, 4), before);
    gba_destroy(g);
}

TEST(cheat_a_bad_text_leaves_no_cheats_and_undoes_the_old_patches) {
    gba_t *g = machine();
    uint32_t before = peek(g, 0x08000100, 2);
    set(g, "08000100:BEEF", GBA_CHEAT_RAW);
    const char *texts[2] = {"02000000:11", "not a code"};
    gba_cheat_type_t types[2] = {GBA_CHEAT_AUTO, GBA_CHEAT_AUTO};
    CHECK_EQ(gba_cheats_set(g, texts, types, 2), GBA_ERR_CHEAT);
    CHECK_EQ(peek(g, 0x08000100, 2), before);
    cheats_apply(g);
    CHECK_EQ(peek(g, 0x02000000, 1), 0);
    gba_destroy(g);
}

TEST(cheat_list_is_in_no_state_and_survives_a_loaded_state) {
    gba_t *a = machine(), *b = machine();
    set(a, "02000000:77", GBA_CHEAT_RAW);
    gba_run_frame(a); /* cheats run at the end of the frame */
    CHECK_EQ(peek(a, 0x02000000, 1), 0x77);
    size_t n = gba_state_size(a);
    uint8_t *st = malloc(n);
    CHECK_EQ(gba_state_save(a, st, n), n);
    CHECK_EQ(gba_state_load(b, st, n), GBA_OK); /* b has no cheats: the state brings none */
    bus_poke(b, 0x02000000, 0, 1);
    gba_run_frame(b);
    CHECK_EQ(peek(b, 0x02000000, 1), 0);
    bus_poke(a, 0x02000000, 0, 1);
    CHECK_EQ(gba_state_load(a, st, n), GBA_OK); /* a keeps its cheats */
    bus_poke(a, 0x02000000, 0, 1);
    gba_run_frame(a);
    CHECK_EQ(peek(a, 0x02000000, 1), 0x77);
    free(st);
    gba_destroy(a);
    gba_destroy(b);
}
