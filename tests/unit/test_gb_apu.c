/* Sound: registers, the four channels, the sequencer, the output samples. */
#include <stdlib.h>
#include <string.h>

#include "gbtestutil.h"
#include "test.h"

enum { NR10 = 0xFF10, NR11, NR12, NR13, NR14, NR21 = 0xFF16, NR22, NR23, NR24, NR30, NR31, NR32, NR33, NR34,
       NR41 = 0xFF20, NR42, NR43, NR44, NR50, NR51, NR52 };

/* A machine with sound switched off and on again (every register 0, nothing playing), both sides at full volume. */
static gb_t *quiet(void) {
    gb_t *g = gbtu_idle();
    gb_bus_write(g, NR52, 0x00);
    gb_bus_write(g, NR52, 0x80);
    gb_bus_write(g, NR50, 0x77);
    gb_bus_write(g, NR51, 0xFF);
    return g;
}

/* Runs `frames` frames and returns their left samples (the caller frees); *count gets how many. */
static int16_t *record(gb_t *g, int frames, size_t *count, int side) {
    int16_t *out = malloc(sizeof(int16_t) * (size_t)frames * 600), buf[1200];
    *count = 0;
    for (int f = 0; f < frames; f++) {
        gb_run_frame(g);
        size_t n = gb_audio_read(g, buf, 600);
        for (size_t i = 0; i < n; i++) out[(*count)++] = buf[i * 2 + side];
    }
    return out;
}

/* How many times the signal rises through zero. */
static int cycles(const int16_t *s, size_t n) {
    int count = 0;
    for (size_t i = 1; i < n; i++) count += s[i - 1] < 0 && s[i] >= 0;
    return count;
}

static int peak(const int16_t *s, size_t n) {
    int max = 0;
    for (size_t i = 0; i < n; i++) max = abs(s[i]) > max ? abs(s[i]) : max;
    return max;
}

static int status(const gb_t *g) {
    return gb_bus_peek(g, NR52) & 0x0F;
}

/* One sequencer step: the divider's bit 12 falls every 8192 ticks. */
static void seq_steps(gb_t *g, int n) {
    for (int i = 0; i < n * 8192 / 4; i++) gb_tick(g);
}

TEST(gb_apu_register_masks) {
    static const uint8_t mask[0x17] = {0x80, 0x3F, 0x00, 0xFF, 0xBF, 0xFF, 0x3F, 0x00, 0xFF, 0xBF, 0x7F, 0xFF,
                                       0x9F, 0xFF, 0xBF, 0xFF, 0xFF, 0x00, 0x00, 0xBF, 0x00, 0x00, 0x70};
    gb_t *g = quiet();
    gb_bus_write(g, NR50, 0);
    gb_bus_write(g, NR51, 0);
    for (int r = 0; r < 0x16; r++) CHECK_EQ(gb_bus_peek(g, (uint16_t)(0xFF10 + r)), mask[r]);
    CHECK_EQ(gb_bus_peek(g, NR52), 0xF0);
    for (int r = 0xFF27; r < 0xFF30; r++) CHECK_EQ(gb_bus_peek(g, (uint16_t)r), 0xFF);
    gb_bus_write(g, NR10, 0x7F);
    gb_bus_write(g, NR32, 0x60);
    gb_bus_write(g, NR43, 0x5A);
    CHECK_EQ(gb_bus_peek(g, NR10), 0xFF);
    CHECK_EQ(gb_bus_peek(g, NR32), 0xFF);
    CHECK_EQ(gb_bus_peek(g, NR43), 0x5A);
    gb_destroy(g);
}

TEST(gb_apu_boot_state) {
    gb_t *g = gbtu_idle();
    CHECK_EQ(gb_bus_peek(g, NR52), 0xF1);
    CHECK_EQ(gb_bus_peek(g, NR50), 0x77);
    CHECK_EQ(gb_bus_peek(g, NR51), 0xF3);
    CHECK_EQ(gb_bus_peek(g, NR12), 0xF3);
    CHECK_EQ(gb_bus_peek(g, NR11), 0xBF);
    size_t n;
    int16_t *s = record(g, 30, &n, 0); /* the chime has faded: nothing to hear */
    CHECK(peak(s + n / 2, n / 2) < 40);
    free(s);
    gb_destroy(g);
}

