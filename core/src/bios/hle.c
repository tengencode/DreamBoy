#include "bios/hle.h"

#include <string.h>

#include "bios/hle_internal.h"
#include "gba_internal.h"

/* Assembled from bios_stub.S by tools/gen_bios_stub.py. */
static const uint8_t bios_stub[] = {
#include "bios_stub.inc"
};

static uint32_t bios_word(const gba_t *g, uint32_t addr) {
    const uint8_t *p = &g->bus.bios[addr];
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* The BIOS leaves the BG2/BG3 affine matrices at identity (PA = PD = 1.0). */
static void set_affine_identity(gba_t *g) {
    static const uint32_t regs[] = {0x020, 0x026, 0x030, 0x036};
    for (int i = 0; i < 4; i++) {
        g->io.raw[regs[i]] = 0x00;
        g->io.raw[regs[i] + 1] = 0x01;
    }
}

/* Stack pointers and modes the real BIOS sets up before entering the cartridge. */
static void set_post_bios_registers(arm_cpu_t *c) {
    cpu_write_cpsr(c, MODE_SVC);
    c->r[13] = 0x03007FE0;
    c->r[14] = 0;
    c->spsr[BANK_SVC] = 0;
    cpu_write_cpsr(c, MODE_IRQ);
    c->r[13] = 0x03007FA0;
    c->r[14] = 0;
    c->spsr[BANK_IRQ] = 0;
    cpu_write_cpsr(c, MODE_SYS);
    c->r[13] = 0x03007F00;
    c->r[14] = 0;
    for (int i = 0; i <= 12; i++) c->r[i] = 0;
}

void hle_boot(gba_t *g) {
    memset(g->bus.bios, 0, BIOS_SIZE);
    memcpy(g->bus.bios, bios_stub, sizeof bios_stub);
    set_post_bios_registers(&g->cpu);
    g->cpu.r[15] = 0x08000000;
    g->io.postflg = 1;
    set_affine_identity(g);
    g->bus.bios_latch = bios_word(g, 0xE4); /* the retail BIOS's last fetch before the jump */
}

/* SWI 0x00: the flag at 0x03007FFA picks EWRAM or ROM as the restart address. */
static void soft_reset(gba_t *g) {
    bool to_ewram = g->bus.iwram[0x7FFA] != 0;
    memset(&g->bus.iwram[IWRAM_SIZE - 0x200], 0, 0x200);
    set_post_bios_registers(&g->cpu);
    cpu_branch(&g->cpu, to_ewram ? 0x02000000 : 0x08000000);
}

/* SWI 0x01: r0 selects which memories and register groups to clear. */
static void register_ram_reset(gba_t *g) {
    uint32_t flags = g->cpu.r[0];
    bus_t *b = &g->bus;
    g->ppu.dispcnt = 0x0080; /* always: forced blank */
    if (flags & 0x01) memset(b->ewram, 0, sizeof b->ewram);
    if (flags & 0x02) memset(b->iwram, 0, IWRAM_SIZE - 0x200); /* the top 0x200 bytes are BIOS state */
    if (flags & 0x04) memset(b->pal, 0, sizeof b->pal);
    if (flags & 0x08) memset(b->vram, 0, sizeof b->vram);
    if (flags & 0x10) memset(b->oam, 0, sizeof b->oam);
    if (flags & 0x20) memset(&g->io.raw[0x120], 0, 0x160 - 0x120); /* serial */
    if (flags & 0x40) { /* sound */
        memset(&g->io.raw[0x060], 0, 0x0B0 - 0x060);
        apu_reset(g);
    }
    if (flags & 0x80) {                                             /* all other registers */
        memset(&g->io.raw[0x000], 0, 0x060);
        memset(&g->io.raw[0x0B0], 0, 0x120 - 0x0B0);
        g->ppu.dispstat = 0;
        g->irq.ie = 0;
        g->irq.if_ = 0;
        g->irq.ime = 0;
        irq_changed(g);
        bus_set_waitcnt(g, 0);
        set_affine_identity(g);
    }
}

/* SWI 0x19: jump straight to the target bias level instead of ramping. */
static void sound_bias(gba_t *g) {
    uint32_t bias = bus_read16(g, 0x04000088, false);
    bias = (bias & ~0x3FFu) | (g->cpu.r[0] ? 0x200u : 0);
    bus_write16(g, 0x04000088, (uint16_t)bias, false);
}

/* SWI 0x1F MidiKey2Freq: r0 = WaveData (its frequency at +4), r1 = MIDI key, r2 = fine adjust (1/256 key):
 * r0 = freq / 2^((180 - key - fine/256) / 12), in whole numbers like the BIOS: a key's factor is
 * 2^(31 + note/12) >> (14 - octave), the fine adjust goes in a straight line to the next key's, and keys above
 * 178 count as 178 with the fine adjust at 255. */
static void midi_key_to_freq(gba_t *g) {
    static const uint32_t note_factor[12] = { /* round(2^(31 + n/12)) */
        2147483648u, 2275179671u, 2410468894u, 2553802834u, 2705659852u, 2866546760u,
        3037000500u, 3217589947u, 3408917802u, 3611622603u, 3826380858u, 4053909305u};
    arm_cpu_t *c = &g->cpu;
    uint32_t key = c->r[1] & 0xFF, fine = c->r[2] & 0xFF;
    if (key > 178) {
        key = 178;
        fine = 255;
    }
    uint32_t low = note_factor[key % 12] >> (14 - key / 12);
    uint32_t high = note_factor[(key + 1) % 12] >> (14 - (key + 1) / 12);
    uint32_t step = (uint32_t)(((uint64_t)(high - low) * (fine << 24)) >> 32);
    uint32_t freq = bus_read32(g, c->r[0] + 4, false);
    c->r[0] = (uint32_t)(((uint64_t)freq * (low + step)) >> 32);
}

void hle_swi(gba_t *g, uint32_t number) {
    arm_cpu_t *c = &g->cpu;
    /* Routine time: mGBA's formulas minus the 8 cycles our call path already spends; others ~20. */
    uint32_t cost = 20;
    switch (number) {
    case 0x06: cost = hle_div_cycles((int32_t)c->r[0], (int32_t)c->r[1]) - 8; break;
    case 0x07: cost = hle_div_cycles((int32_t)c->r[1], (int32_t)c->r[0]) - 8; break;
    case 0x08: cost = hle_sqrt_cycles(c->r[0]) - 8; break;
    case 0x09: cost = hle_arctan_cycles((int32_t)c->r[0]) - 8; break;
    case 0x0A: cost = hle_arctan2_cycles((int32_t)c->r[0], (int32_t)c->r[1]) - 8; break;
    case 0x0B:
    case 0x0C: cost = 8; break; /* plus one internal cycle per unit, charged by the copy */
    default: break;
    }
    bus_idle(g, cost);
    switch (number) {
    case 0x00: soft_reset(g); break;
    case 0x01: register_ram_reset(g); break;
    case 0x02: irq_halt(g); break;
    case 0x03: irq_stop(g); break;
    case 0x06: hle_div(g, (int32_t)c->r[0], (int32_t)c->r[1]); break;
    case 0x07: hle_div(g, (int32_t)c->r[1], (int32_t)c->r[0]); break; /* DivArm: operands swapped */
    case 0x08: c->r[0] = hle_sqrt(c->r[0]); break;
    case 0x09: {
        int32_t r1, r3;
        c->r[0] = (uint32_t)hle_arctan((int32_t)c->r[0], &r1, &r3);
        c->r[1] = (uint32_t)r1;
        c->r[3] = (uint32_t)r3;
        break;
    }
    case 0x0A: {
        int32_t r1 = (int32_t)c->r[1];
        c->r[0] = hle_arctan2((int32_t)c->r[0], (int32_t)c->r[1], &r1);
        c->r[1] = (uint32_t)r1;
        c->r[3] = 0x170;
        break;
    }
    case 0x0B: hle_cpu_set(g); break;
    case 0x0C: hle_cpu_fast_set(g); break;
    case 0x0D: c->r[0] = 0xBAAE187F; break; /* GetBiosChecksum: the retail BIOS value */
    case 0x0E: hle_bg_affine_set(g); break;
    case 0x0F: hle_obj_affine_set(g); break;
    case 0x10: hle_bit_unpack(g); break;
    case 0x11: hle_lz77(g, false); break;
    case 0x12: hle_lz77(g, true); break;
    case 0x13: hle_huffman(g); break;
    case 0x14: hle_rl(g, false); break;
    case 0x15: hle_rl(g, true); break;
    case 0x16: hle_diff8(g, false); break;
    case 0x17: hle_diff8(g, true); break;
    case 0x18: hle_diff16(g); break;
    case 0x19: sound_bias(g); break;
    case 0x1F: midi_key_to_freq(g); break;
    default: NOTE(g, NOTE_BIOS_CALL, number); break; /* 0x04/0x05 run in the stub; sound-driver and multiboot SWIs are no-ops */
    }
}
