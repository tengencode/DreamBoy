#include "apu/psg.h"

#include <string.h>

#include "gba_internal.h"

enum { SQUARE1, SQUARE2, WAVE, NOISE };

/* 8-step duty patterns, step 0 in the top bit: 12.5%, 25%, 50%, 75%. */
static const uint8_t duty_pattern[4] = {0x01, 0x81, 0x87, 0x7E};
/* Length/envelope/duty register (the wave channel's is its length/volume register). */
static const uint32_t env_reg[4] = {0x062, 0x068, 0x072, 0x078};
/* Control register with the length-enable (bit 14) and trigger (bit 15) bits. */
static const uint32_t ctl_reg[4] = {0x064, 0x06C, 0x074, 0x07C};

static uint16_t reg(const gba_t *g, uint32_t off) {
    return (uint16_t)(g->io.raw[off] | g->io.raw[off + 1] << 8);
}

static void set_reg(gba_t *g, uint32_t off, uint16_t v) {
    g->io.raw[off] = (uint8_t)v;
    g->io.raw[off + 1] = (uint8_t)(v >> 8);
}

void psg_reset(psg_t *p, uint64_t now) {
    memset(p->ch, 0, sizeof p->ch);
    p->step = 0;
    p->last = now;
}

/* Cycles per waveform step. */
static uint32_t period(const gba_t *g, int n) {
    const psg_channel_t *c = &g->apu.psg.ch[n];
    if (n == SQUARE1 || n == SQUARE2) return 16u * (2048u - c->freq);
    if (n == WAVE) return 8u * (2048u - c->freq);
    uint16_t v = reg(g, 0x07C);
    uint32_t r = v & 7, s = (v >> 4) & 0xF;
    return r ? (64u * r) << s : 32u << s;
}

static bool dac_on(const gba_t *g, int n) {
    if (n == WAVE) return (reg(g, 0x070) & 0x80) != 0;
    return (reg(g, env_reg[n]) & 0xF800) != 0; /* initial volume 0 and fading down = DAC off */
}

/* Waveform steps during the next `cycles` cycles; keeps c->timer in 1..per. */
static uint32_t clock_steps(psg_channel_t *c, uint32_t cycles, uint32_t per) {
    if (c->timer > cycles) {
        c->timer -= cycles;
        return 0;
    }
    cycles -= c->timer;
    c->timer = per - cycles % per;
    return 1 + cycles / per;
}

static void noise_step(psg_channel_t *c, bool narrow) {
    uint16_t bit = (c->lfsr ^ (c->lfsr >> 1)) & 1;
    c->lfsr = (uint16_t)((c->lfsr >> 1) | bit << 14);
    if (narrow) c->lfsr = (uint16_t)((c->lfsr & ~0x40u) | bit << 6);
}

void psg_advance(gba_t *g, uint64_t now) {
    psg_t *p = &g->apu.psg;
    if (now <= p->last) return;
    uint64_t gap = now - p->last;
    uint32_t elapsed = gap > 0x40000000u ? 0x40000000u : (uint32_t)gap;
    p->last = now;
    for (int n = 0; n < 4; n++) {
        psg_channel_t *c = &p->ch[n];
        if (!c->on) continue;
        uint32_t steps = clock_steps(c, elapsed, period(g, n));
        if (!steps) continue;
        if (n == WAVE) {
            uint32_t samples = (reg(g, 0x070) & 0x20) ? 64 : 32;
            c->pos = (uint8_t)((c->pos + steps) % samples);
        } else if (n == NOISE) {
            bool narrow = (reg(g, 0x07C) & 0x08) != 0;
            for (uint32_t i = 0; i < steps; i++) noise_step(c, narrow);
        } else {
            c->pos = (uint8_t)((c->pos + steps) & 7);
        }
    }
}

/* ---- frame sequencer ---- */

static void clock_length(gba_t *g, int n) {
    psg_channel_t *c = &g->apu.psg.ch[n];
    if (!(reg(g, ctl_reg[n]) & 0x4000) || c->length == 0) return;
    if (--c->length == 0) c->on = false;
}

static void clock_envelope(gba_t *g, int n) {
    psg_channel_t *c = &g->apu.psg.ch[n];
    uint16_t env = reg(g, env_reg[n]);
    uint8_t step_time = (env >> 8) & 7;
    if (!step_time) return; /* frozen */
    if (c->env_timer > 1) {
        c->env_timer--;
        return;
    }
    c->env_timer = step_time;
    if ((env & 0x800) && c->volume < 15) c->volume++;
    else if (!(env & 0x800) && c->volume > 0) c->volume--;
}

static uint16_t sweep_target(const gba_t *g, uint16_t freq) {
    uint16_t s = reg(g, 0x060);
    uint16_t delta = (uint16_t)(freq >> (s & 7));
    return (s & 0x08) ? (uint16_t)(freq - delta) : (uint16_t)(freq + delta);
}

