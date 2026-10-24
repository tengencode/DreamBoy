#include <string.h>

#include "bios/hle.h"
#include "test.h"
#include "testutil.h"

static gba_t *sound_on(void) {
    gba_t *g = gba_create(NULL);
    tu_io16(g, 0x084, 0x0080); /* SOUNDCNT_X: master enable */
    return g;
}

TEST(apu_soundbias_resets_to_0x200) {
    gba_t *g = gba_create(NULL);
    CHECK_EQ(bus_read16(g, 0x04000088, false), 0x0200);
    gba_destroy(g);
}

TEST(apu_one_sample_every_512_cycles) {
    gba_t *g = gba_create(NULL);
    int16_t buf[200 * 2];
    tu_advance_to(g, 512 * 10);
    CHECK_EQ(gba_audio_read(g, buf, 200), 10);
    CHECK_EQ(buf[0], 0); /* sound off: silence */
    CHECK_EQ(buf[19], 0);
    CHECK_EQ(gba_audio_read(g, buf, 200), 0);
    gba_destroy(g);
}

TEST(apu_ring_buffer_drops_when_nobody_reads) {
    gba_t *g = gba_create(NULL);
    static int16_t buf[4096 * 2];
    tu_advance_to(g, 512 * 3000);
    CHECK_EQ(gba_audio_read(g, buf, 4096), APU_RING_FRAMES);
    gba_destroy(g);
}

TEST(apu_master_off_clears_and_locks_psg_registers) {
    gba_t *g = sound_on();
    tu_io16(g, 0x080, 0xFF77);
    CHECK_EQ(bus_read16(g, 0x04000080, false), 0xFF77);
    tu_io16(g, 0x084, 0x0000);
    CHECK_EQ(bus_read16(g, 0x04000080, false), 0x0000);
    tu_io16(g, 0x080, 0x1234); /* ignored while sound is off */
    CHECK_EQ(bus_read16(g, 0x04000080, false), 0x0000);
    tu_io16(g, 0x082, 0x0304); /* SOUNDCNT_H stays writable */
    CHECK_EQ(bus_read16(g, 0x04000082, false), 0x0304);
    tu_io16(g, 0x084, 0x0080);
    tu_io16(g, 0x080, 0x1234);
    CHECK_EQ(bus_read16(g, 0x04000080, false), 0x1234);
    gba_destroy(g);
}

TEST(apu_mixer_psg_levels) {
    const uint8_t loud[4] = {15, 15, 15, 15};
    const int8_t none[2] = {0, 0};
    /* all four channels, volume 7, PSG 100%: (45 << 3) + (15 << 3) = 480; x8 = 3840; >> 2 = 960 → clamps at 0x3FF */
    CHECK_EQ(apu_mix_side(0xFF77, 0x0002, 0x200, loud, none, true), 32704);
    CHECK_EQ(apu_mix_side(0xFF77, 0x0003, 0x200, loud, none, false), 32704); /* ratio 3 acts as 100% */
    /* channel 1 on the left only, volume 0, PSG 25%: 120 >> 4 = 7 */
    CHECK_EQ(apu_mix_side(0x1000, 0x0000, 0x200, loud, none, true), 7 * 64);
    CHECK_EQ(apu_mix_side(0x1000, 0x0000, 0x200, loud, none, false), 0);
}

TEST(apu_mixer_dma_levels) {
    const uint8_t quiet[4] = {0, 0, 0, 0};
    const int8_t high[2] = {127, 64};
    const int8_t low[2] = {-128, 0};
    CHECK_EQ(apu_mix_side(0, 0x0304, 0x200, quiet, high, true), 508 * 64);  /* A 100% */
    CHECK_EQ(apu_mix_side(0, 0x0300, 0x200, quiet, high, true), 254 * 64);  /* A 50% */
    CHECK_EQ(apu_mix_side(0, 0x0304, 0x200, quiet, low, false), -32768);    /* -128 x 4 = -512 */
    CHECK_EQ(apu_mix_side(0, 0x1008, 0x200, quiet, high, false), 256 * 64); /* B 100%, right only */
    CHECK_EQ(apu_mix_side(0, 0x1008, 0x200, quiet, high, true), 0);
}

TEST(apu_mixer_bias_and_clamps) {
    const uint8_t loud[4] = {15, 15, 15, 15};
    const uint8_t quiet[4] = {0, 0, 0, 0};
    const int8_t low[2] = {-128, 0};
    const int8_t none[2] = {0, 0};
    CHECK_EQ(apu_mix_side(0, 0x0304, 0x000, quiet, low, true), 0);            /* clamped at 0, minus bias 0 */
    CHECK_EQ(apu_mix_side(0xFF77, 0x0002, 0x000, loud, none, true), 32767);   /* 960 x 64 is past int16 */
}

