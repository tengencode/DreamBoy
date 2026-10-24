/* A cheat's text → steps. The formats' meanings follow mGBA's reading of them (our own code). */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "cheats/cheats.h"

#define ROM_BASE 0x08000000u
#define IO_BASE 0x04000000u
#define KEYINPUT_ADDR 0x04000130u
#define GAME_ID_MARK 0x001DC0DEu /* second word of the device's game-id line */
#define RESEED_MARK 0xDEADFACEu  /* re-keying line: not supported */

/* ---- lists ---- */

bool cheat_push_step(cheat_list_t *l, cheat_step_t s) {
    if (l->nsteps == l->capsteps) {
        size_t cap = l->capsteps ? l->capsteps * 2 : 16;
        cheat_step_t *p = realloc(l->steps, cap * sizeof *p);
        if (!p) return false;
        l->steps = p;
        l->capsteps = cap;
    }
    l->steps[l->nsteps++] = s;
    return true;
}

bool cheat_push_patch(cheat_list_t *l, uint32_t addr, uint16_t value) {
    if (l->npatches == l->cappatches) {
        size_t cap = l->cappatches ? l->cappatches * 2 : 4;
        cheat_patch_t *p = realloc(l->patches, cap * sizeof *p);
        if (!p) return false;
        l->patches = p;
        l->cappatches = cap;
    }
    l->patches[l->npatches++] = (cheat_patch_t){addr & ~1u, value, 0};
    return true;
}

void cheat_list_free(cheat_list_t *l) {
    free(l->steps);
    free(l->patches);
    memset(l, 0, sizeof *l);
}

static cheat_step_t write_step(uint32_t addr, uint32_t value, unsigned width) {
    return (cheat_step_t){.kind = STEP_WRITE, .width = (uint8_t)width, .addr = addr, .value = value, .count = 1};
}

static cheat_step_t if_step(if_kind_t cond, uint32_t addr, uint32_t value, unsigned width, uint32_t then_n) {
    return (cheat_step_t){.kind = STEP_IF, .cond = (uint8_t)cond, .width = (uint8_t)width, .addr = addr,
                          .value = value, .count = 1, .then_n = then_n};
}

static uint32_t width_mask(unsigned width) {
    return width >= 4 ? 0xFFFFFFFFu : (1u << (8 * width)) - 1;
}

/* ---- how likely an address is real game memory (auto-detection) ---- */

static int address_score(uint32_t addr) {
    uint32_t off = addr & 0x00FFFFFFu;
    switch (addr >> 24) {
    case 0x0: return -0x80; /* BIOS */
    case 0x2: return off > 0x40000 ? -0x40 : 0x20;
    case 0x3: return off > 0x8000 ? -0x40 : 0x20;
    case 0x4: return off > 0x400 ? -0x80 : 0x10;
    case 0x5:
    case 0x7: return off > 0x400 ? -0x80 : -0x8;
    case 0x6: return off > 0x18000 ? -0x80 : -0x8;
    case 0x8: case 0x9: case 0xA: case 0xB: case 0xC: case 0xD: return -0x8;
    case 0xE: case 0xF: return off > 0x10000 ? -0x80 : -0x8;
    default: return -0xC0;
    }
}

static int gs_score(uint32_t op1, uint32_t op2) {
    if (op2 == GAME_ID_MARK) return 0x100;
    uint32_t addr = op1 & 0x0FFFFFFFu;
    switch (op1 >> 28) {
    case 0x0: return 0x20 - ((op2 & 0xFFFFFF00u) ? 0x10 : 0) + address_score(addr);
    case 0x1: return 0x20 - ((op2 & 0xFFFF0000u) ? 0x10 : 0) + address_score(addr);
    case 0x2: return 0x20 + address_score(addr);
    case 0x6: return 0x20 - ((op2 & 0xCFFF0000u) ? 0x10 : 0);
    case 0x8: return 0x10;
    case 0xD: return 0x20 - ((op2 & 0xFFCF0000u) ? 0x10 : 0) + address_score(addr);
    case 0xE: return 0x20 + address_score(op2) - ((op1 & 0x0F000000u) ? 0x10 : 0);
    case 0xF: return 0x20 - ((op2 & 0xFFFF0000u) ? 0x10 : 0);
    default: return -0x40;
    }
}

