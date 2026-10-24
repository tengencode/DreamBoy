#ifndef GBA_ALU_H
#define GBA_ALU_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t value;
    bool carry, overflow;
} alu_result_t;

static inline uint32_t ror32(uint32_t v, uint32_t n) {
    n &= 31;
    return n ? (v >> n) | (v << (32 - n)) : v;
}

/* a + b + carry_in with ARM carry-out and signed overflow. Subtraction a - b is alu_add(a, ~b, 1). */
static inline alu_result_t alu_add(uint32_t a, uint32_t b, uint32_t carry_in) {
    uint64_t wide = (uint64_t)a + b + carry_in;
    alu_result_t r;
    r.value = (uint32_t)wide;
    r.carry = (wide >> 32) != 0;
    r.overflow = ((~(a ^ b) & (a ^ r.value)) >> 31) != 0;
    return r;
}

/*
 * Barrel shifter with an immediate amount (0-31). type: 0 LSL, 1 LSR, 2 ASR, 3 ROR.
 * Amount 0 encodes: LSL #0 = no shift, LSR/ASR #0 = #32, ROR #0 = RRX.
 * *carry holds C on entry and the shifter carry-out on return.
 */
static inline uint32_t shift_by_imm(uint32_t type, uint32_t v, uint32_t amount, bool *carry) {
    switch (type) {
    case 0:
        if (amount == 0) return v;
        *carry = (v >> (32 - amount)) & 1;
        return v << amount;
    case 1:
        if (amount == 0) {
            *carry = v >> 31;
            return 0;
        }
        *carry = (v >> (amount - 1)) & 1;
        return v >> amount;
    case 2:
        if (amount == 0) {
            *carry = v >> 31;
            return (uint32_t)((int32_t)v >> 31);
        }
        *carry = (v >> (amount - 1)) & 1;
        return (uint32_t)((int32_t)v >> amount);
    default:
        if (amount == 0) {
            bool old = *carry;
            *carry = v & 1;
            return (v >> 1) | ((uint32_t)old << 31);
        }
        *carry = (v >> (amount - 1)) & 1;
        return ror32(v, amount);
    }
}

/* Barrel shifter with a register amount (bottom byte of Rs, 0-255). Amount 0 leaves v and C alone. */
static inline uint32_t shift_by_reg(uint32_t type, uint32_t v, uint32_t amount, bool *carry) {
    if (amount == 0) return v;
    switch (type) {
    case 0:
        if (amount < 32) {
            *carry = (v >> (32 - amount)) & 1;
            return v << amount;
        }
        *carry = amount == 32 ? (v & 1) : 0;
        return 0;
    case 1:
        if (amount < 32) {
            *carry = (v >> (amount - 1)) & 1;
            return v >> amount;
        }
        *carry = amount == 32 ? (v >> 31) : 0;
        return 0;
    case 2:
        if (amount < 32) {
            *carry = (v >> (amount - 1)) & 1;
            return (uint32_t)((int32_t)v >> amount);
        }
        *carry = v >> 31;
        return (uint32_t)((int32_t)v >> 31);
    default:
        amount &= 31;
        if (amount == 0) {
            *carry = v >> 31;
            return v;
        }
        *carry = (v >> (amount - 1)) & 1;
        return ror32(v, amount);
    }
}

/* Multiplier array cycles (1-4): the multiplier stops early when the top bytes of rs are all 0s (or all 1s when signed). */
static inline uint32_t alu_mul_cycles(uint32_t rs, bool allow_ones) {
    if ((rs >> 8) == 0 || (allow_ones && (rs >> 8) == 0x00FFFFFF)) return 1;
    if ((rs >> 16) == 0 || (allow_ones && (rs >> 16) == 0xFFFF)) return 2;
    if ((rs >> 24) == 0 || (allow_ones && (rs >> 24) == 0xFF)) return 3;
    return 4;
}

#endif
