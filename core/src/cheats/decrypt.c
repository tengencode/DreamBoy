/* The ciphers of the cheat devices: TEA with each device's key, and CodeBreaker's bit-shuffle scrambling. */
#include "cheats/cheats.h"

const uint32_t CHEAT_GS_SEEDS[4] = {0x09F4FBBD, 0x9681884A, 0x352027E9, 0xF3DEE5A7};
const uint32_t CHEAT_AR3_SEEDS[4] = {0x7AA9648F, 0x7FAE6994, 0xC0EFAAD5, 0x42712C57};

#define TEA_DELTA 0x9E3779B9u
#define TEA_ROUNDS 32

void cheat_tea_decrypt(uint32_t *op1, uint32_t *op2, const uint32_t k[4]) {
    uint32_t a = *op1, b = *op2, sum = TEA_DELTA * TEA_ROUNDS;
    for (int i = 0; i < TEA_ROUNDS; i++) {
        b -= ((a << 4) + k[2]) ^ (a + sum) ^ ((a >> 5) + k[3]);
        a -= ((b << 4) + k[0]) ^ (b + sum) ^ ((b >> 5) + k[1]);
        sum -= TEA_DELTA;
    }
    *op1 = a;
    *op2 = b;
}

void cheat_tea_encrypt(uint32_t *op1, uint32_t *op2, const uint32_t k[4]) {
    uint32_t a = *op1, b = *op2, sum = 0;
    for (int i = 0; i < TEA_ROUNDS; i++) {
        sum += TEA_DELTA;
        a += ((b << 4) + k[0]) ^ (b + sum) ^ ((b >> 5) + k[1]);
        b += ((a << 4) + k[2]) ^ (a + sum) ^ ((a >> 5) + k[3]);
    }
    *op1 = a;
    *op2 = b;
}

/* ---- CodeBreaker ---- */

static uint32_t ror32(uint32_t v, unsigned n) {
    return (v >> n) | (v << (32 - n));
}

/* Three steps of the device's LCG, their bits mixed into one number. */
static uint32_t cb_rand(cb_crypt_t *c) {
    uint32_t r1 = c->rng * 0x41C64E6Du + 0x3039;
    uint32_t r2 = r1 * 0x41C64E6Du + 0x3039;
    uint32_t r3 = r2 * 0x41C64E6Du + 0x3039;
    c->rng = r3;
    return ((r1 << 14) & 0xC0000000u) | ((r2 >> 1) & 0x3FFF8000u) | ((r3 >> 16) & 0x7FFFu);
}

/* A table index from the next random number: the device's own shift-and-subtract remainder by 48, quirks
 * included (it decides which bits get swapped, so it must match exactly). */
static uint32_t cb_swap_index(cb_crypt_t *c) {
    uint32_t roll = cb_rand(c), count = sizeof c->table;
    if (roll == count) roll = 0;
    if (roll < count) return roll;
    uint32_t bit = 1;
    while (count < 0x10000000u && count < roll) {
        count <<= 4;
        bit <<= 4;
    }
    while (count < 0x80000000u && count < roll) {
        count <<= 1;
        bit <<= 1;
    }
    uint32_t mask;
    for (;;) {
        mask = 0;
        if (roll >= count) roll -= count;
        if (roll >= count >> 1) {
            roll -= count >> 1;
            mask |= ror32(bit, 1);
        }
        if (roll >= count >> 2) {
            roll -= count >> 2;
            mask |= ror32(bit, 2);
        }
        if (roll >= count >> 3) {
            roll -= count >> 3;
            mask |= ror32(bit, 3);
        }
        if (!roll || !(bit >> 4)) break;
        bit >>= 4;
        count >>= 4;
    }
    mask &= 0xE0000000u;
    if (mask && (bit & 7)) {
        if (mask & ror32(bit, 3)) roll += count >> 3;
        if (mask & ror32(bit, 2)) roll += count >> 2;
        if (mask & ror32(bit, 1)) roll += count >> 1;
    }
    return roll % sizeof c->table; /* always < 48 already; the remainder only guards the table */
}

