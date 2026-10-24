/* Running the cheats: the steps at the end of every frame, the ROM patches while the list is set. */
#include <stdlib.h>
#include <string.h>

#include "cheats/cheats.h"
#include "gba_internal.h"

static uint32_t rd(gba_t *g, uint32_t addr, unsigned width) {
    return bus_peek(g, addr, width);
}

static bool holds(gba_t *g, const cheat_step_t *s) {
    if (s->cond == IF_NEVER || s->cond == IF_BUTTON) return false; /* no cheat-device button here */
    uint32_t v = rd(g, s->addr, s->width), x = s->value;
    switch (s->cond) {
    case IF_EQ: return v == x;
    case IF_NE: return v != x;
    case IF_LT: return (int32_t)v < (int32_t)x;
    case IF_GT: return (int32_t)v > (int32_t)x;
    case IF_LE: return (int32_t)v <= (int32_t)x;
    case IF_GE: return (int32_t)v >= (int32_t)x;
    case IF_ULT: return v < x;
    case IF_UGT: return v > x;
    case IF_AND: return (v & x) != 0;
    case IF_NAND: return (v & x) == 0;
    default: return false;
    }
}

static void do_step(gba_t *g, const cheat_step_t *s) {
    uint32_t addr = s->addr, operand = s->value;
    for (uint32_t n = 0; n < s->count; n++) {
        uint32_t target = addr, value;
        switch (s->kind) {
        case STEP_WRITE: value = operand; break;
        case STEP_WRITE_INDIRECT: /* the address holds a pointer; the step is an offset from it */
            value = operand;
            target = rd(g, addr, 4) + s->addr_step;
            break;
        case STEP_ADD: value = rd(g, addr, s->width) + operand; break;
        case STEP_AND: value = rd(g, addr, s->width) & operand; break;
        case STEP_OR: value = rd(g, addr, s->width) | operand; break;
        default: return;
        }
        bus_poke(g, target, value, s->width);
        if (s->kind != STEP_WRITE_INDIRECT) addr += s->addr_step;
        operand += s->value_step;
    }
}

/* Steps [i, end): a condition runs its "then" steps when it holds, else the "else" steps after them. */
static void run(gba_t *g, const cheat_step_t *steps, size_t i, size_t end) {
    while (i < end) {
        const cheat_step_t *s = &steps[i];
        if (s->kind != STEP_IF) {
            do_step(g, s);
            i++;
            continue;
        }
        size_t then_end = i + 1 + s->then_n, else_end;
        if (then_end > end) then_end = end;
        else_end = then_end + s->else_n;
        if (else_end > end) else_end = end;
        if (holds(g, s)) run(g, steps, i + 1, then_end);
        else run(g, steps, then_end, else_end);
        i = else_end;
    }
}

void cheats_apply(gba_t *g) {
    cheat_list_t *l = g->cheats;
    if (l && l->nsteps) run(g, l->steps, 0, l->nsteps);
}

static bool in_rom(const gba_t *g, uint32_t addr) {
    uint32_t off = addr & 0x01FFFFFFu;
    return addr >= 0x08000000u && addr < 0x0E000000u && off + 1 < g->cart.rom_size;
}

static void patch_rom(gba_t *g, cheat_list_t *l) {
    for (size_t i = 0; i < l->npatches; i++) {
        cheat_patch_t *p = &l->patches[i];
        if (!in_rom(g, p->addr)) continue;
        uint8_t *b = g->cart.rom + (p->addr & 0x01FFFFFFu);
        p->old = (uint16_t)(b[0] | b[1] << 8);
        b[0] = (uint8_t)p->value;
        b[1] = (uint8_t)(p->value >> 8);
    }
    l->applied = true;
}

static void unpatch_rom(gba_t *g, cheat_list_t *l) {
    if (!l->applied) return;
    for (size_t i = l->npatches; i-- > 0;) { /* last first: two patches of one place restore the original */
        const cheat_patch_t *p = &l->patches[i];
        if (!in_rom(g, p->addr)) continue;
        uint8_t *b = g->cart.rom + (p->addr & 0x01FFFFFFu);
        b[0] = (uint8_t)p->old;
        b[1] = (uint8_t)(p->old >> 8);
    }
    l->applied = false;
}

void cheats_drop(gba_t *g) {
    if (!g->cheats) return;
    unpatch_rom(g, g->cheats);
    cheat_list_free(g->cheats);
    free(g->cheats);
    g->cheats = NULL;
}

gba_err_t gba_cheats_set(gba_t *g, const char *const *texts, const gba_cheat_type_t *types, size_t count) {
    cheats_drop(g);
    if (!count) return GBA_OK;
    cheat_list_t *l = calloc(1, sizeof *l);
    if (!l) return GBA_ERR_NOMEM;
    for (size_t i = 0; i < count; i++) {
        if (cheat_decode(texts[i], types ? types[i] : GBA_CHEAT_AUTO, l, NULL) != 0) {
            cheat_list_free(l);
            free(l);
            return GBA_ERR_CHEAT;
        }
    }
    patch_rom(g, l);
    g->cheats = l;
    return GBA_OK;
}
