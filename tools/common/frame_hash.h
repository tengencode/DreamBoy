#ifndef GBA_TOOLS_FRAME_HASH_H
#define GBA_TOOLS_FRAME_HASH_H

#include <stdint.h>
#include <stddef.h>

/* 64-bit FNV-1a over the 240x160 BGR555 framebuffer, low byte of each pixel first. */
static inline uint64_t frame_hash(const uint16_t *pixels) {
    uint64_t h = 0xCBF29CE484222325ull;
    for (int i = 0; i < 240 * 160; i++) {
        h = (h ^ (pixels[i] & 0xFFu)) * 0x100000001B3ull;
        h = (h ^ (uint32_t)(pixels[i] >> 8)) * 0x100000001B3ull;
    }
    return h;
}

#define AUDIO_HASH_INIT 0xCBF29CE484222325ull

/* Continue a 64-bit FNV-1a over int16 samples, low byte of each first. */
static inline uint64_t audio_hash_update(uint64_t h, const int16_t *samples, size_t count) {
    for (size_t i = 0; i < count; i++) {
        uint16_t v = (uint16_t)samples[i];
        h = (h ^ (v & 0xFFu)) * 0x100000001B3ull;
        h = (h ^ (uint32_t)(v >> 8)) * 0x100000001B3ull;
    }
    return h;
}

#endif
