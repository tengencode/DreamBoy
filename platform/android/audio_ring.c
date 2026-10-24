#include "audio_ring.h"

#include <string.h>

#define MASK (AUDIO_RING_FRAMES - 1u)

void audio_ring_init(audio_ring_t *r) {
    memset(r->data, 0, sizeof r->data);
    atomic_init(&r->write, 0u);
    atomic_init(&r->read, 0u);
}

size_t audio_ring_push(audio_ring_t *r, const int16_t *frames, size_t n) {
    unsigned w = atomic_load_explicit(&r->write, memory_order_relaxed);
    unsigned rd = atomic_load_explicit(&r->read, memory_order_acquire);
    size_t room = AUDIO_RING_FRAMES - (size_t)(w - rd);
    if (n > room) n = room;
    for (size_t i = 0; i < n; i++) {
        unsigned slot = (w + (unsigned)i) & MASK;
        r->data[slot * 2] = frames[i * 2];
        r->data[slot * 2 + 1] = frames[i * 2 + 1];
    }
    atomic_store_explicit(&r->write, w + (unsigned)n, memory_order_release);
    return n;
}

size_t audio_ring_pop(audio_ring_t *r, int16_t *out, size_t n) {
    unsigned rd = atomic_load_explicit(&r->read, memory_order_relaxed);
    unsigned w = atomic_load_explicit(&r->write, memory_order_acquire);
    size_t have = (size_t)(w - rd);
    if (n > have) n = have;
    for (size_t i = 0; i < n; i++) {
        unsigned slot = (rd + (unsigned)i) & MASK;
        out[i * 2] = r->data[slot * 2];
        out[i * 2 + 1] = r->data[slot * 2 + 1];
    }
    atomic_store_explicit(&r->read, rd + (unsigned)n, memory_order_release);
    return n;
}

size_t audio_ring_queued(audio_ring_t *r) {
    unsigned w = atomic_load_explicit(&r->write, memory_order_acquire);
    unsigned rd = atomic_load_explicit(&r->read, memory_order_acquire);
    return (size_t)(w - rd);
}

void audio_ring_clear(audio_ring_t *r) {
    atomic_store_explicit(&r->read, atomic_load_explicit(&r->write, memory_order_acquire), memory_order_release);
}
