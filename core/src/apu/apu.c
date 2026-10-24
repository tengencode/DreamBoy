#include "apu/apu.h"

#include <string.h>

#include "gba_internal.h"

enum {
    REG_PSG_FIRST = 0x060,
    REG_SOUNDCNT_L = 0x080,
    REG_SOUNDCNT_H = 0x082,
    REG_SOUNDCNT_X = 0x084,
    REG_SOUNDBIAS = 0x088,
    REG_WAVE_RAM = 0x090,
    REG_FIFO_A = 0x0A0,
};

static uint16_t raw16(const gba_t *g, uint32_t off) {
    return (uint16_t)(g->io.raw[off] | g->io.raw[off + 1] << 8);
}

static void set_raw16(gba_t *g, uint32_t off, uint16_t v) {
    g->io.raw[off] = (uint8_t)v;
    g->io.raw[off + 1] = (uint8_t)(v >> 8);
}

static bool master_on(const gba_t *g) {
    return (g->io.raw[REG_SOUNDCNT_X] & 0x80) != 0;
}

void apu_reset(gba_t *g) {
    memset(&g->apu, 0, sizeof g->apu);
    psg_reset(&g->apu.psg, g->now);
    set_raw16(g, REG_SOUNDBIAS, 0x0200);
    sched_add(&g->sched, EV_APU_SAMPLE, g->now + APU_SAMPLE_CYCLES);
    sched_add(&g->sched, EV_APU_FRAME, g->now + APU_FRAME_SEQ_CYCLES);
}

/* ---- DMA sound FIFOs ---- */

static void fifo_push(apu_fifo_t *f, uint8_t v) {
    if (f->count == 32) return; /* full: the byte is lost */
    f->data[(f->read + f->count) & 31] = (int8_t)v;
    f->count++;
}

static void fifo_clear(apu_fifo_t *f) {
    f->read = 0;
    f->count = 0;
}

void apu_write_fifo8(gba_t *g, int fifo, uint8_t value) {
    fifo_push(&g->apu.fifo[fifo], value);
}

void apu_on_timer_overflow(gba_t *g, int timer) {
    if (!master_on(g)) return;
    uint16_t cnt_h = raw16(g, REG_SOUNDCNT_H);
    for (int i = 0; i < 2; i++) {
        if ((int)((cnt_h >> (10 + 4 * i)) & 1) != timer) continue;
        apu_fifo_t *f = &g->apu.fifo[i];
        if (f->count) { /* an empty FIFO keeps playing its last sample */
            f->sample = f->data[f->read];
            f->read = (uint8_t)((f->read + 1) & 31);
            f->count--;
        }
        if (f->count <= 16) dma_sound_request(g, 0x04000000u + REG_FIFO_A + 4u * (uint32_t)i);
    }
}

/* ---- mixer and output ---- */

int16_t apu_mix_side(uint16_t soundcnt_l, uint16_t soundcnt_h, uint16_t bias, const uint8_t psg[4],
                     const int8_t fifo[2], bool left) {
    uint32_t enables = (left ? soundcnt_l >> 12 : soundcnt_l >> 8) & 0xF;
    int volume = left ? (soundcnt_l >> 4) & 7 : soundcnt_l & 7;
    int sum = 0;
    for (int n = 0; n < 3; n++) {
        if (enables & (1u << n)) sum += psg[n];
    }
    sum <<= 3;
    if (enables & 8) sum += psg[3] << 3;
    sum *= 1 + volume;
    int ratio = soundcnt_h & 3;
    sum >>= 4 - (ratio == 3 ? 2 : ratio); /* 25% / 50% / 100% (3 is not allowed; treated as 100%) */
    for (int i = 0; i < 2; i++) {
        uint32_t sides = soundcnt_h >> (8 + 4 * i); /* bit 0 right, bit 1 left */
        if (!(sides & (left ? 2u : 1u))) continue;
        int full = (soundcnt_h >> (2 + i)) & 1;
        sum += (fifo[i] * 4) >> (full ? 0 : 1);
    }
    sum += bias;
    if (sum < 0) sum = 0;
    if (sum > 0x3FF) sum = 0x3FF; /* the 10-bit output */
    int out = (sum - bias) * 64;
    if (out > 32767) out = 32767;
    if (out < -32768) out = -32768;
    return (int16_t)out;
}

