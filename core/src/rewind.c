/* Rewind for the Game Boy Advance core: its save states in the shared history ring (core_common/rewind_ring.c). */
#include "rewind.h"

#include "gba_internal.h"
#include "rewind_ring.h"

gba_err_t gba_rewind_configure(gba_t *g, size_t budget_bytes, int interval_frames) {
    rewind_ring_free(g->rewind);
    g->rewind = NULL;
    if (budget_bytes == 0 || interval_frames < 1) return GBA_OK;
    g->rewind = rewind_ring_new(gba_state_size(g), budget_bytes, interval_frames);
    return g->rewind ? GBA_OK : GBA_ERR_NOMEM;
}

void rewind_after_frame(gba_t *g) {
    uint8_t *snapshot = rewind_ring_begin(g->rewind);
    if (!snapshot) return;
    gba_state_save(g, snapshot, gba_state_size(g));
    rewind_ring_commit(g->rewind);
}

int gba_rewind_step_back(gba_t *g) {
    const uint8_t *snapshot = rewind_ring_back(g->rewind);
    if (!snapshot) return 0;
    if (state_load(g, snapshot, gba_state_size(g)) != GBA_OK) {
        rewind_ring_clear(g->rewind);
        return 0;
    }
    return 1;
}