void cheat_cb_reseed(cb_crypt_t *c, uint32_t op1, uint16_t op2) {
    c->rng = (op2 & 0xFFu) ^ 0x1111u;
    for (unsigned i = 0; i < sizeof c->table; i++) c->table[i] = (uint8_t)i;
    for (int i = 0; i < 0x50; i++) {
        uint32_t x = cb_swap_index(c), y = cb_swap_index(c);
        uint8_t t = c->table[x];
        c->table[x] = c->table[y];
        c->table[y] = t;
    }
    c->rng = 0x4EFAD1C3u;
    for (uint32_t i = 0; i < ((op1 >> 24) & 0xF); i++) c->rng = cb_rand(c);
    c->seeds[2] = cb_rand(c);
    c->seeds[3] = cb_rand(c);
    c->rng = (uint32_t)(op2 >> 8) ^ 0xF254u;
    for (uint32_t i = 0; i < (uint32_t)(op2 >> 8); i++) c->rng = cb_rand(c);
    c->seeds[0] = cb_rand(c);
    c->seeds[1] = cb_rand(c);
    c->master = op1;
}

static void to_bytes(uint8_t b[6], uint32_t op1, uint16_t op2) {
    b[0] = (uint8_t)(op1 >> 24);
    b[1] = (uint8_t)(op1 >> 16);
    b[2] = (uint8_t)(op1 >> 8);
    b[3] = (uint8_t)op1;
    b[4] = (uint8_t)(op2 >> 8);
    b[5] = (uint8_t)op2;
}

static void from_bytes(const uint8_t b[6], uint32_t *op1, uint16_t *op2) {
    *op1 = (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3];
    *op2 = (uint16_t)(b[4] << 8 | b[5]);
}

/* Swap bit i of the 48 with bit table[i]. */
static void swap_bits(uint8_t b[6], unsigned i, unsigned j) {
    unsigned x = (b[i >> 3] >> (i & 7)) & 1, y = (b[j >> 3] >> (j & 7)) & 1;
    b[i >> 3] = (uint8_t)((b[i >> 3] & ~(1u << (i & 7))) | (y << (i & 7)));
    b[j >> 3] = (uint8_t)((b[j >> 3] & ~(1u << (j & 7))) | (x << (j & 7)));
}

void cheat_cb_decrypt(const cb_crypt_t *c, uint32_t *op1, uint16_t *op2) {
    uint8_t b[6];
    to_bytes(b, *op1, *op2);
    for (int i = (int)sizeof c->table - 1; i >= 0; i--) swap_bits(b, (unsigned)i, c->table[i]);
    from_bytes(b, op1, op2);
    *op1 ^= c->seeds[0];
    *op2 ^= (uint16_t)c->seeds[1];
    to_bytes(b, *op1, *op2);
    uint8_t k0 = (uint8_t)c->master, k1 = (uint8_t)(c->master >> 8);
    for (int i = 0; i < 5; i++) b[i] ^= k1 ^ b[i + 1];
    b[5] ^= k1;
    for (int i = 5; i > 0; i--) b[i] ^= k0 ^ b[i - 1];
    b[0] ^= k0;
    from_bytes(b, op1, op2);
    *op1 ^= c->seeds[2];
    *op2 ^= (uint16_t)c->seeds[3];
}

void cheat_cb_encrypt(const cb_crypt_t *c, uint32_t *op1, uint16_t *op2) {
    uint8_t b[6];
    *op1 ^= c->seeds[2];
    *op2 ^= (uint16_t)c->seeds[3];
    to_bytes(b, *op1, *op2);
    uint8_t k0 = (uint8_t)c->master, k1 = (uint8_t)(c->master >> 8);
    b[0] ^= k0;
    for (int i = 1; i <= 5; i++) b[i] ^= k0 ^ b[i - 1];
    b[5] ^= k1;
    for (int i = 4; i >= 0; i--) b[i] ^= k1 ^ b[i + 1];
    from_bytes(b, op1, op2);
    *op1 ^= c->seeds[0];
    *op2 ^= (uint16_t)c->seeds[1];
    to_bytes(b, *op1, *op2);
    for (unsigned i = 0; i < sizeof c->table; i++) swap_bits(b, i, c->table[i]);
    from_bytes(b, op1, op2);
}
