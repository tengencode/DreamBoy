#ifndef GBA_CPU_MEM_H
#define GBA_CPU_MEM_H

#include <stdint.h>

#include "cpu/alu.h"
#include "mem/bus.h"

/* ARM7TDMI load semantics shared by the ARM and Thumb handlers. */

static inline uint32_t cpu_load_word(gba_t *g, uint32_t addr) {
    return ror32(bus_read32(g, addr, false), (addr & 3) * 8); /* misaligned: rotated */
}

static inline uint32_t cpu_load_half(gba_t *g, uint32_t addr) {
    return ror32(bus_read16(g, addr, false), (addr & 1) * 8); /* misaligned: rotated */
}

static inline uint32_t cpu_load_signed_byte(gba_t *g, uint32_t addr) {
    return (uint32_t)(int32_t)(int8_t)bus_read8(g, addr, false);
}

static inline uint32_t cpu_load_signed_half(gba_t *g, uint32_t addr) {
    if (addr & 1) return cpu_load_signed_byte(g, addr); /* misaligned: loads a signed byte */
    return (uint32_t)(int32_t)(int16_t)bus_read16(g, addr, false);
}

#endif