/* Action Replay v3 addresses: 0x0T0XXXXX (region in bits 20-23) → 0x0T0XXXXX with the region moved to 24-27. */
static uint32_t ar_addr(uint32_t x) {
    return (x & 0xFFFFFu) | ((x << 4) & 0x0F000000u);
}

static int ar_score(uint32_t op1, uint32_t op2) {
    if (op2 == GAME_ID_MARK) return 0x100;
    if (op1 == RESEED_MARK && !(op2 & 0xFFFF0000u)) return 0x100;
    if (!op1) {
        switch (op2 & 0xFE000000u) {
        case 0x80000000u: case 0x82000000u: case 0x84000000u: return 0x40 + address_score(ar_addr(op2)); /* fills */
        case 0x18000000u: case 0x1A000000u: case 0x1C000000u: case 0x1E000000u: return 0x40;             /* ROM patches */
        case 0x00000000u: case 0x08000000u: case 0x10000000u: case 0x12000000u: case 0x14000000u:
        case 0x40000000u: case 0x60000000u: return 0x40 - ((op2 & 0x01000000u) ? 0x40 : 0);
        default: return 0;
        }
    }
    unsigned bits = (op1 & 0x06000000u) >> 22; /* 0, 8, 16 or 24 (never): mGBA's measure of the width */
    if (op1 & 0x38000000u) { /* a condition */
        if (bits >= 24) return 0;
        return 0x20 - ((op2 & ~((1u << bits) - 1)) ? 0x10 : 0);
    }
    int p = 0x20;
    switch (op1 & 0xC0000000u) {
    case 0x80000000u: /* add */
        if (op2 & ~((1u << bits) - 1)) p -= 0x10;
        /* fall through */
    case 0x00000000u: /* write */
    case 0x40000000u: /* write through a pointer */
        p += address_score(ar_addr(op1));
        if (op1 & 0x01000000u) return 0;
        break;
    default: break; /* I/O */
    }
    return p;
}

/* ---- reading lines ---- */

typedef enum { LINE_RAW, LINE_CB, LINE_GSAR } line_kind_t;

typedef struct {
    line_kind_t kind;
    uint32_t a;    /* first word */
    uint32_t b;    /* second word (CB: 16 bits; raw: the value) */
    unsigned width; /* raw: 1, 2 or 4 */
} code_line_t;

static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = tolower(c);
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

/* One line without spaces: "AAAAAAAA:VV..", 12 hex digits or 16. False if it is none of them. */
static bool read_line(const char *s, size_t n, code_line_t *out) {
    char buf[40];
    size_t k = 0;
    for (size_t i = 0; i < n; i++) {
        if (isspace((unsigned char)s[i])) continue;
        if (k + 1 >= sizeof buf) return false;
        buf[k++] = s[i];
    }
    buf[k] = '\0';
    const char *colon = strchr(buf, ':');
    size_t digits = colon ? (size_t)(colon - buf) : k;
    uint64_t v = 0;
    for (size_t i = 0; i < digits; i++) {
        int h = hexval(buf[i]);
        if (h < 0) return false;
        v = v << 4 | (uint64_t)h;
    }
    if (colon) {
        size_t vd = strlen(colon + 1);
        if (digits != 8 || (vd != 2 && vd != 4 && vd != 8)) return false;
        uint32_t val = 0;
        for (size_t i = 0; i < vd; i++) {
            int h = hexval(colon[1 + i]);
            if (h < 0) return false;
            val = val << 4 | (uint32_t)h;
        }
        *out = (code_line_t){LINE_RAW, (uint32_t)v, val, (unsigned)vd / 2};
        return true;
    }
    if (digits == 12) {
        *out = (code_line_t){LINE_CB, (uint32_t)(v >> 16), (uint32_t)(v & 0xFFFF), 0};
        return true;
    }
    if (digits == 16) {
        *out = (code_line_t){LINE_GSAR, (uint32_t)(v >> 32), (uint32_t)v, 0};
        return true;
    }
    return false;
}

