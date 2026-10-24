#ifndef GBA_KEYPAD_H
#define GBA_KEYPAD_H

typedef struct gba gba_t;

/* Checks KEYCNT (0x04000132) against the pressed keys and raises the keypad IRQ when its condition
 * holds. Called when the keys change and when KEYCNT is written. */
void keypad_check(gba_t *g);

#endif
