#ifndef GBA_APU_H
#define GBA_APU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "apu/psg.h"

typedef struct gba gba_t;

#define APU_SAMPLE_CYCLES 512u      /* 16,777,216 / 512 = 32,768 samples per second */
#define APU_FRAME_SEQ_CYCLES 32768u /* 512 Hz frame sequencer */
#define APU_RING_FRAMES 2048u       /* stereo frames kept for gba_audio_read (about 3.7 video frames) */

typedef struct {
    int8_t data[32];
    uint8_t read;  /* index of the oldest byte */
    uint8_t count; /* bytes queued, 0..32 */
    int8_t sample; /* current output, held until the next pop */
} apu_fifo_t;

typedef struct {
    psg_t psg;
    apu_fifo_t fifo[2];                /* 0 = FIFO A, 1 = FIFO B */
    int16_t ring[APU_RING_FRAMES * 2]; /* interleaved left, right */
    uint32_t ring_read, ring_count;    /* in stereo frames */
    uint32_t silence_cycles;            /* cycles of silence owed while the console is stopped */
} apu_t;

/* Clears all sound state, sets SOUNDBIAS to 0x200 and schedules the sample and sequencer events. */
void apu_reset(gba_t *g);
void apu_on_sample(gba_t *g, uint64_t when);
void apu_on_frame_seq(gba_t *g, uint64_t when);
void apu_on_timer_overflow(gba_t *g, int timer); /* timer 0 or 1 overflowed */
uint16_t apu_read16(gba_t *g, uint32_t off);      /* I/O offsets 0x060-0x0A7 */
void apu_write16(gba_t *g, uint32_t off, uint16_t value);
void apu_write_fifo8(gba_t *g, int fifo, uint8_t value);
size_t apu_read(gba_t *g, int16_t *out, size_t max_frames);
/* One video frame's worth of silent samples (the console is stopped; frontends paced by sound keep going). */
void apu_stopped_frame(gba_t *g);
/* One output value for one side: the hardware mixer's arithmetic (exposed for tests). */
int16_t apu_mix_side(uint16_t soundcnt_l, uint16_t soundcnt_h, uint16_t bias, const uint8_t psg[4],
                     const int8_t fifo[2], bool left);

#endif