/* ---- decoding, one code at a time ---- */

typedef struct {
    cheat_list_t *out;
    size_t first;        /* this cheat's first step (blocks and counts stay inside it) */
    bool scrambled;      /* GS/AR: TEA-scrambled */
    /* multi-line codes */
    size_t pending;      /* index of a step whose next line completes it (SIZE_MAX: none) */
    int pending_kind;    /* 0 none, 1 AR fill/button, 2 CB fill, 3 CB fill list, 4 GS address list, 5 AR ROM patch */
    uint32_t remaining;  /* CB fill list halfwords / GS list addresses still to come */
    uint32_t list_value; /* GS address list: the value */
    uint32_t patch_addr; /* AR ROM patch: its address */
    size_t block;        /* AR: the open if-block's step (SIZE_MAX: none) */
    cb_crypt_t cb;
} decoder_t;

#define NONE ((size_t)-1)

static bool push(decoder_t *d, cheat_step_t s) {
    return cheat_push_step(d->out, s);
}

static size_t steps_since(const decoder_t *d, size_t i) {
    return d->out->nsteps - i - 1;
}

/* GameShark v1/v2, from the plain words. */
static bool decode_gs(decoder_t *d, uint32_t op1, uint32_t op2) {
    if (d->pending_kind == 4) { /* the addresses of a 32-bit list write, two per line */
        uint32_t addrs[2] = {op1, op2};
        for (int i = 0; i < 2 && d->remaining; i++, d->remaining--) {
            if (!push(d, write_step(addrs[i] & 0x0FFFFFFFu, d->list_value, 4))) return false;
        }
        if (!d->remaining) d->pending_kind = 0;
        return true;
    }
    uint32_t addr = op1 & 0x0FFFFFFFu;
    switch (op1 >> 28) {
    case 0x0: return push(d, write_step(addr, op2, 1));
    case 0x1: return push(d, write_step(addr, op2, 2));
    case 0x2: return push(d, write_step(addr, op2, 4));
    case 0x3: /* 3000CCCC VVVVVVVV: VVVVVVVV to the CCCC addresses on the following lines */
        d->remaining = op1 & 0xFFFF;
        d->list_value = op2;
        d->pending_kind = d->remaining ? 4 : 0;
        return true;
    case 0x6: return cheat_push_patch(d->out, ROM_BASE | ((op1 & 0xFFFFFFu) << 1), (uint16_t)op2);
    case 0x8: { /* while the cheat device's button is held: never here */
        unsigned width = (op1 & 0x00F00000u) == 0x00100000u ? 1 : (op1 & 0x00F00000u) == 0x00200000u ? 2 : 0;
        if (!width) return false;
        return push(d, if_step(IF_BUTTON, 0, 0, 0, 1)) && push(d, write_step(op1 & 0x0F0FFFFFu, op2, width));
    }
    case 0xD: {
        if (op1 == RESEED_MARK) return false;
        static const if_kind_t conds[4] = {IF_EQ, IF_NE, IF_LE, IF_GE};
        unsigned c = (op2 >> 20) & 0xF;
        if (c > 3) return false;
        return push(d, if_step(conds[c], addr, op2 & 0xFFFF, 2, 1));
    }
    case 0xE: return push(d, if_step(IF_EQ, op2 & 0x0FFFFFFFu, op1 & 0xFFFF, 2, (op1 >> 16) & 0xFF));
    case 0xF: return true; /* the hook (master code) */
    default: return false;
    }
}

/* Action Replay v3: close the open if-block at an "end if" (or when the next block starts). */
static void ar_end_block(decoder_t *d) {
    cheat_step_t *s = &d->out->steps[d->block];
    uint32_t size = (uint32_t)steps_since(d, d->block);
    if (s->then_n) s->else_n = size - s->then_n; /* it had an "else" */
    else s->then_n = size;
    d->block = NONE;
}

