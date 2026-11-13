/* Sound: two square channels (the first with a frequency sweep), a wave channel playing 32 4-bit samples from
 * wave RAM, and a noise channel. A 512 Hz sequencer (clocked by the divider) drives the length counters, the
 * sweep and the volume envelopes. The mixed signal is averaged over every 128 clock ticks into one 32,768 Hz
 * stereo sample. All integer maths, so every platform makes the same samples. */
#include <string.h>

#include "gb_internal.h"

enum { NR10 = 0x10, NR11, NR12, NR13, NR14, NR21 = 0x16, NR22, NR23, NR24, NR30, NR31, NR32, NR33, NR34,
       NR41 = 0x20, NR42, NR43, NR44, NR50, NR51, NR52 };

/* Bits that always read 1, for 0xFF10-0xFF26. */
static const uint8_t READ_MASK[0x17] = {0x80, 0x3F, 0x00, 0xFF, 0xBF, 0xFF, 0x3F, 0x00, 0xFF, 0xBF, 0x7F, 0xFF,
                                        0x9F, 0xFF, 0xBF, 0xFF, 0xFF, 0x00, 0x00, 0xBF, 0x00, 0x00, 0x70};
static const uint8_t DUTY[4] = {0x01, 0x81, 0x87, 0x7E}; /* 12.5 %, 25 %, 50 %, 75 %: bit n = step n */

#define REG(a, r) ((a)->reg[(r) - NR10])

static int max_length(int ch) {
    return ch == 2 ? 256 : 64;
}

/* The length counters are clocked on the even sequencer steps; "the first half" is when the next step is odd. */
static bool first_half(const gb_apu_t *a) {
    return a->seq_step & 1;
}

static int period(const gb_apu_t *a, int ch) {
    const gb_chan_t *c = &a->ch[ch];
    if (ch == 3) {
        static const uint8_t divisor[8] = {8, 16, 32, 48, 64, 80, 96, 112};
        return divisor[REG(a, NR43) & 7] << (REG(a, NR43) >> 4);
    }
    return (2048 - c->freq) * (ch == 2 ? 2 : 4);
}

/* The sweep's next frequency; past 2047 the channel is switched off. */
static int sweep_next(gb_apu_t *a) {
    gb_chan_t *c = &a->ch[0];
    int delta = a->sweep_freq >> (REG(a, NR10) & 7), next;
    if (REG(a, NR10) & 8) {
        next = a->sweep_freq - delta;
        a->sweep_negged = 1;
    } else {
        next = a->sweep_freq + delta;
    }
    if (next > 2047) c->on = 0;
    return next;
}

static void clock_lengths(gb_apu_t *a) {
    for (int i = 0; i < 4; i++) {
        gb_chan_t *c = &a->ch[i];
        if (c->length_on && c->length && --c->length == 0) c->on = 0;
    }
}

static void clock_sweep(gb_apu_t *a) {
    int every = (REG(a, NR10) >> 4) & 7;
    if (--a->sweep_timer != 0) return;
    a->sweep_timer = every ? (uint8_t)every : 8;
    if (!a->sweep_on || !every) return;
    int next = sweep_next(a);
    if (next <= 2047 && (REG(a, NR10) & 7)) {
        a->sweep_freq = a->ch[0].freq = (uint16_t)next;
        sweep_next(a); /* and the check again, with the new frequency */
    }
}

static void clock_envelopes(gb_apu_t *a) {
    static const uint8_t regs[4] = {NR12, NR22, 0, NR42};
    for (int i = 0; i < 4; i++) {
        gb_chan_t *c = &a->ch[i];
        if (i == 2) continue;
        int every = REG(a, regs[i]) & 7;
        if (!every || --c->env_timer != 0) continue;
        c->env_timer = (uint8_t)every;
        if (REG(a, regs[i]) & 8) {
            if (c->volume < 15) c->volume++;
        } else if (c->volume > 0) {
            c->volume--;
        }
    }
}

/* One step of the 512 Hz sequencer: lengths on 0 2 4 6, the sweep on 2 6, envelopes on 7. */
void gb_apu_sequencer(gb_t *g) {
    gb_apu_t *a = &g->apu;
    if (!a->power) return;
    int step = a->seq_step;
    a->seq_step = (uint8_t)((step + 1) & 7);
    if (!(step & 1)) clock_lengths(a);
    if (step == 2 || step == 6) clock_sweep(a);
    if (step == 7) clock_envelopes(a);
}

/* A channel's level now, 0-15. */
static int level(const gb_apu_t *a, int ch) {
    const gb_chan_t *c = &a->ch[ch];
    if (!c->on) return 0;
    switch (ch) {
    case 0:
    case 1: return (DUTY[REG(a, ch ? NR21 : NR11) >> 6] >> c->pos & 1) ? c->volume : 0;
    case 2: {
        int code = (REG(a, NR32) >> 5) & 3;
        return code ? a->wave_sample >> (code - 1) : 0;
    }
    default: return (~a->lfsr & 1) ? c->volume : 0;
    }
}

