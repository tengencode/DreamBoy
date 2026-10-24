/* ARM7TDMI multiply carry flag.
 *
 * Adapted from zaydlang's multiplication-algorithm (https://github.com/zaydlang/multiplication-algorithm,
 * zlib licence; write-up: https://bmchtech.github.io/post/multiply/), which models the chip's Booth
 * recoding and carry-save adder array. This version keeps only the part the carry flag comes from. */
#include "cpu/mul_carry.h"

typedef struct {
    uint64_t sum, carry;
} csa_t;

typedef struct {
    uint64_t value;
    bool carry;
} booth_t;

static bool bit(uint64_t x, int n) {
    return (x >> n) & 1;
}

static uint64_t sign_extend(uint64_t v, int from, int to) {
    if (bit(v, from - 1)) v |= ((1ull << (to - from)) - 1) << from;
    return v;
}

/* Arithmetic shift right of a 33-bit value. */
static uint64_t asr33(uint64_t v, int shift) {
    int64_t s = (int64_t)sign_extend(v, 33, 64);
    return (uint64_t)(s >> shift) & 0x1FFFFFFFFull;
}

static void ror128(uint64_t *lo, uint64_t *hi, int s) {
    uint64_t l = *lo, h = *hi;
    *lo = (l >> s) | (h << (64 - s));
    *hi = (h >> s) | (l << (64 - s));
}

/* One radix-4 Booth digit (3 multiplier bits) → the addend and whether it was negated. */
static booth_t booth_recode(uint64_t m, unsigned chunk) {
    booth_t b = {0, false};
    switch (chunk) {
    case 1:
    case 2: b.value = m; break;
    case 3: b.value = 2 * m; break;
    case 4: b = (booth_t){~(2 * m), true}; break;
    case 5:
    case 6: b = (booth_t){~m, true}; break;
    default: break; /* 0 and 7 add nothing */
    }
    b.value &= 0x3FFFFFFFFull;
    return b;
}

/* Four carry-save adders: one multiplier cycle (8 multiplier bits). */
static csa_t csa_array(csa_t in, const booth_t m[4], uint64_t *acc_shift) {
    csa_t cur = in, out = {0, 0};
    for (int i = 0; i < 4; i++) {
        cur.sum &= 0x1FFFFFFFFull;
        cur.carry &= 0x1FFFFFFFFull;
        uint64_t addend = m[i].value & 0x1FFFFFFFFull;
        csa_t r = {cur.sum ^ addend ^ cur.carry, (cur.sum & addend) | (addend & cur.carry) | (cur.carry & cur.sum)};
        r.carry = r.carry << 1 | m[i].carry;
        out.sum |= (r.sum & 3) << (2 * i);
        out.carry |= (r.carry & 3) << (2 * i);
        r.sum >>= 2;
        r.carry >>= 2;
        uint64_t magic = (uint64_t)bit(*acc_shift, 0) + !bit(cur.carry, 32) + !bit(m[i].value, 33);
        r.sum |= magic << 31;
        r.carry |= (uint64_t)!bit(*acc_shift, 1) << 32;
        *acc_shift >>= 2;
        cur = r;
    }
    out.sum |= cur.sum << 8;
    out.carry |= cur.carry << 8;
    return out;
}

bool mul_carry(mul_kind_t kind, uint32_t rm, uint32_t rs, uint64_t acc) {
    bool is_signed = kind != MUL_LONG_UNSIGNED;
    uint64_t multiplier = is_signed ? sign_extend(rs, 32, 34) : rs;
    uint64_t multiplicand = is_signed ? sign_extend(rm, 32, 34) : rm;
    csa_t csa = {acc, (multiplier & 1) ? ~multiplicand : 0};
    uint64_t acc_shift = acc >> 34;
    uint64_t lo = csa.carry & 1, hi = 0; /* the carry half of the partial product, rotated as it grows */
    csa.sum >>= 1;
    csa.carry >>= 1;
    ror128(&lo, &hi, 1);
    int cycles = 0;
    do {
        booth_t m[4];
        for (int i = 0; i < 4; i++) m[i] = booth_recode(multiplicand, (unsigned)(multiplier >> (2 * i)) & 7);
        csa = csa_array(csa, m, &acc_shift);
        lo |= csa.carry & 0xFF;
        csa.sum >>= 8;
        csa.carry >>= 8;
        ror128(&lo, &hi, 8);
        multiplier = asr33(multiplier, 8);
        cycles++;
    } while (multiplier != 0 && !(is_signed && multiplier == 0x1FFFFFFFFull));
    lo |= csa.carry;
    static const int realign[5] = {0, 23, 15, 7, 31};
    ror128(&lo, &hi, realign[cycles]);
    if (kind == MUL_SHORT && cycles == 4) return bit(hi, 31);
    return bit(hi, 63);
}