TEST(gb_apu_power_off_clears_and_locks) {
    gb_t *g = quiet();
    gb_bus_write(g, NR12, 0xF0);
    gb_bus_write(g, NR14, 0x80);
    CHECK_EQ(status(g), 1);
    for (int i = 0; i < 16; i++) gb_bus_write(g, (uint16_t)(0xFF30 + i), (uint8_t)(i * 17));
    gb_bus_write(g, NR52, 0x00);
    CHECK_EQ(gb_bus_peek(g, NR52), 0x70);
    CHECK_EQ(gb_bus_peek(g, NR12), 0x00);
    CHECK_EQ(gb_bus_peek(g, NR51), 0x00);
    gb_bus_write(g, NR12, 0xF0); /* ignored while off */
    gb_bus_write(g, NR50, 0x77);
    CHECK_EQ(gb_bus_peek(g, NR12), 0x00);
    CHECK_EQ(gb_bus_peek(g, NR50), 0x00);
    CHECK_EQ(gb_bus_peek(g, 0xFF35), 5 * 17); /* wave RAM is untouched and reachable */
    gb_bus_write(g, NR11, 0x3E); /* the original Game Boy still takes length values: 64 - 62 = 2 steps */
    gb_bus_write(g, NR52, 0x80);
    CHECK_EQ(gb_bus_peek(g, NR11), 0x3F); /* (the duty bits were not taken) */
    gb_bus_write(g, NR12, 0xF0);
    gb_bus_write(g, NR14, 0xC0);
    CHECK_EQ(status(g), 1);
    seq_steps(g, 4); /* two length clocks */
    CHECK_EQ(status(g), 0);
    gb_destroy(g);
}

/* A square at register value x plays 131072 / (2048 - x) Hz. */
TEST(gb_apu_square_pitch) {
    for (int ch = 0; ch < 2; ch++) {
        gb_t *g = quiet();
        gb_bus_write(g, ch ? NR21 : NR11, 0x80); /* 50 % duty */
        gb_bus_write(g, ch ? NR22 : NR12, 0xF0);
        gb_bus_write(g, ch ? NR23 : NR13, 1798 & 0xFF); /* 524.3 Hz */
        gb_bus_write(g, ch ? NR24 : NR14, 0x80 | 1798 >> 8);
        CHECK_EQ(status(g), 1 << ch);
        size_t n;
        int16_t *s = record(g, 60, &n, 0);
        int c = cycles(s, n);
        CHECK(c >= 524 && c <= 530); /* 60 frames are 1.0046 s */
        CHECK(peak(s, n) > 3000);
        free(s);
        gb_destroy(g);
    }
}

TEST(gb_apu_samples_per_frame) {
    gb_t *g = quiet();
    size_t n;
    int16_t *s = record(g, 100, &n, 0);
    CHECK(n >= 100u * GB_FRAME_TICKS / 128 - 1 && n <= 100u * GB_FRAME_TICKS / 128 + 1);
    CHECK_EQ(peak(s + n / 2, n / 2), 0); /* nothing playing, every converter off: once the output filter has
                                            let go of what played before, silence is exactly 0 */
    free(s);
    int16_t buf[8];
    for (int f = 0; f < 10; f++) gb_run_frame(g); /* unread samples: only the newest are kept */
    CHECK_EQ(gb_audio_read(g, buf, 4), 4);
    size_t left = 0, got;
    int16_t big[4096 * 2];
    while ((got = gb_audio_read(g, big, 4096)) > 0) left += got;
    CHECK_EQ(left, GB_AUDIO_FRAMES - 4);
    gb_destroy(g);
}

TEST(gb_apu_length_counter) {
    gb_t *g = quiet();
    gb_bus_write(g, NR22, 0xF0);
    gb_bus_write(g, NR21, 0x3D); /* 64 - 61 = 3 steps */
    gb_bus_write(g, NR24, 0xC0);
    seq_steps(g, 4); /* two length clocks */
    CHECK_EQ(status(g), 2);
    seq_steps(g, 2);
    CHECK_EQ(status(g), 0);
    gb_bus_write(g, NR24, 0x80); /* counter off: plays on */
    seq_steps(g, 200);
    CHECK_EQ(status(g), 2);
    gb_destroy(g);
}