static bool decode_ar_special(decoder_t *d, uint32_t op2) {
    switch (op2 & 0xFF000000u) {
    case 0x00000000u: return true; /* end of the list */
    case 0x08000000u: return true; /* slowdown: ignored */
    case 0x10000000u: case 0x12000000u: case 0x14000000u: { /* while the device's button is held: never here */
        unsigned width = 1u << ((op2 >> 25) & 3);
        if (!push(d, if_step(IF_BUTTON, 0, 0, 0, 1)) || !push(d, write_step(ar_addr(op2), 0, width))) return false;
        d->pending = d->out->nsteps - 1;
        d->pending_kind = 1;
        return true;
    }
    case 0x18000000u: case 0x1A000000u: case 0x1C000000u: case 0x1E000000u: /* ROM patch: the value on the next line */
        d->patch_addr = ROM_BASE | ((op2 & 0xFFFFFFu) << 1);
        d->pending_kind = 5;
        return true;
    case 0x40000000u: /* end if */
        if (d->block == NONE) return false;
        ar_end_block(d);
        return true;
    case 0x60000000u: /* else */
        if (d->block == NONE) return false;
        d->out->steps[d->block].then_n = (uint32_t)steps_since(d, d->block);
        return true;
    case 0x80000000u: case 0x82000000u: case 0x84000000u: { /* fill: the value and the counts on the next line */
        unsigned width = 1u << ((op2 >> 25) & 3);
        if (!push(d, write_step(ar_addr(op2), 0, width))) return false;
        d->pending = d->out->nsteps - 1;
        d->pending_kind = 1;
        return true;
    }
    default: return false;
    }
}

static bool decode_ar(decoder_t *d, uint32_t op1, uint32_t op2) {
    if (d->pending_kind == 5) {
        d->pending_kind = 0;
        return cheat_push_patch(d->out, d->patch_addr, (uint16_t)op1);
    }
    if (d->pending_kind == 1) {
        cheat_step_t *s = &d->out->steps[d->pending];
        s->value = op1 & width_mask(s->width);
        bool button = d->pending > d->first && d->out->steps[d->pending - 1].kind == STEP_IF &&
                      d->out->steps[d->pending - 1].cond == IF_BUTTON;
        if (!button) { /* a fill: value step, count, address step (in units of the width) */
            s->value_step = op2 >> 24;
            s->count = (op2 >> 16) & 0xFF;
            s->addr_step = (op2 & 0xFFFF) * s->width;
        }
        d->pending_kind = 0;
        return true;
    }
    if (op2 == GAME_ID_MARK) return true;
    if (op1 == 0) return decode_ar_special(d, op2);
    if (op1 == RESEED_MARK) return false;
    if (op1 >> 24 == 0xC4) return true; /* the hook (master code) */
    unsigned width = 1u << ((op1 >> 25) & 3);
    if (op1 & 0x38000000u) { /* a condition */
        static const if_kind_t conds[8] = {IF_AND, IF_EQ, IF_NE, IF_LT, IF_GT, IF_ULT, IF_UGT, IF_AND};
        unsigned action = op1 >> 30;
        if (action == 3) return false; /* "disable" codes */
        if_kind_t cond = width > 4 ? IF_NEVER : conds[(op1 >> 27) & 7];
        cheat_step_t s = if_step(cond, ar_addr(op1), op2 & width_mask(width > 4 ? 4 : width), width > 4 ? 4 : width,
                                 action == 0 ? 1 : action == 1 ? 2 : 0);
        if (action == 2) { /* a block up to "else" / "end if" */
            if (d->block != NONE) ar_end_block(d);
            if (!push(d, s)) return false;
            d->block = d->out->nsteps - 1;
            return true;
        }
        return push(d, s);
    }
    if ((op1 & 0x01000000u) && (op1 & 0xFE000000u) != 0xC6000000u) return false;
    cheat_step_t s = write_step(ar_addr(op1), 0, width);
    switch (op1 & 0xC0000000u) {
    case 0x00000000u: /* write; bytes and halfwords repeat by the value's upper bits */
        s.addr_step = width;
        if (width < 4) s.count = (op2 >> (width * 8)) + 1;
        break;
    case 0x40000000u: /* write through a pointer */
        s.kind = STEP_WRITE_INDIRECT;
        if (width < 4) s.addr_step = (op2 >> (width * 8)) * width;
        break;
    case 0x80000000u: s.kind = STEP_ADD; break;
    default: /* I/O */
        width = ((op1 >> 24) & 1) + 1;
        s.width = (uint8_t)width;
        s.addr = IO_BASE | (op1 & 0x00FFFFFFu);
        break;
    }
    s.value = op2 & width_mask(width);
    return push(d, s);
}