static void clock_sweep(gba_t *g) {
    psg_channel_t *c = &g->apu.psg.ch[SQUARE1];
    uint16_t s = reg(g, 0x060);
    uint8_t time = (s >> 4) & 7;
    if (!c->sweep_on) return;
    if (c->sweep_timer > 1) {
        c->sweep_timer--;
        return;
    }
    c->sweep_timer = time ? time : 8;
    if (!time) return;
    uint16_t next = sweep_target(g, c->freq);
    if (next > 2047) {
        c->on = false;
        return;
    }
    if (s & 7) {
        c->freq = next;
        set_reg(g, 0x064, (uint16_t)((reg(g, 0x064) & 0xF800) | next));
        if (sweep_target(g, next) > 2047) c->on = false; /* checked again with the new value */
    }
}

void psg_frame_step(gba_t *g) {
    psg_t *p = &g->apu.psg;
    uint8_t s = p->step;
    if (!(s & 1)) {
        for (int n = 0; n < 4; n++) clock_length(g, n);
    }
    if (s == 2 || s == 6) clock_sweep(g);
    if (s == 7) {
        clock_envelope(g, SQUARE1);
        clock_envelope(g, SQUARE2);
        clock_envelope(g, NOISE);
    }
    p->step = (uint8_t)((s + 1) & 7);
}

/* ---- registers ---- */

static void trigger(gba_t *g, int n) {
    psg_channel_t *c = &g->apu.psg.ch[n];
    c->on = dac_on(g, n);
    if (c->length == 0) c->length = n == WAVE ? 256 : 64;
    if (n == WAVE) {
        c->pos = 0;
    } else {
        uint16_t env = reg(g, env_reg[n]);
        c->volume = (uint8_t)(env >> 12);
        c->env_timer = (env >> 8) & 7;
    }
    if (n == NOISE) c->lfsr = 0x7FFF;
    c->timer = period(g, n);
    if (n == SQUARE1) {
        uint16_t s = reg(g, 0x060);
        uint8_t time = (s >> 4) & 7;
        c->sweep_timer = time ? time : 8;
        c->sweep_on = time || (s & 7);
        if ((s & 7) && sweep_target(g, c->freq) > 2047) c->on = false;
    }
}

void psg_write(gba_t *g, uint32_t off, uint16_t value) {
    psg_t *p = &g->apu.psg;
    switch (off) {
    case 0x062:
    case 0x068:
    case 0x078: {
        int n = off == 0x062 ? SQUARE1 : off == 0x068 ? SQUARE2 : NOISE;
        p->ch[n].length = (uint16_t)(64 - (value & 0x3F));
        if (!dac_on(g, n)) p->ch[n].on = false;
        break;
    }
    case 0x070:
        if (!(value & 0x80)) p->ch[WAVE].on = false;
        break;
    case 0x072: p->ch[WAVE].length = (uint16_t)(256 - (value & 0xFF)); break;
    case 0x064:
    case 0x06C:
    case 0x074: {
        int n = off == 0x064 ? SQUARE1 : off == 0x06C ? SQUARE2 : WAVE;
        p->ch[n].freq = value & 0x7FF;
        set_reg(g, off, value & 0x7FFF); /* the trigger bit is not kept */
        if (value & 0x8000) trigger(g, n);
        break;
    }
    case 0x07C:
        set_reg(g, off, value & 0x7FFF);
        if (value & 0x8000) trigger(g, NOISE);
        break;
    default: break;
    }
}

/* ---- output ---- */

uint8_t psg_output(const gba_t *g, int n) {
    const psg_t *p = &g->apu.psg;
    const psg_channel_t *c = &p->ch[n];
    if (!c->on) return 0;
    if (n == SQUARE1 || n == SQUARE2) {
        uint8_t duty = (reg(g, env_reg[n]) >> 6) & 3;
        return ((duty_pattern[duty] >> (7 - c->pos)) & 1) ? c->volume : 0;
    }
    if (n == NOISE) return (c->lfsr & 1) ? 0 : c->volume;
    uint16_t l = reg(g, 0x070), h = reg(g, 0x072);
    uint32_t bank = (((l >> 6) & 1) + c->pos / 32u) & 1;
    uint32_t i = c->pos % 32u;
    uint8_t byte = p->wave[bank][i / 2];
    uint8_t s = (i & 1) ? byte & 0xF : byte >> 4;
    if (h & 0x8000) return (uint8_t)((s * 3) >> 2); /* forced 75% */
    switch ((h >> 13) & 3) {
    case 1: return s;
    case 2: return s >> 1;
    case 3: return s >> 2;
    default: return 0;
    }
}

uint8_t psg_status(const gba_t *g) {
    uint8_t s = 0;
    for (int n = 0; n < 4; n++) {
        if (g->apu.psg.ch[n].on) s |= (uint8_t)(1 << n);
    }
    return s;
}

/* The CPU reads and writes the wave bank that is not selected for playback (SOUND3CNT_L bit 6). */
static uint8_t *cpu_wave_bank(const gba_t *g) {
    uint32_t bank = ((reg(g, 0x070) >> 6) & 1) ^ 1;
    return (uint8_t *)g->apu.psg.wave[bank];
}

uint16_t psg_wave_read16(const gba_t *g, uint32_t off) {
    const uint8_t *b = cpu_wave_bank(g) + (off - 0x090);
    return (uint16_t)(b[0] | b[1] << 8);
}

void psg_wave_write16(gba_t *g, uint32_t off, uint16_t value) {
    uint8_t *b = cpu_wave_bank(g) + (off - 0x090);
    b[0] = (uint8_t)value;
    b[1] = (uint8_t)(value >> 8);
}
