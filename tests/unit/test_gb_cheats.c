/* Cheats for Game Boy games (17c): Game Genie (a ROM byte reads as another) and GameShark (a RAM byte is written
 * every frame). */
#include <stdlib.h>
#include <string.h>

#include "gbtestutil.h"
#include "test.h"

static gb_cheat_type_t check(const char *text, gb_cheat_type_t type, int *bad) {
    return gb_cheat_check(text, type, bad);
}

static gb_err_t set1(gb_t *g, const char *text) {
    gb_cheat_type_t type = GB_CHEAT_AUTO;
    return gb_cheats_set(g, &text, &type, 1);
}

/* A machine whose game writes 0x55 to 0xC123 and 0xD123 for ever: LD A,0x55; LD (0xC123),A; LD (0xD123),A; JR. */
static gb_t *writer(uint8_t cart_type, uint8_t ram_code, uint8_t cgb) {
    static const uint8_t code[] = {0x3E, 0x55, 0xEA, 0x23, 0xC1, 0xEA, 0x23, 0xD1, 0x18, 0xF6};
    uint8_t *rom = gbtu_rom(0x10000, cart_type, ram_code, cgb);
    memcpy(rom + 0x150, code, sizeof code);
    for (int bank = 1; bank < 4; bank++) rom[bank * 0x4000 + 0x100] = bank == 2 ? 0x22 : 0x11;
    rom[0x0200] = 0x33;
    gbtu_fix_checksum(rom);
    gb_t *g = gb_create();
    CHECK_EQ(gb_load_rom(g, rom, 0x10000), GB_OK);
    free(rom);
    return g;
}

TEST(gb_cheat_reads_game_genie) {
    int bad = -1;
    CHECK_EQ(check("00A-17B-C49", GB_CHEAT_AUTO, &bad), GB_CHEAT_GAME_GENIE);
    CHECK_EQ(bad, 0);
    CHECK_EQ(check("00A-17B", GB_CHEAT_AUTO, &bad), GB_CHEAT_GAME_GENIE);
    CHECK_EQ(bad, 0);
    CHECK_EQ(check("00a17bc49", GB_CHEAT_AUTO, &bad), GB_CHEAT_GAME_GENIE); /* no dashes, small letters */
    CHECK_EQ(bad, 0);
    CHECK_EQ(check("  00A-17B-C49  \n\n 3EA-17B \n", GB_CHEAT_GAME_GENIE, &bad), GB_CHEAT_GAME_GENIE);
    CHECK_EQ(bad, 0);
    check("00A-17B-C49\n00A-17", GB_CHEAT_AUTO, &bad);
    CHECK_EQ(bad, 2);
    check("00A-17G", GB_CHEAT_AUTO, &bad); /* G is no hex digit */
    CHECK_EQ(bad, 1);
    check("00A-170-C49", GB_CHEAT_AUTO, &bad); /* its address is 0xFA17: not in the ROM */
    CHECK_EQ(bad, 1);
    check("", GB_CHEAT_AUTO, &bad); /* nothing is no cheat */
    CHECK_EQ(bad, 1);
}

TEST(gb_cheat_reads_gameshark) {
    int bad = -1;
    CHECK_EQ(check("01AA23C1", GB_CHEAT_AUTO, &bad), GB_CHEAT_GAMESHARK);
    CHECK_EQ(bad, 0);
    CHECK_EQ(check("01aa23c1\n91FF00D0", GB_CHEAT_GAMESHARK, &bad), GB_CHEAT_GAMESHARK);
    CHECK_EQ(bad, 0);
    check("01AA2380", GB_CHEAT_AUTO, &bad); /* 0x8023: video RAM is not written */
    CHECK_EQ(bad, 1);
    check("01AA00FF", GB_CHEAT_AUTO, &bad); /* 0xFF00: a register */
    CHECK_EQ(bad, 1);
    check("01AA23C1", GB_CHEAT_GAME_GENIE, &bad); /* the wrong kind for the chosen type */
    CHECK_EQ(bad, 1);
    check("00A-17B-C49", GB_CHEAT_GAMESHARK, &bad);
    CHECK_EQ(bad, 1);
}

TEST(gb_cheat_plus_joins_codes_on_a_line) {
    int bad = -1;
    CHECK_EQ(check("01AA23C1+01BB24C1", GB_CHEAT_AUTO, &bad), GB_CHEAT_GAMESHARK);
    CHECK_EQ(bad, 0);
    check("01AA23C1\n01BB24C1+XYZ", GB_CHEAT_AUTO, &bad);
    CHECK_EQ(bad, 2);
    gb_t *g = writer(0x1B, 2, 0);
    CHECK_EQ(set1(g, "01AA23C1 + 01BB24C1"), GB_OK);
    gbtu_frames(g, 1);
    CHECK_EQ(gb_debug_peek(g, 0xC123), 0xAA);
    CHECK_EQ(gb_debug_peek(g, 0xC124), 0xBB);
    gb_destroy(g);
}

