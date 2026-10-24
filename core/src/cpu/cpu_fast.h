#ifndef GBA_CPU_FAST_H
#define GBA_CPU_FAST_H

/*
 * The threaded CPU loop (milestone 7b): each fast handler runs its instruction and jumps straight to the next
 * instruction's handler (a guaranteed tail call), without cpu_step's per-instruction bookkeeping. A fast handler
 * only takes instructions it can finish exactly as cpu_step would; anything else (I/O and save-chip accesses,
 * mode changes, PC loads, an interrupt to take, the next event due, code outside RAM and ROM) leaves the loop
 * before any side effect, with r15 and next_fetch_seq stored, and cpu_run runs that instruction with cpu_step.
 * While the loop runs nothing can change the scheduler, the interrupt flags, a DMA or a halt (only I/O does).
 */
#include "gba_internal.h"
#include "mem/bus_inline.h"

#define FAST_TAIL __attribute__((musttail))

/* cond_table[cond] bit n: the condition passes when the flags NZCV are n (cpu.c). */
extern __attribute__((visibility("hidden"))) uint16_t cpu_cond_table[16];

typedef void arm_fast_t(gba_t *g, uint32_t pc, uint32_t op, bool seq);
typedef void thumb_fast_t(gba_t *g, uint32_t pc, uint32_t op, bool seq);
extern arm_fast_t *arm_fast_table[4096];   /* NULL: run by cpu_step */
extern thumb_fast_t *thumb_fast_table[1024];

void arm_fast_run(gba_t *g);   /* the fast loop from r15 in ARM state; returns when an instruction needs cpu_step */
void thumb_fast_run(gba_t *g); /* the same in Thumb state */

/* Leaves the loop before the instruction at pc: cpu_step continues from here. */
static inline void fast_exit(gba_t *g, uint32_t pc, bool seq) {
    g->cpu.r[15] = pc;
    g->cpu.next_fetch_seq = seq;
}

/* An instruction may start in the loop: the next event is not due and no interrupt is to be taken. */
static inline bool fast_may_start(const gba_t *g) {
    if (g->now >= g->sched.next) return false;
    return !(g->irq.pending && irq_line(g) && !(g->cpu.cpsr & CPSR_I));
}

/* The fetch of an instruction from RAM or ROM, as bus_fetch32 / bus_fetch16 and cpu_step do it. */
static inline void arm_fetched(gba_t *g, uint32_t pc, uint32_t op, bool seq) {
    g->bus.in_bios = false;
    g->now += bus_access_cycles(g, pc & ~3u, 4, seq);
    g->bus.open_bus = op;
    g->cpu.r[15] = pc + 8;
}

static inline void thumb_fetched(gba_t *g, uint32_t pc, uint32_t op, bool seq) {
    g->bus.in_bios = false;
    g->now += bus_access_cycles(g, pc & ~1u, 2, seq);
    g->bus.open_bus = op | op << 16;
    g->cpu.r[15] = pc + 4;
}

/* After a taken branch: the pipeline refill (N at the target, S after it), the prefetch buffer emptied. */
static inline void fast_refill(gba_t *g, uint32_t target, unsigned size) {
    g->now += bus_access_cycles(g, target, size, false) + bus_access_cycles(g, target + size, size, true);
    g->bus.prefetch_end = 0;
}

/* Where a data access at addr (aligned to size) can go straight to memory: EWRAM, IWRAM, the palette, VRAM and OAM
 * (not byte writes there: they have their own rules), and for reads the ROM itself (not past its end, not its first
 * 0x100 bytes, where the GPIO port may answer); NULL for the rest. */