static uint16_t swap16(uint32_t v) {
    return (uint16_t)(((v >> 8) | (v << 8)) & 0xFFFF);
}

/* CodeBreaker, 32 + 16 bits. */
static bool decode_cb(decoder_t *d, uint32_t op1, uint16_t op2) {
    if (d->cb.master) cheat_cb_decrypt(&d->cb, &op1, &op2);
    if (d->pending_kind == 2) { /* a fill's counts */
        cheat_step_t *s = &d->out->steps[d->pending];
        s->count = op1 & 0xFFFF;
        s->addr_step = op2;
        s->value_step = op1 >> 16;
        d->pending_kind = 0;
        return true;
    }
    if (d->pending_kind == 3) { /* a fill list: up to three byte-swapped halfwords per line */
        uint32_t halves[3] = {op1 >> 16, op1 & 0xFFFF, op2};
        for (int i = 0; i < 3 && d->remaining; i++) {
            cheat_step_t *s = &d->out->steps[d->pending];
            s->value = swap16(halves[i]);
            if (--d->remaining) {
                if (!push(d, write_step(s->addr + 2, 0, 2))) return false;
                d->pending = d->out->nsteps - 1;
            }
        }
        if (!d->remaining) d->pending_kind = 0;
        return true;
    }
    uint32_t addr = op1 & 0x0FFFFFFFu;
    cheat_step_t s = write_step(addr, op2, 2);
    switch (op1 >> 28) {
    case 0x0: return true; /* game id */
    case 0x1: return true; /* the hook (master code) */
    case 0x2: s.kind = STEP_OR; break;
    case 0x3: s.width = 1; break;
    case 0x4: /* fill: the counts on the next line */
    case 0x5: /* fill list: op2 halfwords on the next lines */
        if (!push(d, s)) return false;
        d->pending = d->out->nsteps - 1;
        d->pending_kind = (op1 >> 28) == 0x4 ? 2 : 3;
        d->remaining = op2;
        if (d->pending_kind == 3 && !d->remaining) d->pending_kind = 0;
        return true;
    case 0x6: s.kind = STEP_AND; break;
    case 0x7: s = if_step(IF_EQ, addr, op2, 2, 1); break;
    case 0x8: break;
    case 0x9: cheat_cb_reseed(&d->cb, op1, op2); return true; /* the codes after it are scrambled */
    case 0xA: s = if_step(IF_NE, addr, op2, 2, 1); break;
    case 0xB: s = if_step(IF_GT, addr, op2, 2, 1); break;
    case 0xC: s = if_step(IF_LT, addr, op2, 2, 1); break;
    case 0xD:
        if (addr != 0x20) return false;
        s = if_step(IF_NAND, KEYINPUT_ADDR, op2, 2, 1); /* while these GBA buttons are held */
        break;
    case 0xE: s.kind = STEP_ADD; break;
    case 0xF: s = if_step(IF_AND, addr, op2, 2, 1); break;
    default: return false;
    }
    return push(d, s);
}

static bool decode_raw(decoder_t *d, const code_line_t *c) {
    if (c->a >= ROM_BASE && c->a < 0x0E000000u) { /* the ROM: patches, one halfword at a time */
        if (c->width == 1) return false;
        if (!cheat_push_patch(d->out, c->a, (uint16_t)(c->width == 4 ? c->b >> 16 : c->b))) return false;
        return c->width == 2 || cheat_push_patch(d->out, c->a + 2, (uint16_t)c->b);
    }
    return push(d, write_step(c->a, c->b, c->width));
}

/* ---- the whole text ---- */