/* Switching the counter on in the first half of its period (the next sequencer step is odd) clocks it at once. */
TEST(gb_apu_length_extra_clock) {
    gb_t *g = quiet();
    gb_bus_write(g, NR22, 0xF0);
    gb_bus_write(g, NR21, 0x3E); /* 2 */
    gb_bus_write(g, NR24, 0x80);
    g->apu.seq_step = 1;
    gb_bus_write(g, NR24, 0x40);
    CHECK_EQ(g->apu.ch[1].length, 1);
    CHECK_EQ(status(g), 2);
    gb_bus_write(g, NR24, 0x00);
    gb_bus_write(g, NR24, 0x40); /* down to 0: the channel stops */
    CHECK_EQ(status(g), 0);
    gb_bus_write(g, NR24, 0x00);
    gb_bus_write(g, NR24, 0xC0); /* a trigger with a spent counter reloads it, less the step already gone */
    CHECK_EQ(g->apu.ch[1].length, 63);
    g->apu.seq_step = 2; /* in the second half nothing extra happens */
    gb_bus_write(g, NR24, 0x00);
    gb_bus_write(g, NR24, 0x40);
    CHECK_EQ(g->apu.ch[1].length, 63);
    gb_destroy(g);
}

TEST(gb_apu_envelope) {
    gb_t *g = quiet();
    gb_bus_write(g, NR22, 0xF1); /* from 15 down, one step each 64th of a second */
    gb_bus_write(g, NR24, 0x80);
    CHECK_EQ(g->apu.ch[1].volume, 15);
    g->apu.seq_step = 0;
    g->timer.div = 0;
    seq_steps(g, 8); /* one envelope clock (step 7) */
    CHECK_EQ(g->apu.ch[1].volume, 14);
    seq_steps(g, 8 * 20);
    CHECK_EQ(g->apu.ch[1].volume, 0);
    CHECK_EQ(status(g), 2); /* silent, but still "on" */
    gb_bus_write(g, NR22, 0x0A); /* from 0 up, every 2 */
    gb_bus_write(g, NR24, 0x80);
    seq_steps(g, 8 * 4);
    CHECK_EQ(g->apu.ch[1].volume, 2);
    gb_destroy(g);
}

TEST(gb_apu_sweep) {
    gb_t *g = quiet();
    gb_bus_write(g, NR12, 0xF0);
    gb_bus_write(g, NR10, 0x11); /* every step, up, shift 1 */
    gb_bus_write(g, NR13, 0x00);
    gb_bus_write(g, NR14, 0x82); /* 0x200 */
    g->apu.seq_step = 0;
    g->timer.div = 0;
    seq_steps(g, 3); /* the sweep clocks on step 2: 0x200 + 0x100 */
    CHECK_EQ(g->apu.ch[0].freq, 0x300);
    seq_steps(g, 4); /* step 6 */
    CHECK_EQ(g->apu.ch[0].freq, 0x480);
    CHECK_EQ(status(g), 1);
    seq_steps(g, 4); /* 0x6C0 — and the one after that, worked out at once, would pass the top: it stops */
    CHECK_EQ(g->apu.ch[0].freq, 0x6C0);
    CHECK_EQ(status(g), 0);
    gb_bus_write(g, NR10, 0x01); /* at the trigger too, when there is a shift */
    gb_bus_write(g, NR13, 0xFF);
    gb_bus_write(g, NR14, 0x87);
    CHECK_EQ(status(g), 0);
    gb_bus_write(g, NR10, 0x19); /* downwards it cannot overflow */
    gb_bus_write(g, NR14, 0x87);
    CHECK_EQ(status(g), 1);
    gb_bus_write(g, NR10, 0x11); /* but leaving "down" after it was used stops the channel */
    CHECK_EQ(status(g), 0);
    gb_destroy(g);
}

TEST(gb_apu_wave_channel) {
    gb_t *g = quiet();
    for (int i = 0; i < 16; i++) gb_bus_write(g, (uint16_t)(0xFF30 + i), i < 8 ? 0xFF : 0x00); /* a square */
    CHECK_EQ(gb_bus_peek(g, 0xFF30), 0xFF);
    CHECK_EQ(gb_bus_peek(g, 0xFF3F), 0x00);
    gb_bus_write(g, NR30, 0x80);
    gb_bus_write(g, NR32, 0x20); /* full volume */
    gb_bus_write(g, NR33, 1798 & 0xFF); /* 65536 / (2048 - x) = 262.1 Hz */
    gb_bus_write(g, NR34, 0x80 | 1798 >> 8);
    CHECK_EQ(status(g), 4);
    size_t n;
    int16_t *s = record(g, 60, &n, 0);
    int c = cycles(s, n), loud = peak(s + n / 2, n / 2);
    CHECK(c >= 261 && c <= 266);
    free(s);
    gb_bus_write(g, NR32, 0x60); /* a quarter */
    s = record(g, 30, &n, 0);
    int soft = peak(s + n / 2, n / 2);
    CHECK(soft * 3 < loud && soft > 0);
    free(s);
    gb_bus_write(g, NR32, 0x00); /* mute */
    s = record(g, 30, &n, 0);
    CHECK(peak(s + n / 2, n / 2) < soft / 2);
    free(s);
    gb_bus_write(g, NR30, 0x00); /* converter off: the channel stops */
    CHECK_EQ(status(g), 0);
    gb_destroy(g);
}