static void ring_push(apu_t *a, int16_t left, int16_t right) {
    if (a->ring_count == APU_RING_FRAMES) return; /* nobody is reading: drop */
    uint32_t w = (a->ring_read + a->ring_count) % APU_RING_FRAMES;
    a->ring[w * 2] = left;
    a->ring[w * 2 + 1] = right;
    a->ring_count++;
}

void apu_on_sample(gba_t *g, uint64_t when) {
    int16_t left = 0, right = 0;
    psg_advance(g, when);
    if (master_on(g)) {
        uint8_t psg[4];
        for (int n = 0; n < 4; n++) psg[n] = psg_output(g, n);
        const int8_t fifo[2] = {g->apu.fifo[0].sample, g->apu.fifo[1].sample};
        uint16_t cnt_l = raw16(g, REG_SOUNDCNT_L), cnt_h = raw16(g, REG_SOUNDCNT_H);
        uint16_t bias = raw16(g, REG_SOUNDBIAS) & 0x3FE;
        left = apu_mix_side(cnt_l, cnt_h, bias, psg, fifo, true);
        right = apu_mix_side(cnt_l, cnt_h, bias, psg, fifo, false);
    }
    ring_push(&g->apu, left, right);
    sched_add(&g->sched, EV_APU_SAMPLE, when + APU_SAMPLE_CYCLES);
}

void apu_on_frame_seq(gba_t *g, uint64_t when) {
    psg_advance(g, when);
    if (master_on(g)) psg_frame_step(g);
    sched_add(&g->sched, EV_APU_FRAME, when + APU_FRAME_SEQ_CYCLES);
}

size_t apu_read(gba_t *g, int16_t *out, size_t max_frames) {
    apu_t *a = &g->apu;
    size_t n = 0;
    while (n < max_frames && a->ring_count) {
        out[n * 2] = a->ring[a->ring_read * 2];
        out[n * 2 + 1] = a->ring[a->ring_read * 2 + 1];
        a->ring_read = (a->ring_read + 1) % APU_RING_FRAMES;
        a->ring_count--;
        n++;
    }
    return n;
}

void apu_stopped_frame(gba_t *g) {
    apu_t *a = &g->apu;
    a->silence_cycles += PPU_FRAME_CYCLES;
    while (a->silence_cycles >= APU_SAMPLE_CYCLES) {
        ring_push(a, 0, 0);
        a->silence_cycles -= APU_SAMPLE_CYCLES;
    }
}

/* ---- registers ---- */

uint16_t apu_read16(gba_t *g, uint32_t off) {
    if (off == REG_SOUNDCNT_X) return (uint16_t)((raw16(g, off) & 0x80) | psg_status(g));
    if (off >= REG_WAVE_RAM && off < REG_WAVE_RAM + 16) return psg_wave_read16(g, off);
    return raw16(g, off);
}

void apu_write16(gba_t *g, uint32_t off, uint16_t value) {
    psg_advance(g, g->now); /* the change takes effect from this cycle */
    if (off >= REG_FIFO_A) {
        apu_fifo_t *f = &g->apu.fifo[(off - REG_FIFO_A) >> 2];
        fifo_push(f, (uint8_t)value);
        fifo_push(f, (uint8_t)(value >> 8));
        return;
    }
    if (off >= REG_WAVE_RAM && off < REG_WAVE_RAM + 16) {
        psg_wave_write16(g, off, value);
        return;
    }
    if (off < REG_SOUNDCNT_H && !master_on(g)) return; /* 0x060-0x081 are locked while sound is off */
    uint16_t old = raw16(g, off);
    switch (off) {
    case REG_SOUNDCNT_H:
        if (value & 0x0800) fifo_clear(&g->apu.fifo[0]);
        if (value & 0x8000) fifo_clear(&g->apu.fifo[1]);
        set_raw16(g, off, value & 0x770F); /* the reset bits read back as 0 */
        break;
    case REG_SOUNDCNT_X:
        set_raw16(g, off, value & 0x0080);
        if ((old & 0x80) && !(value & 0x80)) { /* sound off: PSG registers cleared, channels stopped */
            memset(&g->io.raw[REG_PSG_FIRST], 0, REG_SOUNDCNT_H - REG_PSG_FIRST);
            psg_reset(&g->apu.psg, g->now);
        } else if (!(old & 0x80) && (value & 0x80)) {
            g->apu.psg.step = 0; /* the sequencer restarts at step 0 */
        }
        break;
    default:
        set_raw16(g, off, value);
        if (off < REG_SOUNDCNT_L) psg_write(g, off, value);
        break;
    }
}
