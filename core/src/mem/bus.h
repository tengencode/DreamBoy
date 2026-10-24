#ifndef GBA_BUS_H
#define GBA_BUS_H

#include <stdbool.h>
#include <stdint.h>

typedef struct gba gba_t;

#define BIOS_SIZE 0x4000u
#define EWRAM_SIZE 0x40000u
#define IWRAM_SIZE 0x8000u
#define PAL_SIZE 0x400u
#define VRAM_SIZE 0x18000u
#define OAM_SIZE 0x400u

typedef struct {
    uint8_t bios[BIOS_SIZE];
    uint8_t ewram[EWRAM_SIZE];
    uint8_t iwram[IWRAM_SIZE];
    uint8_t pal[PAL_SIZE];
    uint8_t vram[VRAM_SIZE];
    uint8_t oam[OAM_SIZE];
    uint16_t waitcnt;
    uint8_t cycles[16][2][2]; /* [region][0: 8/16-bit, 1: 32-bit][0: non-seq, 1: seq] */
    bool in_bios;             /* the executing instruction was fetched from the BIOS */
    uint32_t bios_latch;      /* what BIOS reads return while executing outside the BIOS */
    bool data_access;         /* the current instruction made a data read/write (not a code fetch) */
    bool prefetch_hint;       /* this instruction left the cartridge bus free (RAM/I/O access or multiply) */
    uint32_t off_rom;         /* cycles this instruction spent off the cartridge bus */
    uint32_t prefetch_end;    /* address of the last halfword the prefetch buffer holds (0: empty) */
    uint32_t open_bus;        /* last opcode fetched: open-bus fallback when the pipeline is unreadable */
} bus_t;

void bus_reset(gba_t *g); /* clears RAM (not the BIOS) and restores default timings */
void bus_set_waitcnt(gba_t *g, uint16_t value);
void bus_rebuild_timing(gba_t *g); /* the cycles table from the fixed regions and WAITCNT (after a state load) */

uint32_t bus_read8(gba_t *g, uint32_t addr, bool seq);
uint32_t bus_read16(gba_t *g, uint32_t addr, bool seq); /* addr aligned down to 2 */
uint32_t bus_read32(gba_t *g, uint32_t addr, bool seq); /* addr aligned down to 4 */
void bus_write8(gba_t *g, uint32_t addr, uint8_t value, bool seq);
void bus_write16(gba_t *g, uint32_t addr, uint16_t value, bool seq);
void bus_write32(gba_t *g, uint32_t addr, uint32_t value, bool seq);
/* Instruction fetches: also update the BIOS latch and the open-bus value. */
uint32_t bus_fetch16(gba_t *g, uint32_t addr, bool seq);
uint32_t bus_fetch32(gba_t *g, uint32_t addr, bool seq);
void bus_idle(gba_t *g, uint32_t cycles);
/* Cycles of one code fetch at addr (no access is made): for the pipeline refill. */
uint32_t bus_code_cycles(const gba_t *g, uint32_t addr, unsigned size, bool seq);
/* Prefetch buffer (WAITCNT bit 14), mGBA's estimate: called at the end of an instruction that ran from
 * the cartridge and spent cycles off it. Credits the cycles the buffer saves and makes the next fetch S. */
void bus_prefetch_stall(gba_t *g);
/* Read without cycles or side effects, for debuggers and tests. */
uint32_t bus_peek(gba_t *g, uint32_t addr, unsigned size);
/* Write without cycles, for cheats: RAM, I/O (through the register write), palette, VRAM and OAM only
 * (their usual byte-write rules apply); the BIOS, ROM and save chip are left alone. */
void bus_poke(gba_t *g, uint32_t addr, uint32_t value, unsigned size);
/* What unmapped reads return: the opcode(s) the CPU has prefetched (ARM: the word at the current
 * instruction + 8; Thumb: by the region the code runs from, see GBATEK "Unpredictable Things"). */
uint32_t bus_open_bus(gba_t *g);

#endif
