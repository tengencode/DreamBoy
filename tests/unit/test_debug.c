#include "gba_debug.h"
#include "test.h"
#include "testutil.h"

TEST(debug_api_reads_state_and_steps) {
    const uint32_t ops[] = {
        0xE3A00005, /* mov r0, #5 */
        0xEAFFFFFE, /* b . */
    };
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    CHECK_EQ(gba_debug_reg(g, 15), 0x08000000);
    CHECK_EQ(gba_debug_peek32(g, 0x08000004), 0xEAFFFFFE);
    uint64_t before = gba_debug_cycles(g);
    gba_debug_step(g);
    CHECK_EQ(gba_debug_reg(g, 0), 5);
    CHECK(gba_debug_cycles(g) > before);
    CHECK_EQ(gba_debug_cpsr(g), MODE_SYS);
    gba_destroy(g);
}