/* How an 8 + 8 code reads best: GameShark scrambled or plain, Action Replay scrambled or plain. */
static gba_cheat_type_t detect_gsar(uint32_t op1, uint32_t op2, bool *scrambled) {
    uint32_t a = op1, b = op2;
    int best;
    gba_cheat_type_t type = GBA_CHEAT_GAMESHARK;
    cheat_tea_decrypt(&a, &b, CHEAT_GS_SEEDS);
    best = gs_score(a, b);
    *scrambled = true;
    a = op1;
    b = op2;
    cheat_tea_decrypt(&a, &b, CHEAT_AR3_SEEDS);
    int s = ar_score(a, b);
    if (s > best) {
        best = s;
        type = GBA_CHEAT_ACTION_REPLAY;
    }
    s = gs_score(op1, op2);
    if (s > best) {
        best = s;
        type = GBA_CHEAT_GAMESHARK;
        *scrambled = false;
    }
    s = ar_score(op1, op2);
    if (s > best) {
        type = GBA_CHEAT_ACTION_REPLAY;
        *scrambled = false;
    }
    return type;
}

int cheat_decode(const char *text, gba_cheat_type_t type, cheat_list_t *out, gba_cheat_type_t *used) {
    decoder_t d;
    memset(&d, 0, sizeof d);
    d.out = out;
    d.first = out->nsteps;
    d.pending = NONE;
    d.block = NONE;
    d.scrambled = true;
    bool decided = type != GBA_CHEAT_AUTO;
    int line = 0, codes = 0;
    const char *p = text ? text : "";
    while (*p) {
        const char *end = strchr(p, '\n');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        line++;
        size_t k = 0;
        while (k < n && isspace((unsigned char)p[k])) k++;
        if (k < n) { /* not blank */
            code_line_t c;
            if (!read_line(p, n, &c)) return line;
            if (!decided) {
                type = c.kind == LINE_RAW ? GBA_CHEAT_RAW
                       : c.kind == LINE_CB ? GBA_CHEAT_CODEBREAKER
                                           : detect_gsar(c.a, c.b, &d.scrambled);
                decided = true;
            }
            bool ok;
            switch (type) {
            case GBA_CHEAT_RAW: ok = c.kind == LINE_RAW && decode_raw(&d, &c); break;
            case GBA_CHEAT_CODEBREAKER: ok = c.kind == LINE_CB && decode_cb(&d, c.a, (uint16_t)c.b); break;
            case GBA_CHEAT_GAMESHARK:
            case GBA_CHEAT_ACTION_REPLAY: {
                if (c.kind != LINE_GSAR) {
                    ok = false;
                    break;
                }
                uint32_t a = c.a, b = c.b;
                bool gs = type == GBA_CHEAT_GAMESHARK;
                if (d.scrambled) cheat_tea_decrypt(&a, &b, gs ? CHEAT_GS_SEEDS : CHEAT_AR3_SEEDS);
                ok = gs ? decode_gs(&d, a, b) : decode_ar(&d, a, b);
                break;
            }
            default: ok = false; break;
            }
            if (!ok) return line;
            codes++;
        }
        if (!end) break;
        p = end + 1;
    }
    if (!codes) return line ? line : 1; /* nothing to read */
    if (d.block != NONE) ar_end_block(&d); /* no "end if": the block guards the rest of this cheat */
    for (size_t i = d.first; i < out->nsteps; i++) { /* a condition never reaches into the next cheat */
        cheat_step_t *s = &out->steps[i];
        uint32_t left = (uint32_t)(out->nsteps - i - 1);
        if (s->kind != STEP_IF) continue;
        if (s->then_n > left) s->then_n = left;
        if (s->else_n > left - s->then_n) s->else_n = left - s->then_n;
    }
    if (used) *used = type;
    return 0;
}

gba_cheat_type_t gba_cheat_check(const char *text, gba_cheat_type_t type, int *bad_line) {
    cheat_list_t l;
    memset(&l, 0, sizeof l);
    gba_cheat_type_t used = type;
    int bad = cheat_decode(text, type, &l, &used);
    cheat_list_free(&l);
    if (bad_line) *bad_line = bad;
    return used;
}