static void put_sample(gb_t *g, int16_t left, int16_t right) {
    if (g->audio_count == GB_AUDIO_FRAMES) { /* nobody is reading: the oldest goes */
        g->audio_read = (g->audio_read + 1) % GB_AUDIO_FRAMES;
        g->audio_count--;
    }
    uint32_t at = (g->audio_read + g->audio_count) % GB_AUDIO_FRAMES;
    g->audio[at * 2] = left;
    g->audio[at * 2 + 1] = right;
    g->audio_count++;
}

/* One output sample from the sum over 128 ticks, through the filter that takes out the steady (DC) part. */
static int16_t filter(int32_t sum, int32_t *cap) {
    int32_t in = sum * 128; /* full scale: 480 * 128 ticks * 128 = +-30720 * 256 */
    int32_t out = in - *cap;
    *cap += (int32_t)((int64_t)out * 351 / 65536); /* the capacitor's charge factor, 0.999958 a tick, over 128 */
    out /= 256;
    return (int16_t)(out > 32767 ? 32767 : out < -32768 ? -32768 : out);
}

void gb_apu_tick(gb_t *g, int dots) {
    gb_apu_t *a = &g->apu;
    a->wave_just_read = 0;
    if (a->power) {
        for (int i = 0; i < 4; i++) {
            gb_chan_t *c = &a->ch[i];
            if (!c->on) continue;
            c->timer -= dots;
            while (c->timer <= 0) {
                c->timer += period(a, i);
                if (i < 2) {
                    c->pos = (c->pos + 1) & 7;
                } else if (i == 2) {
                    c->pos = (c->pos + 1) & 31;
                    uint8_t byte = a->wave[c->pos / 2];
                    a->wave_sample = (c->pos & 1) ? byte & 0x0F : byte >> 4;
                    a->wave_just_read = 1;
                } else if ((REG(a, NR43) >> 4) < 14) {
                    int bit = (a->lfsr ^ a->lfsr >> 1) & 1;
                    a->lfsr = (uint16_t)(a->lfsr >> 1 | bit << 14);
                    if (REG(a, NR43) & 8) a->lfsr = (uint16_t)((a->lfsr & ~0x40) | bit << 6);
                }
            }
        }
        int left = 0, right = 0;
        for (int i = 0; i < 4; i++) {
            if (!a->ch[i].dac) continue; /* a channel whose converter is off adds nothing */
            int v = 2 * level(a, i) - 15;
            if (REG(a, NR51) & (0x10 << i)) left += v;
            if (REG(a, NR51) & (0x01 << i)) right += v;
        }
        a->sum_left += left * (((REG(a, NR50) >> 4) & 7) + 1) * dots;
        a->sum_right += right * ((REG(a, NR50) & 7) + 1) * dots;
    }
    a->sum_dots = (uint8_t)(a->sum_dots + dots);
    if (a->sum_dots >= 128) {
        a->sum_dots -= 128;
        put_sample(g, filter(a->sum_left, &a->cap_left), filter(a->sum_right, &a->cap_right));
        a->sum_left = a->sum_right = 0;
    }
}

size_t gb_audio_read(gb_t *g, int16_t *out, size_t max_frames) {
    size_t n = g->audio_count < max_frames ? g->audio_count : max_frames;
    for (size_t i = 0; i < n; i++) {
        uint32_t at = (g->audio_read + i) % GB_AUDIO_FRAMES;
        out[i * 2] = g->audio[at * 2];
        out[i * 2 + 1] = g->audio[at * 2 + 1];
    }
    g->audio_read = (uint32_t)((g->audio_read + n) % GB_AUDIO_FRAMES);
    g->audio_count -= (uint32_t)n;
    return n;
}

/* The wave channel reads wave RAM as it plays; meanwhile the CPU reaches only the byte being played, and on the
 * original Game Boy only in the very cycle the channel reads it. Returns the index, or -1 for "not reachable". */
static int wave_index(const gb_t *g, uint16_t addr) {
    const gb_apu_t *a = &g->apu;
    if (!a->ch[2].on) return addr & 0x0F;
    if (!g->info.color && !a->wave_just_read) return -1;
    return a->ch[2].pos / 2;
}

uint8_t gb_apu_read(const gb_t *g, uint16_t addr) {
    const gb_apu_t *a = &g->apu;
    if (addr >= 0xFF30) {
        int i = wave_index(g, addr);
        return i < 0 ? 0xFF : a->wave[i];
    }
    if (addr > 0xFF26) return 0xFF;
    uint8_t v = a->reg[addr - 0xFF10] | READ_MASK[addr - 0xFF10];
    if (addr == 0xFF26) {
        v = (uint8_t)(0x70 | (a->power ? 0x80 : 0));
        for (int i = 0; i < 4; i++) v |= (uint8_t)(a->ch[i].on << i);
    }
    return v;
}

