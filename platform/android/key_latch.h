#ifndef GBAEMU_KEY_LATCH_H
#define GBAEMU_KEY_LATCH_H

#include <stdatomic.h>

/* Held GBA buttons from the UI thread to the emulation thread, where a press shorter than a frame
 * still reaches the game: every button pressed since the last frame counts as held for that frame.
 * (The phone emulator's window sends letter keys as 0.1 ms taps; the game reads the keys once a frame.) */
typedef struct {
    atomic_uint held;    /* buttons held right now (GBA_KEY_* bits) */
    atomic_uint pressed; /* buttons that went down since the last key_latch_take */
} key_latch_t;

void key_latch_init(key_latch_t *k);
/* UI thread: the buttons now held. */
void key_latch_set(key_latch_t *k, unsigned held);
/* Emulation thread, once per frame: the buttons for this frame (held now, or pressed since the last call). */
unsigned key_latch_take(key_latch_t *k);

#endif
