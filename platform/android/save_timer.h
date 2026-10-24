#ifndef GBAEMU_SAVE_TIMER_H
#define GBAEMU_SAVE_TIMER_H

#include <stdbool.h>

#define SAVE_TIMER_FRAMES 60 /* write about 1 s after the game's last save */

/* When to write the save file: a game often writes its save over several frames, so the file is
 * written once, SAVE_TIMER_FRAMES quiet frames after the last write — or at once on a flush. */
typedef struct {
    int countdown; /* frames until the write; 0 = nothing pending */
} save_timer_t;

void save_timer_init(save_timer_t *t);
/* Once per frame; dirty = the game saved during this frame. True: write the save file now. */
bool save_timer_frame(save_timer_t *t, bool dirty);
/* Pause, stop, another game: true if a write is pending (write now; it is no longer pending). */
bool save_timer_flush(save_timer_t *t);

#endif
