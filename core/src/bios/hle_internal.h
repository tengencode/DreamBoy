#ifndef GBA_HLE_INTERNAL_H
#define GBA_HLE_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

typedef struct gba gba_t;

/* hle_math.c — register-based SWIs follow GBATEK's argument conventions. */
void hle_div(gba_t *g, int32_t num, int32_t den); /* r0 = quotient, r1 = remainder, r3 = |quotient| */
uint32_t hle_sqrt(uint32_t value);
/* tan in 1.14 fixed point (full 32-bit input, 32-bit wrap-around like the BIOS); result -0x4000..0x4000
 * as a sign-extended 16-bit value. r1/r3 (may be NULL) receive what the BIOS leaves in those registers. */
int32_t hle_arctan(int32_t tan, int32_t *r1, int32_t *r3);
/* Cycles the BIOS routines take (mGBA's measured formulas; the HLE call path adds the rest). */
uint32_t hle_div_cycles(int32_t num, int32_t den);
uint32_t hle_sqrt_cycles(uint32_t value);
uint32_t hle_arctan_cycles(int32_t tan);
uint32_t hle_arctan2_cycles(int32_t x, int32_t y);
/* Result 0..0xFFFF for a full circle; *r1 receives the inner ArcTan's r1 (untouched when x or y is 0). */
uint32_t hle_arctan2(int32_t x, int32_t y, int32_t *r1);
void hle_bg_affine_set(gba_t *g);
void hle_obj_affine_set(gba_t *g);

/* hle_memory.c */
void hle_cpu_set(gba_t *g);
void hle_cpu_fast_set(gba_t *g);
void hle_bit_unpack(gba_t *g);

/* hle_decompress.c — the vram variants write 16-bit units because VRAM ignores byte writes. */
void hle_lz77(gba_t *g, bool vram);
void hle_huffman(gba_t *g);
void hle_rl(gba_t *g, bool vram);
void hle_diff8(gba_t *g, bool vram);
void hle_diff16(gba_t *g);

#endif
