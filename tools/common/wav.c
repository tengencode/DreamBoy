#include "wav.h"

#include <string.h>

static void put16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v) {
    put16(p, (uint16_t)v);
    put16(p + 2, (uint16_t)(v >> 16));
}

static void header(uint8_t h[44], uint32_t rate, uint32_t frames) {
    uint32_t data = frames * 4;
    memcpy(h, "RIFF", 4);
    put32(h + 4, 36 + data);
    memcpy(h + 8, "WAVEfmt ", 8);
    put32(h + 16, 16);       /* fmt chunk size */
    put16(h + 20, 1);        /* PCM */
    put16(h + 22, 2);        /* stereo */
    put32(h + 24, rate);
    put32(h + 28, rate * 4); /* bytes per second */
    put16(h + 32, 4);        /* bytes per frame */
    put16(h + 34, 16);       /* bits per sample */
    memcpy(h + 36, "data", 4);
    put32(h + 40, data);
}

int wav_open(wav_writer_t *w, const char *path, uint32_t rate) {
    uint8_t h[44];
    w->rate = rate;
    w->frames = 0;
    w->f = fopen(path, "wb");
    if (!w->f) return -1;
    header(h, rate, 0);
    return fwrite(h, 1, sizeof h, w->f) == sizeof h ? 0 : -1;
}

void wav_write(wav_writer_t *w, const int16_t *stereo, size_t frames) {
    uint8_t buf[4096];
    size_t done = 0;
    while (done < frames) {
        size_t n = frames - done > 1024 ? 1024 : frames - done;
        for (size_t i = 0; i < n * 2; i++) put16(buf + i * 2, (uint16_t)stereo[done * 2 + i]);
        fwrite(buf, 1, n * 4, w->f);
        done += n;
    }
    w->frames += (uint32_t)frames;
}

int wav_close(wav_writer_t *w) {
    uint8_t h[44];
    int ok = w->f != NULL;
    if (ok) {
        header(h, w->rate, w->frames);
        ok = fseek(w->f, 0, SEEK_SET) == 0 && fwrite(h, 1, sizeof h, w->f) == sizeof h;
        if (fclose(w->f) != 0) ok = 0;
    }
    w->f = NULL;
    return ok ? 0 : -1;
}
