#ifndef GBA_DELTA_PACK_H
#define GBA_DELTA_PACK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The difference of two equal-sized snapshots, packed: the XOR of the two as records of
 * (unchanged-byte count, changed-byte count, the changed bytes XORed), counts as variable-length
 * integers. Snapshots a moment apart are mostly unchanged, so the records are few and short. */

/* The most bytes delta_pack can write for n-byte snapshots. */
size_t delta_pack_bound(size_t n);
/* Packs a XOR b (n bytes each) into out (delta_pack_bound(n) bytes); returns the bytes written. */
size_t delta_pack(const uint8_t *a, const uint8_t *b, size_t n, uint8_t *out);
/* XORs a packed difference into buf (n bytes): buf = a turns into b and back. False, and buf
 * possibly half-changed, if the packed bytes do not describe exactly n bytes. */
bool delta_unpack(const uint8_t *in, size_t len, uint8_t *buf, size_t n);

#endif
