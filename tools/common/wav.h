#ifndef GBA_TOOLS_WAV_H
#define GBA_TOOLS_WAV_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    FILE *f;
    uint32_t rate, frames;
} wav_writer_t;

/* 16-bit stereo PCM. Each returns 0 on success. */
int wav_open(wav_writer_t *w, const char *path, uint32_t rate);
void wav_write(wav_writer_t *w, const int16_t *stereo, size_t frames); /* interleaved left, right */
int wav_close(wav_writer_t *w); /* writes the final sizes into the header */

#endif
