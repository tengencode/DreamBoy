#ifndef GBTESTUTIL_H
#define GBTESTUTIL_H

#include <stddef.h>
#include <stdint.h>

#include "gb_internal.h"

/* A ROM of `size` bytes, zeroed, with a valid header: "NOP; JP 0x150" at 0x100, the title "TEST", the given
 * cartridge type (0x147), RAM size code (0x149) and colour flag (0x143), the size byte and header checksum right.
 * The caller frees it. */
uint8_t *gbtu_rom(size_t size, uint8_t cart_type, uint8_t ram_code, uint8_t cgb_flag);
void gbtu_fix_checksum(uint8_t *rom); /* after changing header bytes */
/* A machine running a 32 KiB ROM without a banking chip whose code (n bytes) starts at 0x150; the CPU is at
 * 0x100 ("NOP; JP 0x150"). */
gb_t *gbtu_boot(const uint8_t *code, size_t n);
/* A machine idling in "JR -2", for tests that set the hardware up by hand. */
gb_t *gbtu_idle(void);
/* Tile `index` of the first tile block (0x8000 addressing) from 8 strings of 8 digits '0'-'3'. */
void gbtu_tile(gb_t *g, int index, const char *const rows[8]);
/* A tile filled with one colour number. */
void gbtu_tile_fill(gb_t *g, int index, int colour);
uint16_t gbtu_px(const gb_t *g, int x, int y); /* the shown picture */
void gbtu_frames(gb_t *g, int n);

#endif
