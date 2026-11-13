#include "gb_internal.h"

uint16_t gb_debug_reg(const gb_t *g, int which) {
    const gb_cpu_t *c = &g->cpu;
    switch (which) {
    case GB_REG_AF: return (uint16_t)(c->a << 8 | c->f);
    case GB_REG_BC: return (uint16_t)(c->b << 8 | c->c);
    case GB_REG_DE: return (uint16_t)(c->d << 8 | c->e);
    case GB_REG_HL: return (uint16_t)(c->h << 8 | c->l);
    case GB_REG_SP: return c->sp;
    default: return c->pc;
    }
}

uint8_t gb_debug_peek(const gb_t *g, uint16_t addr) {
    return gb_bus_peek(g, addr);
}

void gb_debug_step(gb_t *g) {
    gb_cpu_step(g);
}

uint64_t gb_debug_ticks(const gb_t *g) {
    return g->ticks;
}

int gb_debug_breakpoint(const gb_t *g) {
    return g->cpu.breakpoint;
}

size_t gb_debug_serial(const gb_t *g, char *out, size_t cap) {
    if (!cap) return 0;
    size_t n = g->serial_len < cap - 1 ? g->serial_len : cap - 1;
    for (size_t i = 0; i < n; i++) out[i] = g->serial_out[i];
    out[n] = 0;
    return n;
}
