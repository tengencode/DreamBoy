#include "test.h"
#include "testutil.h"

static gba_t *sound_on(void) {
    gba_t *g = gba_create(NULL);
    tu_io16(g, 0x084, 0x0080);
    return g;
}

/* Channel ch's level at cycles t0, t0 + period, t0 + 2 * period, ... */
static void levels(gba_t *g, int ch, uint64_t t0, uint32_t period, int count, uint8_t *out) {
    for (int i = 0; i < count; i++) {
        psg_advance(g, t0 + (uint64_t)i * period);
        out[i] = psg_output(g, ch);
    }
}

static uint16_t status(gba_t *g) {
    return bus_read16(g, 0x04000084, false) & 0x0F;
}

TEST(psg_square_duty_patterns) {
    static const uint8_t expect[4][8] = {{0, 0, 0, 0, 0, 0, 0, 15},
                                         {15, 0, 0, 0, 0, 0, 0, 15},
                                         {15, 0, 0, 0, 0, 15, 15, 15},
                                         {0, 15, 15, 15, 15, 15, 15, 0}};
    for (int d = 0; d < 4; d++) {
        gba_t *g = sound_on();
        tu_io16(g, 0x068, (uint16_t)(0xF000 | d << 6)); /* volume 15, duty d */
        tu_io16(g, 0x06C, 0x8000 | 2047);               /* trigger: 16 cycles per step */
        uint8_t got[8];
        levels(g, 1, g->now, 16, 8, got);
        for (int i = 0; i < 8; i++) CHECK_EQ(got[i], expect[d][i]);
        gba_destroy(g);
    }
}

TEST(psg_square_step_length) {
    gba_t *g = sound_on();
    tu_io16(g, 0x068, 0xF080);        /* 50% duty */
    tu_io16(g, 0x06C, 0x8000 | 1750); /* 16 x 298 = 4768 cycles per step */
    uint64_t t0 = g->now;
    psg_advance(g, t0 + 4767);
    CHECK_EQ(psg_output(g, 1), 15);
    psg_advance(g, t0 + 4768);
    CHECK_EQ(psg_output(g, 1), 0);
    gba_destroy(g);
}

TEST(psg_register_writes_catch_the_channels_up_first) {
    gba_t *g = sound_on();
    tu_io16(g, 0x068, 0xF080);
    tu_io16(g, 0x06C, 0x8000 | 2047);
    g->now += 20;                /* one step (16 cycles) passes without a sample event */
    tu_io16(g, 0x06C, 1000);     /* new frequency, no trigger */
    CHECK_EQ(g->apu.psg.ch[1].pos, 1);
    gba_destroy(g);
}

TEST(psg_length_counter_stops_the_channel) {
    gba_t *g = sound_on();
    tu_io16(g, 0x068, 0xF03F);        /* length 63: one length clock left */
    tu_io16(g, 0x06C, 0xC000 | 1000); /* trigger + length enable */
    CHECK_EQ(status(g), 0x02);
    tu_advance_to(g, 32767);
    CHECK_EQ(status(g), 0x02);
    tu_advance_to(g, 32768); /* step 0 clocks the lengths */
    CHECK_EQ(status(g), 0x00);
    gba_destroy(g);
}

TEST(psg_length_ignored_unless_enabled) {
    gba_t *g = sound_on();
    tu_io16(g, 0x068, 0xF03F);
    tu_io16(g, 0x06C, 0x8000 | 1000); /* no length enable */
    tu_advance_to(g, 4 * 32768);
    CHECK_EQ(status(g), 0x02);
    gba_destroy(g);
}

TEST(psg_envelope_steps_on_sequencer_step_7) {
    gba_t *g = sound_on();
    tu_io16(g, 0x062, 0x7100); /* volume 7, down, every step */
    tu_io16(g, 0x064, 0x8000 | 1000);
    tu_io16(g, 0x068, 0xE900); /* volume 14, up, every step */
    tu_io16(g, 0x06C, 0x8000 | 1000);
    tu_io16(g, 0x078, 0x5000); /* volume 5, frozen */
    tu_io16(g, 0x07C, 0x8000);
    tu_advance_to(g, 8 * 32768 - 1);
    CHECK_EQ(g->apu.psg.ch[0].volume, 7);
    tu_advance_to(g, 8 * 32768); /* the 8th sequencer event is step 7 */
    CHECK_EQ(g->apu.psg.ch[0].volume, 6);
    CHECK_EQ(g->apu.psg.ch[1].volume, 15);
    CHECK_EQ(g->apu.psg.ch[3].volume, 5);
    tu_advance_to(g, 16 * 32768);
    CHECK_EQ(g->apu.psg.ch[0].volume, 5);
    CHECK_EQ(g->apu.psg.ch[1].volume, 15); /* stays at the top */
    gba_destroy(g);
}