TEST(apu_fifo_order_and_timer_select) {
    gba_t *g = sound_on();
    tu_io16(g, 0x082, 0x4000); /* A on timer 0, B on timer 1 */
    tu_io32(g, 0x0A0, 0x04030201);
    tu_io32(g, 0x0A4, 0x08070605);
    CHECK_EQ(g->apu.fifo[0].count, 4);
    apu_on_timer_overflow(g, 0);
    CHECK_EQ(g->apu.fifo[0].sample, 1);
    CHECK_EQ(g->apu.fifo[1].sample, 0);
    apu_on_timer_overflow(g, 1);
    CHECK_EQ(g->apu.fifo[1].sample, 5);
    apu_on_timer_overflow(g, 0);
    CHECK_EQ(g->apu.fifo[0].sample, 2);
    gba_destroy(g);
}

TEST(apu_fifo_drops_when_full_and_holds_when_empty) {
    gba_t *g = sound_on();
    for (uint32_t i = 0; i < 9; i++) {
        uint32_t b = i * 4;
        tu_io32(g, 0x0A0, b | (b + 1) << 8 | (b + 2) << 16 | (b + 3) << 24);
    }
    CHECK_EQ(g->apu.fifo[0].count, 32); /* bytes 32-35 were dropped */
    for (int i = 0; i < 32; i++) apu_on_timer_overflow(g, 0);
    CHECK_EQ(g->apu.fifo[0].sample, 31);
    apu_on_timer_overflow(g, 0);
    CHECK_EQ(g->apu.fifo[0].sample, 31); /* empty: the last sample is held */
    gba_destroy(g);
}

TEST(apu_fifo_reset_bits_and_byte_writes) {
    gba_t *g = sound_on();
    bus_write8(g, 0x040000A0, 0x11, false);
    bus_write8(g, 0x040000A1, 0x22, false);
    CHECK_EQ(g->apu.fifo[0].count, 2);
    apu_on_timer_overflow(g, 0);
    CHECK_EQ(g->apu.fifo[0].sample, 0x11);
    tu_io16(g, 0x082, 0x0800); /* reset FIFO A */
    CHECK_EQ(g->apu.fifo[0].count, 0);
    CHECK_EQ(bus_read16(g, 0x04000082, false) & 0x0800, 0);
    gba_destroy(g);
}

TEST(apu_timer_overflow_pops_the_fifo) {
    gba_t *g = sound_on();
    tu_io16(g, 0x082, 0x0304); /* A 100%, both sides, timer 0 */
    tu_io32(g, 0x0A0, 0x7F7F7F7F);
    tu_io16(g, 0x100, 0xFFF0);
    tu_io16(g, 0x102, 0x0080);
    tu_advance_to(g, g->timers.unit[0].start + 16);
    CHECK_EQ(g->apu.fifo[0].sample, 127);
    CHECK_EQ(g->apu.fifo[0].count, 3);
    gba_destroy(g);
}

TEST(apu_dma_sample_reaches_the_output) {
    gba_t *g = sound_on();
    int16_t out[2];
    tu_io16(g, 0x082, 0x0304);
    tu_io32(g, 0x0A0, 0x40404040);
    apu_on_timer_overflow(g, 0); /* sample 64 */
    tu_advance_to(g, 512);
    CHECK_EQ(gba_audio_read(g, out, 1), 1);
    CHECK_EQ(out[0], 16384); /* 64 x 4 x 64 */
    CHECK_EQ(out[1], 16384);
    gba_destroy(g);
}

TEST(apu_wave_ram_cpu_sees_the_other_bank) {
    gba_t *g = sound_on();
    tu_io16(g, 0x070, 0x0000); /* bank 0 playing */
    tu_io16(g, 0x090, 0xBEEF);
    CHECK_EQ(g->apu.psg.wave[1][0], 0xEF);
    CHECK_EQ(g->apu.psg.wave[1][1], 0xBE);
    CHECK_EQ(g->apu.psg.wave[0][0], 0x00);
    CHECK_EQ(bus_read16(g, 0x04000090, false), 0xBEEF);
    tu_io16(g, 0x070, 0x0040); /* bank 1 playing: the CPU now sees bank 0 */
    CHECK_EQ(bus_read16(g, 0x04000090, false), 0x0000);
    gba_destroy(g);
}

TEST(apu_frame_sequencer_steps_at_512_hz) {
    gba_t *g = sound_on();
    CHECK_EQ(g->apu.psg.step, 0);
    tu_advance_to(g, 32768);
    CHECK_EQ(g->apu.psg.step, 1);
    tu_advance_to(g, 9 * 32768);
    CHECK_EQ(g->apu.psg.step, 1);
    gba_destroy(g);
}

TEST(apu_bios_register_ram_reset_clears_sound) {
    gba_t *g = sound_on();
    tu_io32(g, 0x0A0, 0x01020304);
    tu_io16(g, 0x088, 0x0100);
    g->cpu.r[0] = 0x40; /* sound registers */
    hle_swi(g, 0x01);
    CHECK_EQ(g->apu.fifo[0].count, 0);
    CHECK_EQ(bus_read16(g, 0x04000088, false), 0x0200);
    CHECK_EQ(bus_read16(g, 0x04000084, false), 0x0000);
    gba_destroy(g);
}
