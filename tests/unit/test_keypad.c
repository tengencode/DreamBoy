#include "test.h"
#include "testutil.h"

TEST(keypad_irq_any_selected_key) {
    gba_t *g = gba_create(NULL);
    tu_io16(g, 0x132, 0x4000 | GBA_KEY_A | GBA_KEY_B); /* IRQ on, OR mode */
    gba_set_keys(g, GBA_KEY_START);
    CHECK(!(g->irq.if_ & IRQ_KEYPAD));
    gba_set_keys(g, GBA_KEY_B);
    CHECK(g->irq.if_ & IRQ_KEYPAD);
    gba_destroy(g);
}

TEST(keypad_irq_all_selected_keys) {
    gba_t *g = gba_create(NULL);
    tu_io16(g, 0x132, 0xC000 | GBA_KEY_A | GBA_KEY_B); /* IRQ on, AND mode */
    gba_set_keys(g, GBA_KEY_A);
    CHECK(!(g->irq.if_ & IRQ_KEYPAD));
    gba_set_keys(g, GBA_KEY_A | GBA_KEY_B);
    CHECK(g->irq.if_ & IRQ_KEYPAD);
    gba_destroy(g);
}

TEST(keypad_irq_needs_its_enable_bit) {
    gba_t *g = gba_create(NULL);
    tu_io16(g, 0x132, GBA_KEY_A);
    gba_set_keys(g, GBA_KEY_A);
    CHECK(!(g->irq.if_ & IRQ_KEYPAD));
    gba_destroy(g);
}

TEST(keypad_irq_checked_when_keycnt_is_written) {
    gba_t *g = gba_create(NULL);
    gba_set_keys(g, GBA_KEY_A);
    tu_io16(g, 0x132, 0x4000 | GBA_KEY_A);
    CHECK(g->irq.if_ & IRQ_KEYPAD);
    gba_destroy(g);
}