static void trigger(gb_t *g, int ch) {
    gb_apu_t *a = &g->apu;
    gb_chan_t *c = &a->ch[ch];
    static const uint8_t volume_regs[4] = {NR12, NR22, 0, NR42};
    c->on = c->dac;
    if (c->length == 0) { /* a spent counter starts again from the top; in the first half with the counter on,
                             one step is already gone */
        c->length = (uint16_t)max_length(ch);
        if (c->length_on && first_half(a)) c->length--;
    }
    c->timer = period(a, ch);
    if (ch != 2) {
        c->volume = REG(a, volume_regs[ch]) >> 4;
        c->env_timer = REG(a, volume_regs[ch]) & 7;
        if (!c->env_timer) c->env_timer = 8;
    }
    if (ch == 0) {
        int every = (REG(a, NR10) >> 4) & 7, shift = REG(a, NR10) & 7;
        a->sweep_freq = c->freq;
        a->sweep_timer = every ? (uint8_t)every : 8;
        a->sweep_on = every || shift;
        a->sweep_negged = 0;
        if (shift) sweep_next(a); /* the overflow check, at once */
    } else if (ch == 2) {
        c->pos = 0;
        c->timer += 6; /* the first sample comes a little late */
    } else if (ch == 3) {
        a->lfsr = 0x7FFF;
    }
}

/* NRx4: the frequency's top bits, the length switch, the trigger. */
static void write_control(gb_t *g, int ch, uint8_t value) {
    gb_apu_t *a = &g->apu;
    gb_chan_t *c = &a->ch[ch];
    c->freq = (uint16_t)((c->freq & 0xFF) | (value & 7) << 8);
    bool was_on = c->length_on;
    c->length_on = (value & 0x40) != 0;
    /* Switching the length counter on in the first half of its period clocks it once more. */
    if (!was_on && c->length_on && first_half(a) && c->length && --c->length == 0 && !(value & 0x80)) c->on = 0;
    if (value & 0x80) trigger(g, ch);
}

static void power_off(gb_apu_t *a, bool color) {
    uint16_t lengths[4];
    for (int i = 0; i < 4; i++) lengths[i] = a->ch[i].length;
    memset(a->reg, 0, sizeof a->reg);
    memset(a->ch, 0, sizeof a->ch);
    a->sweep_on = a->sweep_negged = 0;
    a->power = 0;
    if (!color) /* the original Game Boy keeps its length counters through power-off */
        for (int i = 0; i < 4; i++) a->ch[i].length = lengths[i];
}

void gb_apu_write(gb_t *g, uint16_t addr, uint8_t value) {
    gb_apu_t *a = &g->apu;
    if (addr >= 0xFF30) {
        int i = wave_index(g, addr);
        if (i >= 0) a->wave[i] = value;
        return;
    }
    if (addr > 0xFF26) return;
    int r = addr - 0xFF00;
    if (r == NR52) {
        if (!(value & 0x80) && a->power) {
            power_off(a, g->info.color);
        } else if ((value & 0x80) && !a->power) {
            a->power = 1;
            a->seq_step = 0;
            a->wave_sample = 0;
        }
        return;
    }
    if (!a->power) { /* off: registers ignore writes — except, on the original Game Boy, the length counters */
        if (g->info.color || (r != NR11 && r != NR21 && r != NR31 && r != NR41)) return;
        if (r != NR31) value &= 0x3F;
    }
    int ch = r < NR21 - 1 ? 0 : r < NR30 ? 1 : r < NR41 - 1 ? 2 : 3;
    gb_chan_t *c = &a->ch[ch];
    uint8_t old = a->reg[r - NR10];
    a->reg[r - NR10] = value;
    switch (r) {
    case NR10: /* leaving "down" after a downward sweep was worked out switches the channel off */
        if ((old & 8) && !(value & 8) && a->sweep_negged) a->ch[0].on = 0;
        break;
    case NR11:
    case NR21:
    case NR41: c->length = (uint16_t)(64 - (value & 0x3F)); break;
    case NR31: c->length = (uint16_t)(256 - value); break;
    case NR12:
    case NR22:
    case NR42:
        c->dac = (value & 0xF8) != 0;
        if (!c->dac) c->on = 0;
        break;
    case NR30:
        c->dac = (value & 0x80) != 0;
        if (!c->dac) c->on = 0;
        break;
    case NR13:
    case NR23:
    case NR33: c->freq = (uint16_t)((c->freq & 0x700) | value); break;
    case NR14:
    case NR24:
    case NR34:
    case NR44: write_control(g, ch, value); break;
    default: break;
    }
}

/* The registers as the boot ROM leaves them: power on, the first channel "playing" its faded-out chime. */
void gb_apu_reset(gb_t *g) {
    gb_apu_t *a = &g->apu;
    memset(a, 0, sizeof *a);
    a->power = 1;
    REG(a, NR10) = 0x80;
    REG(a, NR11) = 0x80;
    REG(a, NR12) = 0xF3;
    REG(a, NR50) = 0x77;
    REG(a, NR51) = 0xF3;
    a->ch[0].on = a->ch[0].dac = 1;
    a->ch[0].length = 64;
    a->ch[0].freq = 0x7C1;
    a->ch[0].timer = period(a, 0);
    a->ch[0].env_timer = 3;
    for (int i = 1; i < 4; i++) a->ch[i].length = (uint16_t)max_length(i);
    a->lfsr = 0x7FFF;
    g->audio_read = g->audio_count = 0;
}
