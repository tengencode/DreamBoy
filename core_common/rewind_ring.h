#ifndef REWIND_RING_H
#define REWIND_RING_H

#include <stddef.h>
#include <stdint.h>

/* The rewind history of either core: a snapshot every few frames, each kept as its packed difference to the one
 * after it (delta_pack). The ring knows nothing about what a snapshot is; a core fills and loads the buffers. */
typedef struct rewind_ring rewind_ring_t;

/* Reserves all the memory: budget_bytes for the history plus three snapshots' worth. NULL: no memory. */
rewind_ring_t *rewind_ring_new(size_t state_size, size_t budget_bytes, int interval_frames);
void rewind_ring_free(rewind_ring_t *r);
/* Forget the history (reset, a new ROM, a loaded state); the memory stays reserved. */
void rewind_ring_clear(rewind_ring_t *r);
/* After every frame: NULL (no snapshot is due, or r is NULL), or a state_size buffer for the core to save its
 * state into, followed by rewind_ring_commit. */
uint8_t *rewind_ring_begin(rewind_ring_t *r);
void rewind_ring_commit(rewind_ring_t *r);
/* One step back: the snapshot to load (the newest one first, if frames ran since it), or NULL when there is none.
 * A core that cannot load it calls rewind_ring_clear. */
const uint8_t *rewind_ring_back(rewind_ring_t *r);
/* For measuring: snapshots kept (the current one included) and ring bytes used. */
void rewind_ring_stats(const rewind_ring_t *r, size_t *snapshots, size_t *bytes);

#endif
