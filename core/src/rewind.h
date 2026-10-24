#ifndef GBA_REWIND_H
#define GBA_REWIND_H

#include <stddef.h>

typedef struct gba gba_t;
typedef struct rewind rewind_t;

/* End of every gba_run_frame: a snapshot every interval_frames frames (nothing when rewind is off). */
void rewind_after_frame(gba_t *g);
/* Forget the history (reset, a new ROM, a loaded state); the memory stays reserved. */
void rewind_clear(rewind_t *r);
void rewind_free(rewind_t *r);
/* For measuring: snapshots kept (the current one included) and ring bytes used. */
void rewind_stats(const rewind_t *r, size_t *snapshots, size_t *bytes);

#endif