TEST(psg_sweep_raises_frequency_until_overflow) {
    gba_t *g = sound_on();
    tu_io16(g, 0x060, 0x0012); /* sweep time 1, up, shift 2 */
    tu_io16(g, 0x062, 0xF080);
    tu_io16(g, 0x064, 0x8000 | 1000);
    tu_advance_to(g, 3 * 32768); /* step 2 */
    CHECK_EQ(g->apu.psg.ch[0].freq, 1250);
    CHECK_EQ(tu_raw16(g, 0x064) & 0x7FF, 1250); /* the frequency is write-only; the store follows the sweep */
    tu_advance_to(g, 7 * 32768); /* step 6 */
    CHECK_EQ(g->apu.psg.ch[0].freq, 1562);
    CHECK_EQ(status(g) & 1, 1);
    tu_advance_to(g, 11 * 32768); /* 1562 → 1952; the next value (2440) would pass 2047: off */
    CHECK_EQ(g->apu.psg.ch[0].freq, 1952);
    CHECK_EQ(status(g) & 1, 0);
    gba_destroy(g);
}

TEST(psg_sweep_down_and_overflow_at_trigger) {
    gba_t *g = sound_on();
    tu_io16(g, 0x060, 0x0019); /* time 1, down, shift 1 */
    tu_io16(g, 0x062, 0xF080);
    tu_io16(g, 0x064, 0x8000 | 1000);
    tu_advance_to(g, 3 * 32768);
    CHECK_EQ(g->apu.psg.ch[0].freq, 500);
    tu_io16(g, 0x060, 0x0011);        /* up, shift 1 */
    tu_io16(g, 0x064, 0x8000 | 2000); /* 2000 + 1000 > 2047: off at once */
    CHECK_EQ(status(g) & 1, 0);
    gba_destroy(g);
}

TEST(psg_dac_off_keeps_the_channel_silent) {
    gba_t *g = sound_on();
    tu_io16(g, 0x062, 0x0080); /* volume 0, fading down: DAC off */
    tu_io16(g, 0x064, 0x8000 | 1000);
    CHECK_EQ(status(g) & 1, 0);
    tu_io16(g, 0x062, 0xF080);
    tu_io16(g, 0x064, 0x8000 | 1000);
    CHECK_EQ(status(g) & 1, 1);
    tu_io16(g, 0x062, 0x0000); /* turning the DAC off stops it */
    CHECK_EQ(status(g) & 1, 0);
    gba_destroy(g);
}

/* Bank 0 = samples 0, 1, ..., 15, 0, 1, ..., 15; bank 1 = fill. Leaves bank 0 selected for playback. */
static void load_wave(gba_t *g, uint16_t fill) {
    static const uint16_t ramp[4] = {0x2301, 0x6745, 0xAB89, 0xEFCD};
    tu_io16(g, 0x070, 0x0040); /* bank 1 playing: the CPU sees bank 0 */
    for (uint32_t i = 0; i < 8; i++) tu_io16(g, 0x090 + i * 2, ramp[i & 3]);
    tu_io16(g, 0x070, 0x0000); /* bank 0 playing: the CPU sees bank 1 */
    for (uint32_t i = 0; i < 8; i++) tu_io16(g, 0x090 + i * 2, fill);
}

TEST(psg_wave_plays_bank_samples) {
    gba_t *g = sound_on();
    load_wave(g, 0xFFFF);
    tu_io16(g, 0x070, 0x0080);        /* bank 0, DAC on */
    tu_io16(g, 0x072, 0x2000);        /* 100% */
    tu_io16(g, 0x074, 0x8000 | 2047); /* 8 cycles per sample */
    uint8_t got[34];
    levels(g, 2, g->now, 8, 34, got);
    for (int i = 0; i < 34; i++) CHECK_EQ(got[i], (i % 32) & 15);
    gba_destroy(g);
}

