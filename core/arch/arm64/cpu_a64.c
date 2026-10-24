/*
 * The ARM64 assembly CPU loop (cpu_a64.S): its state, and the C it calls. Its plain-C twin is cpu_run_generic
 * (cpu.c, cpu_fast.h); both run the same instructions with the same results and cycles.
 *
 * The loop keeps the guest PC, the cycle count, the sequential-fetch flag and a few pointers in registers. It runs
 * the common instructions itself while their code is in one "window" of RAM or ROM and nothing is due; for anything
 * else it calls a64_slow, which runs that one instruction with cpu_step. While it runs, nothing can change the
 * scheduler, the interrupt flags, a DMA, WAITCNT or a halt: only an I/O access does, and those go to cpu_step.
 */
#include <stddef.h>

#include "gba_internal.h"
#include "cpu/cpu_fast.h"
#include "mem/bus_inline.h"

typedef struct a64_ctx {
    gba_t *g;           /* 0 */
    uint64_t limit;     /* 8: leave the loop at this cycle: the next event, or an interrupt becoming visible */
    uint8_t *win_host;  /* 16: the host address of guest address win_lo */
    uint32_t win_lo;    /* 24: the code window: [win_lo, win_lo + win_len) */
    uint32_t win_len;   /* 28 */
    uint8_t fetch_n;    /* 32: cycles of a non-sequential / sequential code fetch in the window */
    uint8_t fetch_s;    /* 33 */
    uint8_t stall;      /* 34: the window is the cartridge and the prefetch buffer is on */
    uint8_t pad0;
    uint32_t rom_size;  /* 36 */
    uint8_t *ewram;     /* 40 */
    uint8_t *iwram;     /* 48 */
    const uint8_t *rom; /* 56 */
    uint64_t *now;      /* 64 */
    uint16_t waitcnt;   /* 72: WAITCNT when the window was found (its fetch cycles and the stall flag depend on it) */
    uint8_t thumb;      /* 74: the window is for this state's fetch size */
    uint8_t stall_s;    /* 75: with stall: the cartridge's sequential wait states (16-bit), bus_prefetch_stall's s */
    uint8_t *pal;       /* 80 */
    uint8_t *vram;      /* 88 */
    uint8_t *oam;       /* 96 */
} a64_ctx_t;

_Static_assert(offsetof(a64_ctx_t, limit) == 8, "cpu_a64.S");
_Static_assert(offsetof(a64_ctx_t, win_host) == 16, "cpu_a64.S");
_Static_assert(offsetof(a64_ctx_t, win_lo) == 24, "cpu_a64.S");
_Static_assert(offsetof(a64_ctx_t, win_len) == 28, "cpu_a64.S");
_Static_assert(offsetof(a64_ctx_t, fetch_n) == 32, "cpu_a64.S");
_Static_assert(offsetof(a64_ctx_t, fetch_s) == 33, "cpu_a64.S");
_Static_assert(offsetof(a64_ctx_t, stall) == 34, "cpu_a64.S");
_Static_assert(offsetof(a64_ctx_t, rom_size) == 36, "cpu_a64.S");
_Static_assert(offsetof(a64_ctx_t, ewram) == 40, "cpu_a64.S");
_Static_assert(offsetof(a64_ctx_t, iwram) == 48, "cpu_a64.S");
_Static_assert(offsetof(a64_ctx_t, rom) == 56, "cpu_a64.S");
_Static_assert(offsetof(a64_ctx_t, now) == 64, "cpu_a64.S");
_Static_assert(offsetof(a64_ctx_t, stall_s) == 75, "cpu_a64.S");
_Static_assert(offsetof(a64_ctx_t, pal) == 80, "cpu_a64.S");
_Static_assert(offsetof(a64_ctx_t, vram) == 88, "cpu_a64.S");
_Static_assert(offsetof(a64_ctx_t, oam) == 96, "cpu_a64.S");
/* The guest registers: r[0..15], then CPSR; next_fetch_seq further on. */
_Static_assert(offsetof(arm_cpu_t, cpsr) == 64, "cpu_a64.S");
_Static_assert(offsetof(arm_cpu_t, next_fetch_seq) == 181, "cpu_a64.S");
/* The bus fields the loop touches, from &bus.waitcnt. */
#define BUS_OFF(field) (offsetof(bus_t, field) - offsetof(bus_t, waitcnt))
_Static_assert(BUS_OFF(cycles) == 2, "cpu_a64.S");
_Static_assert(BUS_OFF(in_bios) == 66, "cpu_a64.S");
_Static_assert(BUS_OFF(off_rom) == 76, "cpu_a64.S");
_Static_assert(BUS_OFF(prefetch_end) == 80, "cpu_a64.S");
_Static_assert(BUS_OFF(open_bus) == 84, "cpu_a64.S");

