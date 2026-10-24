#include "sys/keypad.h"

#include "gba_internal.h"

void keypad_check(gba_t *g) {
    uint16_t cnt = (uint16_t)(g->io.raw[0x132] | g->io.raw[0x133] << 8);
    if (!(cnt & 0x4000)) return;
    uint16_t selected = cnt & 0x3FF;
    uint16_t pressed = (uint16_t)(~g->keyinput & 0x3FF);
    bool hit = (cnt & 0x8000) ? (pressed & selected) == selected /* AND: all of them */
                              : (pressed & selected) != 0;       /* OR: any of them */
    if (hit) irq_raise(g, IRQ_KEYPAD);
}