TEST(psg_wave_64_sample_mode_plays_both_banks) {
    gba_t *g = sound_on();
    load_wave(g, 0xFFFF);
    tu_io16(g, 0x070, 0x00A0); /* 64 samples, starting at bank 0, DAC on */
    tu_io16(g, 0x072, 0x2000);
    tu_io16(g, 0x074, 0x8000 | 2047);
    uint8_t got[65];
    levels(g, 2, g->now, 8, 65, got);
    CHECK_EQ(got[16], 0);
    CHECK_EQ(got[32], 15); /* bank 1 */
    CHECK_EQ(got[63], 15);
    CHECK_EQ(got[64], 0); /* back to bank 0 */
    gba_destroy(g);
}

TEST(psg_wave_volume_codes) {
    static const struct {
        uint16_t reg;
        uint8_t level;
    } cases[] = {{0x0000, 0}, {0x2000, 13}, {0x4000, 6}, {0x6000, 3}, {0x8000, 9}};
    for (size_t k = 0; k < TU_LEN(cases); k++) {
        gba_t *g = sound_on();
        load_wave(g, 0xFFFF);
        tu_io16(g, 0x070, 0x0080);
        tu_io16(g, 0x072, cases[k].reg);
        tu_io16(g, 0x074, 0x8000 | 2047);
        psg_advance(g, g->now + 13 * 8); /* sample 13 */
        CHECK_EQ(psg_output(g, 2), cases[k].level);
        gba_destroy(g);
    }
}

TEST(psg_noise_lfsr_15_bit) {
    gba_t *g = sound_on();
    tu_io16(g, 0x078, 0xF000); /* volume 15 */
    tu_io16(g, 0x07C, 0x8000); /* r 0, s 0: a step every 32 cycles */
    uint64_t t0 = g->now;
    CHECK_EQ(g->apu.psg.ch[3].lfsr, 0x7FFF);
    CHECK_EQ(psg_output(g, 3), 0); /* bit 0 set: low */
    psg_advance(g, t0 + 14 * 32);
    CHECK_EQ(g->apu.psg.ch[3].lfsr, 0x0001);
    psg_advance(g, t0 + 15 * 32);
    CHECK_EQ(g->apu.psg.ch[3].lfsr, 0x4000);
    CHECK_EQ(psg_output(g, 3), 15);
    gba_destroy(g);
}

TEST(psg_noise_7_bit_and_period) {
    gba_t *g = sound_on();
    tu_io16(g, 0x078, 0xF000);
    tu_io16(g, 0x07C, 0x8000 | 0x08 | 0x20 | 1); /* 7-bit, s 2, r 1: 64 x 1 << 2 = 256 cycles */
    uint64_t t0 = g->now;
    psg_advance(g, t0 + 255);
    CHECK_EQ(g->apu.psg.ch[3].lfsr, 0x7FFF);
    psg_advance(g, t0 + 256);
    CHECK_EQ(g->apu.psg.ch[3].lfsr, 0x3FBF);
    gba_destroy(g);
}

TEST(psg_levels_reach_the_mixed_output) {
    gba_t *g = sound_on();
    int16_t buf[4096 * 2];
    tu_io16(g, 0x070, 0x0040); /* fill bank 0 with 15s */
    for (uint32_t i = 0; i < 8; i++) tu_io16(g, 0x090 + i * 2, 0xFFFF);
    tu_io16(g, 0x070, 0x0080);
    tu_io16(g, 0x072, 0x2000);
    tu_io16(g, 0x080, 0x4477); /* wave channel on both sides, volume 7 */
    tu_io16(g, 0x082, 0x0002); /* PSG 100% */
    tu_io16(g, 0x074, 0x8000 | 2047);
    gba_audio_read(g, buf, 4096);
    tu_advance_to(g, (g->now / 512 + 1) * 512);
    CHECK_EQ(gba_audio_read(g, buf, 4096), 1);
    CHECK_EQ(buf[0], 15360); /* 15 << 3 = 120; x8 = 960; >> 2 = 240; x64 */
    CHECK_EQ(buf[1], 15360);
    gba_destroy(g);
}