/* cpu_a64.S */
/* Return 2 after a BX into the other state (go on there, no cpu_step in between), else 0. */
int a64_run_arm(a64_ctx_t *x, uint32_t *regs, uint8_t *bus);
int a64_run_thumb(a64_ctx_t *x, uint32_t *regs, uint8_t *bus);
extern const uintptr_t a64_thumb_handlers[58];
extern char a64_thumb_slow[];
__attribute__((visibility("hidden"))) uintptr_t a64_thumb_table[1024];
extern const uintptr_t a64_arm_dp_handlers[16][2][5];
extern const uintptr_t a64_arm_single_handlers[64];
extern char a64_arm_slow[], a64_arm_branch[], a64_arm_block[], a64_arm_mul[], a64_arm_half[], a64_arm_bx[];
__attribute__((visibility("hidden"))) uintptr_t a64_arm_table[4096];

/* Where the loop must stop: the next event, or (with an interrupt waiting that the CPU would take) the moment
 * the request becomes visible. The same test as fast_may_start, as one number. */
static uint64_t loop_limit(const gba_t *g) {
    uint64_t limit = g->sched.next;
    const irq_t *q = &g->irq;
    if (q->pending && (q->ime & 1) && !(g->cpu.cpsr & CPSR_I) && q->visible_at < limit) limit = q->visible_at;
    return limit;
}

/* The code window around pc for fetches of size bytes (bus_code_ptr's rules); false when pc is elsewhere. */
int a64_window(a64_ctx_t *x, uint32_t pc, uint32_t size) {
    gba_t *g = x->g;
    uint32_t region = pc >> 24, lo, hi;
    uint8_t *host;
    if (pc & (size - 1)) return 0;
    switch (region) {
    case 0x2:
        lo = pc & ~(EWRAM_SIZE - 1);
        hi = lo + EWRAM_SIZE - 3;
        host = g->bus.ewram;
        break;
    case 0x3:
        lo = pc & ~(IWRAM_SIZE - 1);
        hi = lo + IWRAM_SIZE - 3;
        host = g->bus.iwram;
        break;
    case 0x8:
    case 0x9:
    case 0xA:
    case 0xB:
    case 0xC: {
        uint32_t base = (region & ~1u) << 24, top = (region + 1) << 24;
        if (g->cart.rom_size < 0x104) return 0;
        lo = base + 0x100 > region << 24 ? base + 0x100 : region << 24;
        hi = base + g->cart.rom_size - 3; /* off + 4 <= rom_size */
        if (hi > top || hi < base) hi = top;
        if (hi <= lo || pc < lo || pc >= hi) return 0;
        host = g->cart.rom + (lo - base);
        break;
    }
    default: return 0;
    }
    if (pc < lo || pc >= hi) return 0; /* e.g. Thumb code in RAM's last halfword (bus_code_ptr reads 4 bytes) */
    x->win_lo = lo;
    x->win_len = hi - lo;
    x->win_host = host;
    x->fetch_n = g->bus.cycles[region][size == 4][0];
    x->fetch_s = g->bus.cycles[region][size == 4][1];
    x->stall = region >= 0x8 && (g->bus.waitcnt & 0x4000);
    x->stall_s = (uint8_t)(g->bus.cycles[region][0][1] - 1);
    x->waitcnt = g->bus.waitcnt;
    x->thumb = size == 2;
    return 1;
}

static void prepare(a64_ctx_t *x) {
    x->limit = loop_limit(x->g);
    /* The window stays while it is still right: the same WAITCNT and the same state's fetch size. */
    if (x->g->bus.waitcnt != x->waitcnt || x->thumb != ((x->g->cpu.cpsr & CPSR_T) != 0)) x->win_len = 0;
}

/* The instruction at r15 by cpu_step (the loop has stored r15, now and next_fetch_seq). Returns 1 to go on in
 * the same state, 0 to leave the loop. */
int a64_slow(a64_ctx_t *x) {
    gba_t *g = x->g;
    uint32_t thumb = g->cpu.cpsr & CPSR_T;
    cpu_step(g);
    if (g->irq.halted || g->irq.stopped || (g->cpu.cpsr & CPSR_T) != thumb) return 0;
    prepare(x);
    return 1;
}

/* The prefetch buffer after a data access from cartridge code (bus_prefetch_stall; the loop has stored r15 = the
 * next instruction, now and off_rom). */
