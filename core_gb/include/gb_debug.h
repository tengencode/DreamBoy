#ifndef GB_DEBUG_H
#define GB_DEBUG_H

#include "gb.h"

/* For tests and the PC tools; not used by the app. */
enum { GB_REG_AF, GB_REG_BC, GB_REG_DE, GB_REG_HL, GB_REG_SP, GB_REG_PC };
uint16_t gb_debug_reg(const gb_t *g, int which);
uint8_t gb_debug_peek(const gb_t *g, uint16_t addr); /* a read with no side effects and no time passing */
void gb_debug_step(gb_t *g);                         /* one instruction (or one interrupt entry, or one idle cycle) */
uint64_t gb_debug_ticks(const gb_t *g);              /* clock ticks since reset */
/* Every byte the game has sent on the serial port (at most 4096 kept), zero-terminated; returns its length. */
size_t gb_debug_serial(const gb_t *g, char *out, size_t cap);
int gb_debug_breakpoint(const gb_t *g); /* 1 once the game ran LD B,B (the mooneye test ROMs' "finished") */

#endif
