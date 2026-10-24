#include <string.h>

#include "bios/hle_internal.h"
#include "gba_internal.h"

/* Output sink: byte writes, or (VRAM variants) byte pairs combined into halfword writes. */
typedef struct {
    uint32_t addr;
    bool halfwords;
    bool has_pending;
    uint8_t pending;
} out_writer_t;

static out_writer_t writer_init(uint32_t addr, bool halfwords) {
    out_writer_t w = {addr, halfwords, false, 0};
    return w;
}

static void writer_put(gba_t *g, out_writer_t *w, uint8_t v) {
    if (!w->halfwords) {
        bus_write8(g, w->addr++, v, false);
        return;
    }
    if (!w->has_pending) {
        w->pending = v;
        w->has_pending = true;
        return;
    }
    bus_write16(g, w->addr, (uint16_t)(w->pending | v << 8), false);
    w->addr += 2;
    w->has_pending = false;
}

static void writer_flush(gba_t *g, out_writer_t *w) {
    if (w->halfwords && w->has_pending) bus_write16(g, w->addr, w->pending, false);
    w->has_pending = false;
}

static uint8_t read_byte(gba_t *g, uint32_t *src) {
    return (uint8_t)bus_read8(g, (*src)++, false);
}

/* Every compressed stream starts with a word: type in bits 4-7, decompressed size in bits 8-31. */
static uint32_t read_header(gba_t *g, uint32_t *src) {
    uint32_t header = bus_read32(g, *src, false);
    *src += 4;
    return header;
}

void hle_lz77(gba_t *g, bool vram) {
    uint32_t src = g->cpu.r[0] & ~3u;
    uint32_t size = read_header(g, &src) >> 8;
    out_writer_t w = writer_init(g->cpu.r[1], vram);
    uint8_t window[4096]; /* the maximum displacement is 4096 */
    memset(window, 0, sizeof window);
    uint32_t written = 0;
    while (written < size) {
        uint8_t flags = read_byte(g, &src);
        for (int bit = 7; bit >= 0 && written < size; bit--) {
            if (flags & (1u << bit)) {
                uint8_t hi = read_byte(g, &src), lo = read_byte(g, &src);
                uint32_t len = (hi >> 4) + 3u;
                uint32_t disp = (((hi & 0xFu) << 8) | lo) + 1;
                for (uint32_t j = 0; j < len && written < size; j++) {
                    uint8_t v = window[(written - disp) & 0xFFF];
                    window[written & 0xFFF] = v;
                    writer_put(g, &w, v);
                    written++;
                }
            } else {
                uint8_t v = read_byte(g, &src);
                window[written & 0xFFF] = v;
                writer_put(g, &w, v);
                written++;
            }
        }
    }
    writer_flush(g, &w);
}

void hle_huffman(gba_t *g) {
    uint32_t src = g->cpu.r[0] & ~3u, dst = g->cpu.r[1] & ~3u;
    uint32_t header = read_header(g, &src);
    uint32_t bits = header & 0xF; /* 4 or 8 */
    uint32_t remaining = header >> 8;
    uint32_t tree = src;
    uint32_t stream = tree + (bus_read8(g, tree, false) + 1) * 2;
    uint32_t root = tree + 1, node = root;
    uint32_t out = 0, out_bits = 0;
    if (bits != 4 && bits != 8) return;

    while (remaining > 0) {
        uint32_t word = bus_read32(g, stream, false);
        stream += 4;
        for (int i = 31; i >= 0 && remaining > 0; i--) {
            uint32_t bit = (word >> i) & 1;
            uint32_t n = bus_read8(g, node, false);
            uint32_t child = (node & ~1u) + (n & 0x3F) * 2 + 2 + bit;
            bool is_data = (n & (bit ? 0x40u : 0x80u)) != 0;
            if (!is_data) {
                node = child;
                continue;
            }
            out |= (bus_read8(g, child, false) & ((1u << bits) - 1)) << out_bits;
            out_bits += bits;
            node = root;
            if (out_bits == 32) {
                bus_write32(g, dst, out, false);
                dst += 4;
                remaining = remaining > 4 ? remaining - 4 : 0;
                out = 0;
                out_bits = 0;
            }
        }
    }
}

void hle_rl(gba_t *g, bool vram) {
    uint32_t src = g->cpu.r[0] & ~3u;
    uint32_t size = read_header(g, &src) >> 8;
    out_writer_t w = writer_init(g->cpu.r[1], vram);
    uint32_t written = 0;
    while (written < size) {
        uint8_t flag = read_byte(g, &src);
        if (flag & 0x80) {
            uint32_t len = (flag & 0x7Fu) + 3;
            uint8_t v = read_byte(g, &src);
            for (uint32_t j = 0; j < len && written < size; j++, written++) writer_put(g, &w, v);
        } else {
            uint32_t len = (flag & 0x7Fu) + 1;
            for (uint32_t j = 0; j < len && written < size; j++, written++) writer_put(g, &w, read_byte(g, &src));
        }
    }
    writer_flush(g, &w);
}

void hle_diff8(gba_t *g, bool vram) {
    uint32_t src = g->cpu.r[0] & ~3u;
    uint32_t size = read_header(g, &src) >> 8;
    out_writer_t w = writer_init(g->cpu.r[1], vram);
    uint8_t value = 0;
    for (uint32_t i = 0; i < size; i++) {
        value = (uint8_t)(value + read_byte(g, &src));
        writer_put(g, &w, value);
    }
    writer_flush(g, &w);
}

void hle_diff16(gba_t *g) {
    uint32_t src = g->cpu.r[0] & ~3u, dst = g->cpu.r[1];
    uint32_t size = read_header(g, &src) >> 8;
    uint16_t value = 0;
    for (uint32_t i = 0; i < size; i += 2) {
        value = (uint16_t)(value + bus_read16(g, src + i, false));
        bus_write16(g, dst + i, value, false);
    }
}
