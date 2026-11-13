/* Rewind: a snapshot every few frames, each kept as its difference to the one after it.
 *
 * `current` is the newest snapshot in full. The ring holds packed differences, newest last; unpacking
 * the newest into `current` turns it into the snapshot before. Each record is its length, the packed
 * bytes, and its length again, so the newest can be popped from the end and the oldest dropped from
 * the start. All memory is reserved by rewind_ring_new; nothing is allocated while frames run. */
#include "rewind_ring.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "delta_pack.h"

#define LEN_SIZE 4u /* a record's length before and after it */

struct rewind_ring {
    size_t state_size;
    uint8_t *current; /* the newest snapshot (valid when have_current) */
    uint8_t *fresh;   /* a new snapshot, before it becomes current */
    uint8_t *packed;  /* delta_pack_bound(state_size) bytes */
    uint8_t *ring;
    size_t cap;   /* ring bytes */
    size_t head;  /* where the oldest record starts */
    size_t used;  /* bytes of records */
    size_t count; /* records */
    bool have_current;
    int interval; /* frames between snapshots */
    int since;    /* frames run since the last snapshot or step back */
};

static void ring_put(rewind_ring_t *r, size_t at, const uint8_t *src, size_t n) {
    at %= r->cap;
    size_t first = n < r->cap - at ? n : r->cap - at;
    memcpy(r->ring + at, src, first);
    memcpy(r->ring, src + first, n - first);
}

static void ring_get(const rewind_ring_t *r, size_t at, uint8_t *dst, size_t n) {
    at %= r->cap;
    size_t first = n < r->cap - at ? n : r->cap - at;
    memcpy(dst, r->ring + at, first);
    memcpy(dst + first, r->ring, n - first);
}

static uint32_t ring_len(const rewind_ring_t *r, size_t at) {
    uint8_t b[LEN_SIZE];
    ring_get(r, at, b, LEN_SIZE);
    uint32_t v;
    memcpy(&v, b, LEN_SIZE);
    return v;
}

static void drop_oldest(rewind_ring_t *r) {
    size_t rec = LEN_SIZE + ring_len(r, r->head) + LEN_SIZE;
    r->head = (r->head + rec) % r->cap;
    r->used -= rec;
    r->count--;
}

void rewind_ring_clear(rewind_ring_t *r) {
    if (!r) return;
    r->head = r->used = r->count = 0;
    r->have_current = false;
    r->since = 0;
}

void rewind_ring_free(rewind_ring_t *r) {
    if (!r) return;
    free(r->current);
    free(r->fresh);
    free(r->packed);
    free(r->ring);
    free(r);
}

void rewind_ring_stats(const rewind_ring_t *r, size_t *snapshots, size_t *bytes) {
    *snapshots = r ? r->count + (r->have_current ? 1 : 0) : 0;
    *bytes = r ? r->used : 0;
}

rewind_ring_t *rewind_ring_new(size_t state_size, size_t budget_bytes, int interval_frames) {
    rewind_ring_t *r = calloc(1, sizeof *r);
    if (!r) return NULL;
    r->state_size = state_size;
    r->current = malloc(state_size);
    r->fresh = malloc(state_size);
    r->packed = malloc(delta_pack_bound(state_size));
    r->ring = malloc(budget_bytes);
    if (!r->current || !r->fresh || !r->packed || !r->ring) {
        rewind_ring_free(r);
        return NULL;
    }
    r->cap = budget_bytes;
    r->interval = interval_frames;
    return r;
}

uint8_t *rewind_ring_begin(rewind_ring_t *r) {
    if (!r || ++r->since < r->interval) return NULL;
    r->since = 0;
    return r->fresh;
}

void rewind_ring_commit(rewind_ring_t *r) {
    if (r->have_current) {
        size_t n = delta_pack(r->fresh, r->current, r->state_size, r->packed);
        size_t rec = LEN_SIZE + n + LEN_SIZE;
        if (rec > r->cap) {
            rewind_ring_clear(r); /* a difference bigger than the whole budget: start the history again */
        } else {
            while (r->cap - r->used < rec) drop_oldest(r);
            uint32_t len = (uint32_t)n;
            size_t at = r->head + r->used;
            ring_put(r, at, (const uint8_t *)&len, LEN_SIZE);
            ring_put(r, at + LEN_SIZE, r->packed, n);
            ring_put(r, at + LEN_SIZE + n, (const uint8_t *)&len, LEN_SIZE);
            r->used += rec;
            r->count++;
        }
    }
    uint8_t *t = r->current;
    r->current = r->fresh;
    r->fresh = t;
    r->have_current = true;
}

const uint8_t *rewind_ring_back(rewind_ring_t *r) {
    if (!r || !r->have_current) return NULL;
    if (r->since == 0) { /* at the newest snapshot: go to the one before */
        if (r->count == 0) return NULL;
        size_t end = r->head + r->used;
        uint32_t n = ring_len(r, end - LEN_SIZE);
        ring_get(r, end - LEN_SIZE - n, r->packed, n);
        if (!delta_unpack(r->packed, n, r->current, r->state_size)) {
            rewind_ring_clear(r); /* cannot happen with our own records; never load a broken snapshot */
            return NULL;
        }
        r->used -= LEN_SIZE + n + LEN_SIZE;
        r->count--;
    }
    r->since = 0;
    return r->current;
}
