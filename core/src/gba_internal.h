#ifndef GBA_INTERNAL_H
#define GBA_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "gba.h"
#include "apu/apu.h"
#include "cart/cart.h"
#include "sys/dma.h"
#include "cpu/cpu.h"
#include "mem/bus.h"
#include "mem/io.h"
#include "notes.h"
#include "ppu/ppu.h"
#include "sys/irq.h"
#include "sys/sched.h"
#include "sys/sio.h"
#include "sys/timer.h"

struct gba {
    arm_cpu_t cpu;
    bus_t bus;
    io_t io;
    ppu_t ppu;
    irq_t irq;
    timers_t timers;
    dma_t dma;
    apu_t apu;
    sio_t sio;
    sched_t sched;
    cart_t cart;
    uint64_t now;      /* master clock: 16.78 MHz cycles since reset */
    bool frame_done;   /* set when VBlank starts; ends gba_run_frame */
    bool hle_bios;     /* true: built-in BIOS stub + C SWIs; false: user BIOS */
    uint16_t keyinput; /* KEYINPUT register: bit clear = pressed */
    struct rewind_ring *rewind; /* rewind history (core_common), NULL while off; in no state chunk */
    struct cheat_list *cheats; /* the switched-on cheats (cheats/), NULL without; in no state chunk */
#ifdef GBA_NOTES
    notes_t notes; /* the warning log (notes.h); in no state chunk */
#endif
};

/* gba_state_load without forgetting the rewind history (rewind.c loads its own snapshots with it). */
gba_err_t state_load(gba_t *g, const void *buf, size_t len);

/* Dispatch every scheduler event due at g->now, in timestamp order. */
void gba_run_events(gba_t *g);

#endif
