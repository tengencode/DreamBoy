/* Cheats: the ciphers (core/src/cheats/decrypt.c) and reading codes (decode.c). */
#include <stdio.h>
#include <string.h>

#include "cheats/cheats.h"
#include "test.h"

TEST(cheat_tea_matches_its_published_test_value) {
    static const uint32_t zero_key[4] = {0, 0, 0, 0};
    uint32_t a = 0, b = 0;
    cheat_tea_encrypt(&a, &b, zero_key);
    CHECK_EQ(a, 0x41EA3A0Au); /* the TEA reference's value for a zero key and a zero block */
    CHECK_EQ(b, 0x94BAA940u);
    cheat_tea_decrypt(&a, &b, zero_key);
    CHECK_EQ(a, 0);
    CHECK_EQ(b, 0);
}

TEST(cheat_tea_round_trips_with_both_device_keys) {
    static const uint32_t plain[][2] = {{0x00300000, 0x00000078}, {0x02300002, 0x00005678}, {0x00000000, 0x001DC0DE},
                                        {0x13000000, 0x0000FFFF}, {0xDEADBEEF, 0x12345678}};
    for (size_t i = 0; i < sizeof plain / sizeof plain[0]; i++) {
        for (int k = 0; k < 2; k++) {
            const uint32_t *seeds = k ? CHEAT_AR3_SEEDS : CHEAT_GS_SEEDS;
            uint32_t a = plain[i][0], b = plain[i][1];
            cheat_tea_encrypt(&a, &b, seeds);
            CHECK(a != plain[i][0] || b != plain[i][1]);
            cheat_tea_decrypt(&a, &b, seeds);
            CHECK_EQ(a, plain[i][0]);
            CHECK_EQ(b, plain[i][1]);
        }
    }
}

TEST(cheat_codebreaker_scrambling_round_trips) {
    static const uint32_t seeds[][2] = {{0x9123ABCD, 0x1234}, {0x90000000, 0x0000}, {0x9F00FF00, 0xFFFF}};
    for (size_t s = 0; s < sizeof seeds / sizeof seeds[0]; s++) {
        cb_crypt_t c;
        cheat_cb_reseed(&c, seeds[s][0], (uint16_t)seeds[s][1]);
        for (unsigned i = 0; i < sizeof c.table; i++) CHECK(c.table[i] < sizeof c.table);
        for (uint32_t i = 0; i < 50; i++) {
            uint32_t a = 0x32000000u + i * 0x01010101u, orig_a = a;
            uint16_t b = (uint16_t)(i * 0x1357), orig_b = b;
            cheat_cb_encrypt(&c, &a, &b);
            cheat_cb_decrypt(&c, &a, &b);
            CHECK_EQ(a, orig_a);
            CHECK_EQ(b, orig_b);
        }
    }
}

/* "XXXXXXXX YYYYYYYY" of a plain code scrambled with a device key. */
static void scrambled(char *out, size_t cap, uint32_t a, uint32_t b, const uint32_t seeds[4]) {
    cheat_tea_encrypt(&a, &b, seeds);
    snprintf(out, cap, "%08X %08X", (unsigned)a, (unsigned)b);
}

TEST(cheat_auto_detection_finds_each_format) {
    char line[40];
    int bad = -1;
    scrambled(line, sizeof line, 0x00300000, 0x00000078, CHEAT_AR3_SEEDS); /* AR: byte write to IWRAM */
    CHECK_EQ(gba_cheat_check(line, GBA_CHEAT_AUTO, &bad), GBA_CHEAT_ACTION_REPLAY);
    CHECK_EQ(bad, 0);
    scrambled(line, sizeof line, 0x03000000, 0x00000078, CHEAT_GS_SEEDS); /* GS: byte write to IWRAM */
    CHECK_EQ(gba_cheat_check(line, GBA_CHEAT_AUTO, &bad), GBA_CHEAT_GAMESHARK);
    CHECK_EQ(bad, 0);
    CHECK_EQ(gba_cheat_check("33000000 0063", GBA_CHEAT_AUTO, &bad), GBA_CHEAT_CODEBREAKER);
    CHECK_EQ(bad, 0);
    CHECK_EQ(gba_cheat_check("02000000:1234", GBA_CHEAT_AUTO, &bad), GBA_CHEAT_RAW);
    CHECK_EQ(bad, 0);
    CHECK_EQ(gba_cheat_check("  0300 0000 0063  \n", GBA_CHEAT_AUTO, &bad), GBA_CHEAT_CODEBREAKER); /* spaces anywhere */
    CHECK_EQ(bad, 0);
}

TEST(cheat_check_names_the_first_bad_line_counting_blank_ones) {
    int bad = -1;
    gba_cheat_check("33000000 0063\n\n33000001 0064\nnot a code\n", GBA_CHEAT_AUTO, &bad);
    CHECK_EQ(bad, 4);
    gba_cheat_check("33000000 0063\n02000000:12", GBA_CHEAT_AUTO, &bad); /* another format in the same cheat */
    CHECK_EQ(bad, 2);
    gba_cheat_check("33000000 00631", GBA_CHEAT_AUTO, &bad); /* 13 digits */
    CHECK_EQ(bad, 1);
    gba_cheat_check("02000000:123456", GBA_CHEAT_RAW, &bad); /* 3 bytes */
    CHECK_EQ(bad, 1);
    gba_cheat_check("", GBA_CHEAT_AUTO, &bad); /* nothing */
    CHECK_EQ(bad, 1);
    gba_cheat_check("\n\n", GBA_CHEAT_AUTO, &bad);
    CHECK_EQ(bad, 2);
    char line[40];
    scrambled(line, sizeof line, 0xDEADFACE, 0x00001234, CHEAT_AR3_SEEDS); /* re-keying: not supported */
    gba_cheat_check(line, GBA_CHEAT_ACTION_REPLAY, &bad);
    CHECK_EQ(bad, 1);
    CHECK_EQ(gba_cheat_check("33000000 0063", GBA_CHEAT_ACTION_REPLAY, &bad), GBA_CHEAT_ACTION_REPLAY);
    CHECK_EQ(bad, 1); /* the picked type decides: a CodeBreaker line is not an Action Replay code */
}