TEST(gb_apu_noise) {
    gb_t *g = quiet();
    gb_bus_write(g, NR42, 0xF0);
    gb_bus_write(g, NR43, 0x00);
    gb_bus_write(g, NR44, 0x80);
    CHECK_EQ(status(g), 8);
    CHECK_EQ(g->apu.lfsr, 0x7FFF);
    size_t n;
    int16_t *s = record(g, 20, &n, 0);
    CHECK(peak(s, n) > 1000);
    CHECK(cycles(s, n) > 500); /* a hiss, not a tone */
    free(s);
    /* 7-bit mode repeats every 127 steps, 15-bit mode every 32767. */
    for (int width7 = 0; width7 < 2; width7++) {
        gb_bus_write(g, NR43, width7 ? 0x08 : 0x00);
        gb_bus_write(g, NR44, 0x80);
        uint16_t start = 0;
        int period = 0;
        for (int i = 0; i < 40000; i++) {
            gb_tick(g);
            gb_tick(g); /* 8 ticks: one step */
            gb_apu_sync(g);
            if (i == 200) start = g->apu.lfsr & (width7 ? 0x7F : 0x7FFF);
            else if (i > 200 && !period && (g->apu.lfsr & (width7 ? 0x7F : 0x7FFF)) == start) period = i - 200;
        }
        CHECK_EQ(period, width7 ? 127 : 32767);
    }
    gb_destroy(g);
}

TEST(gb_apu_converter_off_silences) {
    gb_t *g = quiet();
    gb_bus_write(g, NR22, 0xF0);
    gb_bus_write(g, NR24, 0x80);
    CHECK_EQ(status(g), 2);
    gb_bus_write(g, NR22, 0x07); /* volume 0 and "down": the converter is off */
    CHECK_EQ(status(g), 0);
    gb_bus_write(g, NR24, 0x80); /* and a trigger cannot start it */
    CHECK_EQ(status(g), 0);
    gb_bus_write(g, NR22, 0x08); /* volume 0 but "up": on */
    gb_bus_write(g, NR24, 0x80);
    CHECK_EQ(status(g), 2);
    gb_destroy(g);
}

TEST(gb_apu_routing_and_volume) {
    gb_t *g = quiet();
    gb_bus_write(g, NR21, 0x80);
    gb_bus_write(g, NR22, 0xF0);
    gb_bus_write(g, NR23, 0x00);
    gb_bus_write(g, NR24, 0x86);
    gb_bus_write(g, NR51, 0x20); /* channel 2 to the left only */
    size_t n;
    int16_t *l = record(g, 20, &n, 0);
    int left = peak(l + n / 2, n / 2);
    free(l);
    int16_t *r = record(g, 20, &n, 1);
    CHECK(left > 3000);
    CHECK(peak(r + n / 2, n / 2) < 40);
    free(r);
    gb_bus_write(g, NR50, 0x07); /* left volume 1 of 8 */
    l = record(g, 20, &n, 0);
    int soft = peak(l + n / 2, n / 2);
    CHECK(soft * 6 < left && soft > 0);
    free(l);
    gb_destroy(g);
}

/* The sequencer is clocked by the divider: resetting the divider while its bit 12 is 1 is a clock too. */
TEST(gb_apu_sequencer_follows_the_divider) {
    gb_t *g = quiet();
    g->timer.div = 0;
    g->apu.seq_step = 0;
    seq_steps(g, 3);
    CHECK_EQ(g->apu.seq_step, 3);
    for (int i = 0; i < 4096 / 4 + 4; i++) gb_tick(g); /* bit 12 is 1 now */
    gb_bus_write(g, 0xFF04, 0);
    CHECK_EQ(g->apu.seq_step, 4);
    gb_bus_write(g, 0xFF04, 0); /* and 0 now: nothing */
    CHECK_EQ(g->apu.seq_step, 4);
    gb_destroy(g);
}
