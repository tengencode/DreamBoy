#ifndef GBA_PSG_H
#define GBA_PSG_H

#include <stdbool.h>
#include <stdint.h>

typedef struct gba gba_t;

typedef struct {
    bool on;
    uint8_t volume;      /* envelope volume 0..15 */
    uint8_t env_timer;   /* sequencer envelope clocks left until the next volume step */
    uint16_t length;     /* length clocks left; 0 = expired */
    uint32_t timer;      /* cycles until the next waveform step (1..period) */
    uint8_t pos;         /* duty step 0..7 or wave sample 0..63 */
    uint16_t freq;       /* 11-bit frequency (square 1: updated by the sweep) */
    uint8_t sweep_timer; /* sequencer sweep clocks left */
    bool sweep_on;
    uint16_t lfsr;       /* noise shift register */
} psg_channel_t;

typedef struct {
    psg_channel_t ch[4]; /* square 1, square 2, wave, noise */
    uint8_t wave[2][16]; /* wave RAM banks: two 4-bit samples per byte, high nibble first */
    uint8_t step;        /* next frame-sequencer step 0..7 */
    uint64_t last;       /* cycle the channels were last advanced to */
} psg_t;

void psg_reset(psg_t *p, uint64_t now); /* channels off, sequencer at step 0; wave RAM kept */
void psg_advance(gba_t *g, uint64_t now); /* run the channel timers up to cycle now */
void psg_frame_step(gba_t *g);            /* one 512 Hz sequencer step */
/* A PSG register (0x060-0x07F) was written; io.raw already holds the value. */
void psg_write(gba_t *g, uint32_t off, uint16_t value);
uint8_t psg_output(const gba_t *g, int ch); /* current channel level 0..15 */
uint8_t psg_status(const gba_t *g);        /* SOUNDCNT_X bits 0-3: channels playing */
uint16_t psg_wave_read16(const gba_t *g, uint32_t off); /* 0x090-0x09F: the bank that is not playing */
void psg_wave_write16(gba_t *g, uint32_t off, uint16_t value);

#endif
