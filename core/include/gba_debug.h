#ifndef GBA_DEBUG_H
#define GBA_DEBUG_H

#include <stddef.h>
#include <stdint.h>

#include "gba.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Introspection for tools and tests; not part of the frontend API. */
uint32_t gba_debug_reg(const gba_t *g, unsigned n);  /* r0-r15 of the current mode; r15 = next instruction */
uint32_t gba_debug_cpsr(const gba_t *g);
uint32_t gba_debug_peek32(gba_t *g, uint32_t addr);  /* no cycles or side effects; addr aligned down */
void gba_debug_step(gba_t *g);                       /* execute exactly one instruction */
uint64_t gba_debug_cycles(const gba_t *g);
/* Rewind history: snapshots kept (the newest full one included) and packed bytes used. */
void gba_debug_rewind_stats(const gba_t *g, size_t *snapshots, size_t *bytes);

/* The warning log (milestone 13): what the game used that the emulator skips. Only builds with GBA_NOTES keep
 * it (tools/dev.sh's build/host and build/check); elsewhere it is always empty. */
#define GBA_DEBUG_NOTES_MAX 64
typedef struct {
    const char *kind;     /* "bios-call", "midline-io", "midline-palette", "green-swap", "video-capture",
                             "io-unused", "exec-empty", "undefined", "notes-dropped" */
    uint32_t detail;      /* the BIOS call number, register offset or address; 0 when the kind has none */
    uint32_t first_frame; /* frames finished before it first happened */
    uint32_t count;
} gba_note_t;
int gba_debug_notes_enabled(void);                                   /* 1 in builds with the warning log */
size_t gba_debug_notes(const gba_t *g, gba_note_t *out, size_t max); /* entries kept (copies at most max) */
/* The game's own messages through mGBA's debug-print registers, once per text and level (GBA_NOTES builds). */
#define GBA_DEBUG_PRINTS_MAX 16
typedef struct {
    uint32_t level;       /* 0 fatal, 1 error, 2 warning, 3 info, 4 debug */
    uint32_t first_frame; /* frames finished before it was first sent */
    uint32_t count;
    const char *text;     /* valid while the machine lives and is not reset */
} gba_print_t;
size_t gba_debug_prints(const gba_t *g, gba_print_t *out, size_t max); /* messages kept (copies at most max) */

#ifdef __cplusplus
}
#endif

#endif
