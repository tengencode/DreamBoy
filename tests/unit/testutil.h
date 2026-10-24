#ifndef TESTUTIL_H
#define TESTUTIL_H

#include <stddef.h>
#include <stdint.h>

#include "gba_internal.h"

#define TU_LEN(a) (sizeof(a) / sizeof((a)[0]))

enum {
    C_BLACK = 0x0000,
    C_RED = 0x001F,
    C_GREEN = 0x03E0,
    C_BLUE = 0x7C00,
    C_WHITE = 0x7FFF,
    C_YELLOW = 0x03FF,
    C_MAGENTA = 0x7C1F,
    C_CYAN = 0x7FE0,
};

/* A system booted (HLE) into a ROM whose code is ops, starting at 0x08000000. */
gba_t *tu_arm(const uint32_t *ops, size_t n);
/* Same, but the CPU starts in Thumb state. */
gba_t *tu_thumb(const uint16_t *ops, size_t n);
/* Execute exactly n instructions (pending IRQs are taken as in cpu_step). */
void tu_steps(gba_t *g, int n);

/* Register and memory pokes through the bus (so I/O side effects happen). */
void tu_io16(gba_t *g, uint32_t off, uint16_t value); /* I/O register at 0x04000000 + off */
void tu_io32(gba_t *g, uint32_t off, uint32_t value);
uint16_t tu_raw16(const gba_t *g, uint32_t off);      /* stored I/O halfword, ignoring read rules */
void tu_pal(gba_t *g, uint32_t index, uint16_t color);   /* palette entry (256+ = OBJ palette) */
void tu_vram16(gba_t *g, uint32_t off, uint16_t value);  /* VRAM at 0x06000000 + off */
void tu_oam16(gba_t *g, uint32_t off, uint16_t value);   /* OAM at 0x07000000 + off */
void tu_hide_sprites(gba_t *g);                          /* disable all 128 sprites */
uint16_t tu_px(const gba_t *g, int x, int y);            /* framebuffer pixel */
void tu_advance_to(gba_t *g, uint64_t t);                /* set the clock, run due events */

/* A booted system whose ROM (size bytes, at least 0x200) starts with "b ." and holds each marker
 * string at 0x100 + 0x40 * i. */
gba_t *tu_rom_markers(const char *const *markers, size_t count, size_t size);

/* A booted system whose 0x400-byte ROM starts with "b .", has game code `code` (4 letters) at 0xAC,
 * 0xEE in bytes 0xC4-0xC9 (so GPIO registers and ROM bytes can be told apart) and, when marker is
 * not NULL, that string at 0x100. */
gba_t *tu_rom_code(const char *code, const char *marker);

#endif