TEST(gb_cheat_genie_patches_only_matching_banks_and_is_undone) {
    gb_t *g = writer(0x1B, 2, 0); /* MBC5: banks 1-3 hold 0x11, 0x22, 0x11 at 0x4100 */
    /* 0x4100 reads 0x99 where it was 0x11: value 99, address 4100 (its top digit XOR F = B), compare A?E. */
    CHECK_EQ(set1(g, "991-00B-A0E"), GB_OK);
    static const uint8_t with[] = {0x99, 0x22, 0x99}, without[] = {0x11, 0x22, 0x11}, all[] = {0x77, 0x77, 0x77};
    for (int bank = 1; bank < 4; bank++) {
        gb_bus_write(g, 0x2000, (uint8_t)bank);
        CHECK_EQ(gb_debug_peek(g, 0x4100), with[bank - 1]);
    }
    CHECK_EQ(gb_debug_peek(g, 0x0100), 0x00); /* bank 0 is left alone by a code for 0x4000-0x7FFF */
    CHECK_EQ(set1(g, "771-00B"), GB_OK); /* the old cheat is taken back first; six digits: every bank */
    for (int bank = 1; bank < 4; bank++) {
        gb_bus_write(g, 0x2000, (uint8_t)bank);
        CHECK_EQ(gb_debug_peek(g, 0x4100), all[bank - 1]);
    }
    CHECK_EQ(gb_cheats_set(g, NULL, NULL, 0), GB_OK);
    for (int bank = 1; bank < 4; bank++) {
        gb_bus_write(g, 0x2000, (uint8_t)bank);
        CHECK_EQ(gb_debug_peek(g, 0x4100), without[bank - 1]);
    }
    gb_destroy(g);
}

TEST(gb_cheat_genie_in_bank0_needs_its_compare) {
    gb_t *g = writer(0x1B, 2, 0); /* 0x0200 holds 0x33 */
    CHECK_EQ(set1(g, "992-00F"), GB_OK); /* value 99 at 0x0200; six digits: no compare */
    CHECK_EQ(gb_debug_peek(g, 0x0200), 0x99);
    gb_cheats_set(g, NULL, NULL, 0);
    /* compare 0x33: (0x33 ^ 0xBA) = 0x89, rotated left by 2 = 0x26 → digits 2 ? 6 */
    CHECK_EQ(set1(g, "992-00F-206"), GB_OK);
    CHECK_EQ(gb_debug_peek(g, 0x0200), 0x99);
    gb_cheats_set(g, NULL, NULL, 0);
    /* compare 0x34: (0x34 ^ 0xBA) = 0x8E, rotated left by 2 = 0x3A → 3 ? A: does not fit, nothing changes */
    CHECK_EQ(set1(g, "992-00F-30A"), GB_OK);
    CHECK_EQ(gb_debug_peek(g, 0x0200), 0x33);
    gb_destroy(g);
}

TEST(gb_cheat_gameshark_holds_a_byte_against_the_game) {
    gb_t *g = writer(0x1B, 2, 0);
    gbtu_frames(g, 2);
    CHECK_EQ(gb_debug_peek(g, 0xC123), 0x55);
    CHECK_EQ(set1(g, "01AA23C1\n010180FF"), GB_OK); /* work RAM and high RAM (0xFF80) */
    gbtu_frames(g, 1);
    CHECK_EQ(gb_debug_peek(g, 0xC123), 0xAA);
    CHECK_EQ(gb_debug_peek(g, 0xFF80), 0x01);
    gb_cheats_set(g, NULL, NULL, 0);
    gbtu_frames(g, 1);
    CHECK_EQ(gb_debug_peek(g, 0xC123), 0x55);
    gb_destroy(g);
}

TEST(gb_cheat_gameshark_bank_digit_picks_the_work_ram_bank_on_color) {
    gb_t *g = writer(0x1B, 2, 0x80);
    CHECK_EQ(set1(g, "93AA23D1\n01BB24D1\n90CC25D1"), GB_OK); /* bank 3; the mapped bank; "bank 0" = bank 1 */
    gbtu_frames(g, 1);
    CHECK_EQ(gb_wram(g)[3 * 0x1000 + 0x123], 0xAA);
    CHECK_EQ(gb_wram(g)[1 * 0x1000 + 0x124], 0xBB); /* bank 1 is mapped after reset */
    CHECK_EQ(gb_wram(g)[1 * 0x1000 + 0x125], 0xCC);
    CHECK_EQ(gb_wram(g)[1 * 0x1000 + 0x123], 0x55); /* the game's own write, untouched */
    gb_destroy(g);
    g = writer(0x1B, 2, 0x00); /* a plain Game Boy has one bank there: the digit means nothing */
    CHECK_EQ(set1(g, "93AA23D1"), GB_OK);
    gbtu_frames(g, 1);
    CHECK_EQ(gb_debug_peek(g, 0xD123), 0xAA);
    gb_destroy(g);
}

