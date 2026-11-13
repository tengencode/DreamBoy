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

#endif
