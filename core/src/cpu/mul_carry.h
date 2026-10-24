#ifndef GBA_MUL_CARRY_H
#define GBA_MUL_CARRY_H

#include <stdbool.h>
#include <stdint.h>

typedef enum { MUL_SHORT, MUL_LONG_SIGNED, MUL_LONG_UNSIGNED } mul_kind_t;

/* The carry flag a flag-setting ARM7TDMI multiply leaves: rm = multiplicand, rs = multiplier (the
 * register the timing depends on), acc = Rn for MLA, RdHi:RdLo for UMLAL/SMLAL, 0 otherwise. */
bool mul_carry(mul_kind_t kind, uint32_t rm, uint32_t rs, uint64_t acc);

#endif
