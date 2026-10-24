#include "delta_pack.h"

#include <string.h>

/* A run of fewer unchanged bytes than this stays inside the changed bytes: a new record would cost more. */
#define MIN_ZERO_RUN 8

/* Every record is at most 2 counts + its changed bytes; a count takes at most as many bytes as it
 * counts (1 byte per 7 bits), so all records together take at most 2n, plus 2 for an empty first
 * run and an empty last run. */
size_t delta_pack_bound(size_t n) {
    return 2 * n + 2;
}

static uint8_t *put_count(uint8_t *p, size_t v) {
    while (v >= 0x80) {
        *p++ = (uint8_t)(v | 0x80);
        v >>= 7;
    }
    *p++ = (uint8_t)v;
    return p;
}

static bool get_count(const uint8_t **p, const uint8_t *end, size_t *v) {
    size_t r = 0;
    for (unsigned shift = 0; *p < end && shift < 8 * sizeof(size_t); shift += 7) {
        uint8_t b = *(*p)++;
        r |= (size_t)(b & 0x7F) << shift;
        if (!(b & 0x80)) {
            *v = r;
            return true;
        }
    }
    return false;
}

/* The unchanged bytes from i on. Word by word while it can: 650 KB go through here 15 times a second. */
static size_t same_from(const uint8_t *a, const uint8_t *b, size_t i, size_t n) {
    size_t j = i;
    while (j + 8 <= n) {
        uint64_t x, y;
        memcpy(&x, a + j, 8);
        memcpy(&y, b + j, 8);
        if (x != y) break;
        j += 8;
    }
    while (j < n && a[j] == b[j]) j++;
    return j - i;
}

size_t delta_pack(const uint8_t *a, const uint8_t *b, size_t n, uint8_t *out) {
    uint8_t *p = out;
    size_t i = 0;
    while (i < n) {
        size_t zeros = same_from(a, b, i, n);
        size_t start = i + zeros;
        size_t end = start; /* the changed bytes run until MIN_ZERO_RUN unchanged ones in a row, or the end */
        while (end < n) {
            if (a[end] != b[end]) {
                end++;
                continue;
            }
            size_t same = same_from(a, b, end, n);
            if (same >= MIN_ZERO_RUN || end + same == n) break;
            end += same;
        }
        p = put_count(p, zeros);
        p = put_count(p, end - start);
        for (size_t k = start; k < end; k++) *p++ = a[k] ^ b[k];
        i = end;
    }
    return (size_t)(p - out);
}

bool delta_unpack(const uint8_t *in, size_t len, uint8_t *buf, size_t n) {
    const uint8_t *p = in, *end = in + len;
    size_t i = 0;
    while (p < end) {
        size_t zeros, lits;
        if (!get_count(&p, end, &zeros) || !get_count(&p, end, &lits)) return false;
        if (zeros > n - i || lits > n - i - zeros || lits > (size_t)(end - p)) return false;
        i += zeros;
        for (size_t k = 0; k < lits; k++) buf[i + k] ^= p[k];
        i += lits;
        p += lits;
    }
    return i == n;
}
