#include "bios/hle_internal.h"
#include "cpu/cpu_mem.h"
#include "gba_internal.h"

/* The BIOS refuses to copy from its own address range. */
static bool source_allowed(uint32_t src) {
    return (src & 0x0E000000u) != 0;
}

/* r0 = source, r1 = destination, r2 = count (bits 0-20) | fill (bit 24) | 32-bit units (bit 26).
 * Like the BIOS: 32-bit units move with LDMIA/STMIA (the bus aligns, the save area keeps exact
 * addresses), 16-bit units with LDRH/STRH (a misaligned read rotates the halfword). */
void hle_cpu_set(gba_t *g) {
    arm_cpu_t *c = &g->cpu;
    uint32_t src = c->r[0], dst = c->r[1], ctrl = c->r[2];
    uint32_t count = ctrl & 0x1FFFFF;
    bool fill = (ctrl >> 24) & 1, words = (ctrl >> 26) & 1;
    if (!source_allowed(src)) return;
    uint32_t unit = words ? 4 : 2;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t from = fill ? src : src + i * unit;
        uint32_t to = dst + i * unit;
        if (words) bus_write32(g, to, bus_read32(g, from, i > 0), i > 0);
        else bus_write16(g, to, (uint16_t)cpu_load_half(g, from), i > 0);
        bus_idle(g, 1); /* the BIOS loop's load has an internal cycle */
    }
}

/* Like CpuSet with 32-bit units (LDMIA/STMIA), but the count is rounded up to a multiple of 8 words. */
void hle_cpu_fast_set(gba_t *g) {
    arm_cpu_t *c = &g->cpu;
    uint32_t src = c->r[0], dst = c->r[1], ctrl = c->r[2];
    uint32_t count = ((ctrl & 0x1FFFFF) + 7) & ~7u;
    bool fill = (ctrl >> 24) & 1;
    if (!source_allowed(src)) return;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t from = fill ? src : src + i * 4;
        bus_write32(g, dst + i * 4, bus_read32(g, from, i > 0), i > 0);
        bus_idle(g, 1);
    }
}

/* r0 = source, r1 = destination (word-aligned), r2 = info: u16 length, u8 src width, u8 dst width, u32 offset|zero flag */
void hle_bit_unpack(gba_t *g) {
    arm_cpu_t *c = &g->cpu;
    uint32_t src = c->r[0], dst = c->r[1] & ~3u, info = c->r[2];
    uint32_t len = bus_read16(g, info, false);
    uint32_t src_width = bus_read8(g, info + 2, false);
    uint32_t dst_width = bus_read8(g, info + 3, false);
    uint32_t data = bus_read32(g, info + 4, false);
    uint32_t offset = data & 0x7FFFFFFF;
    bool offset_zero = (data >> 31) != 0;
    bool src_ok = src_width == 1 || src_width == 2 || src_width == 4 || src_width == 8;
    bool dst_ok = dst_width == 1 || dst_width == 2 || dst_width == 4 || dst_width == 8 || dst_width == 16 ||
                  dst_width == 32;
    if (!src_ok || !dst_ok) return;

    uint32_t src_mask = (1u << src_width) - 1;
    uint32_t out = 0, out_bits = 0;
    for (uint32_t i = 0; i < len; i++) {
        uint32_t byte = bus_read8(g, src + i, false);
        for (uint32_t bit = 0; bit < 8; bit += src_width) {
            uint32_t unit = (byte >> bit) & src_mask;
            if (unit || offset_zero) unit += offset;
            out |= unit << out_bits; /* not cut to the unit's width: a unit the offset overflows spills into the next */
            out_bits += dst_width;
            if (out_bits == 32) {
                bus_write32(g, dst, out, false);
                dst += 4;
                out = 0;
                out_bits = 0;
            }
        }
    }
}