static inline uint8_t *fast_data_ptr(gba_t *g, uint32_t addr, unsigned size, bool write) {
    switch (addr >> 24) {
    case 0x2: return &g->bus.ewram[addr & (EWRAM_SIZE - 1)];
    case 0x3: return &g->bus.iwram[addr & (IWRAM_SIZE - 1)];
    case 0x5:
        if (write) NOTE_DRAWING(g, NOTE_MIDLINE_PALETTE, 0); /* the warning log (test builds only) */
        return write && size == 1 ? NULL : &g->bus.pal[addr & (PAL_SIZE - 1)];
    case 0x6: {
        uint32_t off = addr & 0x1FFFF; /* 96 KiB in a 128 KiB window: 0x18000-0x1FFFF mirrors 0x10000-0x17FFF */
        return write && size == 1 ? NULL : &g->bus.vram[off >= 0x18000 ? off - 0x8000 : off];
    }
    case 0x7: return write && size == 1 ? NULL : &g->bus.oam[addr & (OAM_SIZE - 1)];
    case 0x8:
    case 0x9:
    case 0xA:
    case 0xB:
    case 0xC: {
        if (write) return NULL;
        uint32_t off = addr & 0x01FFFFFFu;
        return off >= 0x100 && off + size <= g->cart.rom_size ? g->cart.rom + off : NULL;
    }
    default: return NULL;
    }
}

/* A data access in the loop: its cycles, and the cartridge bus left free when it is not the ROM (bus_read*,
 * bus_write*). The caller has checked that no DMA is due by then (fast_data_ok). */
static inline void fast_data_cycles(gba_t *g, uint32_t addr, unsigned size) {
    uint32_t cyc = bus_access_cycles(g, addr, size, false);
    g->now += cyc;
    if (addr < 0x08000000u) {
        g->bus.off_rom += cyc;
        g->bus.prefetch_hint = true;
    }
}

/* Before the fetch is counted: a data access fetch_cycles later would find no DMA due (bus_read* would run it). */
static inline bool fast_data_ok(const gba_t *g, uint32_t fetch_cycles) {
    return g->now + fetch_cycles < g->dma.due;
}

/* The end of an instruction that made data accesses, as cpu_step ends it: the next fetch is non-sequential, and
 * the prefetch buffer may have used the time the bus was off the cartridge. */
static inline bool fast_after_data(gba_t *g, uint32_t next_pc) {
    g->cpu.next_fetch_seq = false;
    if (g->bus.prefetch_hint) {
        g->cpu.r[15] = next_pc;
        bus_prefetch_stall(g);
    }
    return g->cpu.next_fetch_seq;
}

/* A block of count words from addr (each access aligned down, as bus_read32 / bus_write32 do) in EWRAM or IWRAM,
 * all in one region, with no DMA due before its last access: the region's memory, its address mask and the cycles
 * of each access (EWRAM and IWRAM take as long sequential as not); NULL otherwise. */
static inline uint8_t *fast_block_ptr(gba_t *g, uint32_t addr, uint32_t count, uint32_t fetch_cycles, uint32_t *mask,
                                      uint32_t *cyc) {
    uint32_t a0 = addr & ~3u, region = a0 >> 24;
    if ((region != 0x2 && region != 0x3) || ((a0 + 4 * (count - 1)) >> 24) != region) return NULL;
    uint32_t c = g->bus.cycles[region][1][0];
    if (g->bus.cycles[region][1][1] != c || g->now + fetch_cycles + (uint64_t)(count - 1) * c >= g->dma.due) return NULL;
    *cyc = c;
    *mask = region == 0x2 ? EWRAM_SIZE - 1 : IWRAM_SIZE - 1;
    return region == 0x2 ? g->bus.ewram : g->bus.iwram;
}

/* One word of such a block: its cycles off the cartridge bus. */
static inline uint32_t fast_block_load(gba_t *g, const uint8_t *mem, uint32_t mask, uint32_t cyc, uint32_t addr) {
    g->now += cyc;
    g->bus.off_rom += cyc;
    return bus_load_n(mem + (addr & ~3u & mask), 4);
}

static inline void fast_block_store(gba_t *g, uint8_t *mem, uint32_t mask, uint32_t cyc, uint32_t addr, uint32_t v) {
    g->now += cyc;
    g->bus.off_rom += cyc;
    bus_store32(mem + (addr & ~3u & mask), v);
}