TEST(gb_cheat_gameshark_cart_ram_bank_dirty_and_missing) {
    gb_t *g = writer(0x1B, 3, 0); /* MBC5 + RAM + battery, 32 KiB = 4 banks */
    gb_save_dirty(g);
    CHECK_EQ(set1(g, "82AA10A0\n01BB11A0\n8FCC12A0"), GB_OK); /* bank 2; the mapped bank (0); bank 15: not there */
    gbtu_frames(g, 1);
    CHECK_EQ(gb_save_dirty(g), 1);
    size_t n = gb_save_size(g);
    uint8_t *save = malloc(n);
    CHECK_EQ(gb_save_write(g, save, n), n);
    CHECK_EQ(save[2 * 0x2000 + 0x10], 0xAA);
    CHECK_EQ(save[0x11], 0xBB);
    int stray = 0;
    for (size_t i = 0; i < n; i++) stray += save[i] == 0xCC;
    CHECK_EQ(stray, 0);
    free(save);
    gb_destroy(g);
    g = writer(0x19, 0, 0); /* no cartridge RAM at all: the code is read, nothing is written, nothing breaks */
    CHECK_EQ(set1(g, "01BB11A0"), GB_OK);
    gbtu_frames(g, 1);
    CHECK_EQ(gb_save_dirty(g), 0);
    gb_destroy(g);
}

TEST(gb_cheats_are_not_in_states_and_survive_loads_and_rewind) {
    gb_t *a = writer(0x1B, 2, 0), *b = writer(0x1B, 2, 0);
    CHECK_EQ(set1(b, "991-00B-A0E"), GB_OK); /* a ROM patch alone changes nothing a state holds */
    gbtu_frames(a, 3);
    gbtu_frames(b, 3);
    size_t n = gb_state_size(a);
    uint8_t *sa = malloc(n), *sb = malloc(n);
    CHECK_EQ(gb_state_save(a, sa, n), n);
    CHECK_EQ(gb_state_save(b, sb, n), n);
    CHECK(memcmp(sa, sb, n) == 0);
    CHECK_EQ(set1(b, "991-00B-A0E\n01AA23C1"), GB_OK);
    CHECK_EQ(gb_state_load(b, sa, n), GB_OK); /* a state made without cheats: they stay on */
    CHECK_EQ(gb_debug_peek(b, 0x4100), 0x99);
    gbtu_frames(b, 1);
    CHECK_EQ(gb_debug_peek(b, 0xC123), 0xAA);
    CHECK_EQ(gb_rewind_configure(b, 1 << 20, 1), GB_OK);
    gbtu_frames(b, 4);
    CHECK_EQ(gb_rewind_step_back(b), 1);
    CHECK_EQ(gb_debug_peek(b, 0x4100), 0x99);
    gbtu_frames(b, 1);
    CHECK_EQ(gb_debug_peek(b, 0xC123), 0xAA);
    gb_reset(b); /* a reset is the same game: still on */
    CHECK_EQ(gb_debug_peek(b, 0x4100), 0x99);
    free(sa);
    free(sb);
    gb_destroy(a);
    gb_destroy(b);
}

TEST(gb_cheats_a_bad_text_leaves_none_on_and_a_new_game_starts_clean) {
    gb_t *g = writer(0x1B, 2, 0);
    const char *texts[] = {"991-00B-A0E", "nonsense"};
    gb_cheat_type_t types[] = {GB_CHEAT_AUTO, GB_CHEAT_AUTO};
    CHECK_EQ(gb_cheats_set(g, texts, types, 2), GB_ERR_CHEAT);
    CHECK_EQ(gb_debug_peek(g, 0x4100), 0x11);
    CHECK_EQ(gb_cheats_set(g, texts, types, 1), GB_OK);
    CHECK_EQ(gb_debug_peek(g, 0x4100), 0x99);
    uint8_t *rom = gbtu_rom(0x8000, 0, 0, 0); /* another game: the old one's cheats are gone, its ROM untouched */
    rom[0x4100] = 0x11;
    CHECK_EQ(gb_load_rom(g, rom, 0x8000), GB_OK);
    free(rom);
    CHECK_EQ(gb_debug_peek(g, 0x4100), 0x11);
    gbtu_frames(g, 1);
    /* too many codes: refused */
    char *many = malloc(300 * 9 + 1);
    many[0] = 0;
    for (int i = 0; i < 300; i++) strcat(many, "01AA23C1\n");
    CHECK_EQ(set1(g, many), GB_ERR_CHEAT);
    free(many);
    gb_destroy(g);
}
