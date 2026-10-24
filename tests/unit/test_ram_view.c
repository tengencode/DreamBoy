/* The work-RAM views (milestone 14, for RetroAchievements): what the game wrote, read straight. */
#include <stdlib.h>

#include "gba.h"
#include "mem/bus.h"
#include "test.h"
#include "testutil.h"

static gba_t *machine(void) {
    static const uint32_t spin[] = {0xEAFFFFFE}; /* b . : the program never touches memory */
    return tu_arm(spin, TU_LEN(spin));
}

TEST(ram_views_show_what_the_game_wrote_at_both_ends) {
    gba_t *g = machine();
    bus_write8(g, 0x03000005, 0x77, false);
    bus_write8(g, 0x03007FFF, 0x12, false);
    bus_write8(g, 0x02000000, 0x34, false);
    bus_write8(g, 0x0203FFFF, 0x56, false);
    CHECK_EQ(gba_iwram(g)[5], 0x77);
    CHECK_EQ(gba_iwram(g)[GBA_IWRAM_SIZE - 1], 0x12);
    CHECK_EQ(gba_ewram(g)[0], 0x34);
    CHECK_EQ(gba_ewram(g)[GBA_EWRAM_SIZE - 1], 0x56);
    gba_destroy(g);
}

TEST(ram_views_follow_mirrors_and_state_loads) {
    gba_t *g = machine();
    bus_write8(g, 0x03008005, 0x99, false); /* IWRAM repeats every 32 KiB */
    CHECK_EQ(gba_iwram(g)[5], 0x99);
    size_t n = gba_state_size(g);
    uint8_t *state = malloc(n);
    CHECK_EQ(gba_state_save(g, state, n), n);
    bus_write8(g, 0x03000005, 0x01, false);
    CHECK_EQ(gba_iwram(g)[5], 0x01);
    CHECK_EQ(gba_state_load(g, state, n), GBA_OK);
    CHECK_EQ(gba_iwram(g)[5], 0x99);
    free(state);
    gba_destroy(g);
}
