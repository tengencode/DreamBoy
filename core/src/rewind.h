#ifndef GBA_REWIND_H
#define GBA_REWIND_H

#include <stddef.h>

typedef struct gba gba_t;

/* End of every gba_run_frame: a snapshot every interval_frames frames (nothing when rewind is off). The history
 * itself is the shared ring, core_common/rewind_ring.h. */
void rewind_after_frame(gba_t *g);

#endif