void a64_stall(a64_ctx_t *x) {
    bus_prefetch_stall(x->g);
}

static void init_tables(void) {
    static bool done;
    if (done) return;
    for (uint32_t i = 0; i < 4096; i++) {
        uint32_t hi = i >> 4, lo = i & 0xF;
        uintptr_t h = (uintptr_t)a64_arm_slow;
        switch (arm_class(i)) { /* the C decoder's own classes */
        case ARM_CLASS_DP:
            if ((hi & 0x20) || !(lo & 1)) /* an immediate or immediate-shift operand */
                h = a64_arm_dp_handlers[(hi >> 1) & 0xF][hi & 1][(hi & 0x20) ? 0 : 1 + (int)((lo >> 1) & 3)];
            break;
        case ARM_CLASS_BRANCH: h = (uintptr_t)a64_arm_branch; break;
        case ARM_CLASS_SINGLE: h = a64_arm_single_handlers[hi & 0x3F]; break;
        case ARM_CLASS_BLOCK: h = (uintptr_t)a64_arm_block; break;
        case ARM_CLASS_MUL: h = (uintptr_t)a64_arm_mul; break;
        case ARM_CLASS_HALF: h = (uintptr_t)a64_arm_half; break;
        case ARM_CLASS_BX: h = (uintptr_t)a64_arm_bx; break;
        default: break;
        }
        a64_arm_table[i] = h;
    }
    for (uint32_t i = 0; i < 1024; i++) {
        uint32_t op = i << 6;
        int k = -1;
        switch (thumb_class(i)) { /* the asm's order: see a64_thumb_handlers */
        case THUMB_CLASS_SHIFT_IMM: k = 0 + (int)((op & 0x1800) >> 11); break;
        case THUMB_CLASS_ADD_SUB: k = 4 + (int)((op & 0x0600) >> 9); break;
        case THUMB_CLASS_IMM: k = 8 + (int)((op & 0x1800) >> 11); break;
        case THUMB_CLASS_ALU: k = 12 + (int)((op & 0x03C0) >> 6); break;
        case THUMB_CLASS_HI_REG: k = 28 + (int)((op & 0x0300) >> 8); break;
        case THUMB_CLASS_REG_OFFSET: k = 32 + (int)((op & 0x0C00) >> 10); break;
        case THUMB_CLASS_REG_OFFSET_SIGNED: k = 36 + (int)((op & 0x0C00) >> 10); break;
        case THUMB_CLASS_IMM_OFFSET: k = 40 + (int)((op & 0x1800) >> 11); break;
        case THUMB_CLASS_HALF_IMM: k = 44 + (int)((op & 0x0800) >> 11); break;
        case THUMB_CLASS_SP_RELATIVE: k = 46 + (int)((op & 0x0800) >> 11); break;
        case THUMB_CLASS_LOAD_ADDRESS: k = 48 + (int)((op & 0x0800) >> 11); break;
        case THUMB_CLASS_PC_LOAD: k = 50; break;
        case THUMB_CLASS_SP_ADJUST: k = 51; break;
        case THUMB_CLASS_COND_BRANCH: k = 52; break;
        case THUMB_CLASS_BRANCH: k = 53; break;
        case THUMB_CLASS_BL_HIGH: k = 54; break;
        case THUMB_CLASS_BL_LOW: k = 55; break;
        case THUMB_CLASS_PUSH_POP: k = 56; break;
        case THUMB_CLASS_BLOCK: k = 57; break;
        default: break;
        }
        a64_thumb_table[i] = k < 0 ? (uintptr_t)a64_thumb_slow : a64_thumb_handlers[k];
    }
    done = true;
}

void a64_cpu_run(gba_t *g) {
    init_tables();
    a64_ctx_t x = {.g = g, .ewram = g->bus.ewram, .iwram = g->bus.iwram, .rom = g->cart.rom,
                   .rom_size = g->cart.rom_size, .now = &g->now, .pal = g->bus.pal,
                   .vram = g->bus.vram, .oam = g->bus.oam};
    while (g->now < sched_next(&g->sched) && !g->irq.halted && !g->irq.stopped) {
        prepare(&x);
        int r = (g->cpu.cpsr & CPSR_T) ? a64_run_thumb(&x, g->cpu.r, (uint8_t *)&g->bus.waitcnt)
                                       : a64_run_arm(&x, g->cpu.r, (uint8_t *)&g->bus.waitcnt);
        if (r == 2) continue; /* a BX into the other state: on in that loop */
        if (g->now < sched_next(&g->sched) && !g->irq.halted && !g->irq.stopped) cpu_step(g);
    }
}
