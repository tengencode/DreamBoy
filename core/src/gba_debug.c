#include "gba_debug.h"

#include "gba_internal.h"
#include "rewind.h"

uint32_t gba_debug_reg(const gba_t *g, unsigned n) {
    return n < 16 ? g->cpu.r[n] : 0;
}

uint32_t gba_debug_cpsr(const gba_t *g) {
    return g->cpu.cpsr;
}

uint32_t gba_debug_peek32(gba_t *g, uint32_t addr) {
    return bus_peek(g, addr, 4);
}

void gba_debug_step(gba_t *g) {
    cpu_step(g);
}

uint64_t gba_debug_cycles(const gba_t *g) {
    return g->now;
}

void gba_debug_rewind_stats(const gba_t *g, size_t *snapshots, size_t *bytes) {
    rewind_stats(g->rewind, snapshots, bytes);
}

int gba_debug_notes_enabled(void) {
#ifdef GBA_NOTES
    return 1;
#else
    return 0;
#endif
}

size_t gba_debug_notes(const gba_t *g, gba_note_t *out, size_t max) {
#ifdef GBA_NOTES
    static const char *const names[NOTE_KIND_COUNT] = {
        "bios-call", "midline-io", "midline-palette", "green-swap", "video-capture",
        "io-unused", "exec-empty", "undefined",       "notes-dropped"};
    size_t n = g->notes.count;
    for (size_t i = 0; i < n && i < max; i++) {
        const note_t *e = &g->notes.list[i];
        out[i] = (gba_note_t){names[e->kind], e->detail, e->first_frame, e->count};
    }
    return n;
#else
    (void)g;
    (void)out;
    (void)max;
    return 0;
#endif
}

size_t gba_debug_prints(const gba_t *g, gba_print_t *out, size_t max) {
#ifdef GBA_NOTES
    size_t n = g->notes.print_count;
    for (size_t i = 0; i < n && i < max; i++) {
        const note_print_t *p = &g->notes.prints[i];
        out[i] = (gba_print_t){p->level, p->first_frame, p->count, p->text};
    }
    return n;
#else
    (void)g;
    (void)out;
    (void)max;
    return 0;
#endif
}
