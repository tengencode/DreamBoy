#ifndef GB_INTERNAL_H
#define GB_INTERNAL_H

#include <stdbool.h>

#include "gb.h"
#include "gb_debug.h"

/* Every name seen outside its file starts with gb_: both cores are linked into one program. */

typedef enum { MBC_NONE, MBC1, MBC2, MBC3, MBC5 } mbc_t;

/* What the loaded ROM is: set by gb_load_rom, never part of a save state. */
typedef struct {
    uint8_t *rom;      /* a power-of-two copy, padded with 0xFF */
    uint32_t rom_size; /* of the copy */
    uint32_t crc32;    /* of the bytes as given */
    char title[17];
    bool color;
    mbc_t mbc;
    bool battery, clock, rumble;
    uint32_t ram_size; /* 0 or a power of two */
} cart_info_t;

/* CPU flags (register F). */
#define GB_FZ 0x80
#define GB_FN 0x40
#define GB_FH 0x20
#define GB_FC 0x10

typedef struct {
    uint8_t a, f, b, c, d, e, h, l;
    uint16_t sp, pc;
    uint8_t ime;        /* interrupts on */
    uint8_t ei_delay;   /* EI: ime turns on when this counts down to 0 (after the next instruction) */
    uint8_t halted;     /* HALT / STOP: idle until an interrupt is pending */
    uint8_t halt_bug;   /* the next opcode byte is read without moving PC */
    uint8_t locked;     /* an illegal opcode: the CPU has stopped for good */
    uint8_t breakpoint; /* LD B,B ran */
} gb_cpu_t;

typedef struct {
    uint8_t wram[GB_WRAM_SIZE];
    uint8_t hram[0x7F];
    uint8_t iflag, ie; /* interrupt requests (5 bits) and enables */
    uint8_t wram_bank; /* the bank at 0xD000: 1-7 */
} gb_mem_t;

struct gb {
    cart_info_t info;
    uint8_t *cart_ram; /* info.ram_size bytes */
    gb_cpu_t cpu;
    gb_mem_t mem;
    uint64_t ticks;     /* clock ticks since reset */
    int32_t frame_left; /* ticks left in the frame gb_run_frame is running (<= 0: done; the overshoot carries over) */
};

/* One machine cycle passes: everything but the CPU moves on. Called before each bus access and for each of the
 * CPU's internal cycles. */
void gb_tick(gb_t *g);
uint8_t gb_bus_read(gb_t *g, uint16_t addr); /* a CPU read: one cycle, then the value */
void gb_bus_write(gb_t *g, uint16_t addr, uint8_t value);
uint8_t gb_bus_peek(const gb_t *g, uint16_t addr); /* the value, no time passing */
void gb_cpu_reset(gb_t *g);
void gb_cpu_step(gb_t *g); /* one instruction, interrupt entry or idle cycle */

gb_err_t gb_cart_load(gb_t *g, const uint8_t *rom, size_t size);
void gb_cart_free(gb_t *g);

#endif