/* Any other instruction, run in the loop exactly as cpu_step runs it: these reset its per-instruction state before
 * the fetch, and finish it after the handler (r15 to the next instruction, the refill after a branch, the
 * non-sequential fetch and prefetch stall after a data access). fast_generic_end is false when the loop must stop:
 * the CPU halted or stopped. */
static inline void fast_generic_begin(gba_t *g) {
    g->cpu.branched = false;
    g->bus.data_access = false;
    g->bus.prefetch_hint = false;
    g->bus.off_rom = 0;
}

static inline bool fast_generic_end(gba_t *g, uint32_t next_pc) {
    arm_cpu_t *c = &g->cpu;
    c->next_fetch_seq = true;
    if (!c->branched) c->r[15] = next_pc;
    if (c->branched) {
        unsigned size = (c->cpsr & CPSR_T) ? 2 : 4;
        g->now += bus_access_cycles(g, c->r[15], size, false) + bus_access_cycles(g, c->r[15] + size, size, true);
        c->next_fetch_seq = true;
        g->bus.prefetch_end = 0;
    } else if (g->bus.data_access) {
        c->next_fetch_seq = false;
        if (g->bus.prefetch_hint) bus_prefetch_stall(g);
    }
    return !g->irq.halted && !g->irq.stopped;
}

/* Starts the per-instruction counters a data access updates (cpu_step zeroes them before every instruction). */
static inline void fast_data_begin(gba_t *g) {
    g->bus.off_rom = 0;
    g->bus.prefetch_hint = false;
}

/* Jumps to the handler of the ARM instruction at pc, or leaves the loop. */
#define ARM_NEXT(g, pc, seq)                                                                   \
    do {                                                                                       \
        uint32_t pc_ = (pc);                                                                   \
        bool seq_ = (seq);                                                                     \
        const uint8_t *p_;                                                                     \
        if (!fast_may_start(g) || !(p_ = bus_code_ptr(g, pc_ & ~3u))) {                        \
            fast_exit(g, pc_, seq_);                                                           \
            return;                                                                            \
        }                                                                                      \
        uint32_t op_ = bus_load_n(p_, 4);                                                      \
        arm_fast_t *f_ = ((cpu_cond_table[op_ >> 28] >> ((g)->cpu.cpsr >> 28)) & 1)            \
                             ? arm_fast_table[((op_ >> 16) & 0xFF0) | ((op_ >> 4) & 0xF)]      \
                             : arm_fast_skip;                                                  \
        if (!f_) {                                                                             \
            fast_exit(g, pc_, seq_);                                                           \
            return;                                                                            \
        }                                                                                      \
        FAST_TAIL return f_(g, pc_, op_, seq_);                                                \
    } while (0)

#define THUMB_NEXT(g, pc, seq)                                                                 \
    do {                                                                                       \
        uint32_t pc_ = (pc);                                                                   \
        bool seq_ = (seq);                                                                     \
        const uint8_t *p_;                                                                     \
        if (!fast_may_start(g) || !(p_ = bus_code_ptr(g, pc_ & ~1u))) {                        \
            fast_exit(g, pc_, seq_);                                                           \
            return;                                                                            \
        }                                                                                      \
        uint32_t op_ = bus_load_n(p_, 2);                                                      \
        thumb_fast_t *f_ = thumb_fast_table[op_ >> 6];                                         \
        if (!f_) {                                                                             \
            fast_exit(g, pc_, seq_);                                                           \
            return;                                                                            \
        }                                                                                      \
        FAST_TAIL return f_(g, pc_, op_, seq_);                                                \
    } while (0)

void arm_fast_skip(gba_t *g, uint32_t pc, uint32_t op, bool seq);    /* an ARM instruction whose condition fails */
void arm_fast_generic(gba_t *g, uint32_t pc, uint32_t op, bool seq); /* any other ARM instruction */
void thumb_fast_generic(gba_t *g, uint32_t pc, uint32_t op, bool seq);

#endif
