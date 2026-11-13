/* The SM83 CPU's arithmetic and flags, against small models written here. */
#include "gbtestutil.h"
#include "test.h"

/* Runs `op` (with an optional operand byte) with A, F and B set; returns the machine after it. */
static gb_t *run1(uint8_t op, uint8_t operand, uint8_t a, uint8_t f, uint8_t b) {
    uint8_t code[2] = {op, operand};
    gb_t *g = gbtu_boot(code, 2);
    gb_debug_step(g);
    gb_debug_step(g);
    g->cpu.a = a;
    g->cpu.f = f;
    g->cpu.b = b;
    gb_debug_step(g);
    return g;
}

static const uint8_t VALUES[] = {0x00, 0x01, 0x0F, 0x10, 0x7F, 0x80, 0x99, 0xF0, 0xFF};
#define NV (sizeof VALUES)

TEST(gb_cpu_alu_flags) {
    for (int op = 0; op < 8; op++) {
        for (size_t i = 0; i < NV; i++) {
            for (size_t j = 0; j < NV; j++) {
                for (int cin = 0; cin < 2; cin++) {
                    int a = VALUES[i], v = VALUES[j], r, h, c, n = 0;
                    switch (op) {
                    case 0: r = a + v; h = (a & 15) + (v & 15) > 15; c = r > 255; break;
                    case 1: r = a + v + cin; h = (a & 15) + (v & 15) + cin > 15; c = r > 255; break;
                    case 2:
                    case 7: r = a - v; h = (a & 15) < (v & 15); c = a < v; n = 1; break;
                    case 3: r = a - v - cin; h = (a & 15) < (v & 15) + cin; c = a < v + cin; n = 1; break;
                    case 4: r = a & v; h = 1; c = 0; break;
                    case 5: r = a ^ v; h = 0; c = 0; break;
                    default: r = a | v; h = 0; c = 0; break;
                    }
                    uint8_t want_f = (uint8_t)(((r & 255) ? 0 : GB_FZ) | (n ? GB_FN : 0) | (h ? GB_FH : 0) | (c ? GB_FC : 0));
                    uint8_t want_a = op == 7 ? (uint8_t)a : (uint8_t)r;
                    /* register form (B) and immediate form */
                    gb_t *g = run1((uint8_t)(0x80 | op << 3), 0, (uint8_t)a, cin ? GB_FC : 0, (uint8_t)v);
                    CHECK_EQ(g->cpu.a, want_a);
                    CHECK_EQ(g->cpu.f, want_f);
                    gb_destroy(g);
                    g = run1((uint8_t)(0xC6 | op << 3), (uint8_t)v, (uint8_t)a, cin ? GB_FC : 0, 0);
                    CHECK_EQ(g->cpu.a, want_a);
                    CHECK_EQ(g->cpu.f, want_f);
                    gb_destroy(g);
                }
            }
        }
    }
}

TEST(gb_cpu_inc_dec) {
    for (size_t i = 0; i < NV; i++) {
        for (int cin = 0; cin < 2; cin++) {
            uint8_t v = VALUES[i], f = cin ? GB_FC : 0;
            gb_t *g = run1(0x04, 0, 0, f, v); /* INC B */
            uint8_t r = (uint8_t)(v + 1);
            CHECK_EQ(g->cpu.b, r);
            CHECK_EQ(g->cpu.f, (uint8_t)(f | (r ? 0 : GB_FZ) | ((r & 15) ? 0 : GB_FH)));
            gb_destroy(g);
            g = run1(0x05, 0, 0, f, v); /* DEC B */
            r = (uint8_t)(v - 1);
            CHECK_EQ(g->cpu.b, r);
            CHECK_EQ(g->cpu.f, (uint8_t)(f | GB_FN | (r ? 0 : GB_FZ) | ((r & 15) == 15 ? GB_FH : 0)));
            gb_destroy(g);
        }
    }
    gb_t *g = run1(0x03, 0, 0, 0xF0, 0xFF); /* INC BC: no flags; C was 0x13 */
    CHECK_EQ(gb_debug_reg(g, GB_REG_BC), 0xFF14);
    CHECK_EQ(g->cpu.f, 0xF0);
    gb_destroy(g);
    g = run1(0x34, 0, 0, 0, 0); /* INC (HL) on RAM */
    gb_destroy(g);
}

TEST(gb_cpu_16bit_math) {
    gb_t *g = run1(0x09, 0, 0, GB_FZ | GB_FN, 0x0F); /* ADD HL,BC: HL=0x014D, BC=0x0F13 */
    CHECK_EQ(gb_debug_reg(g, GB_REG_HL), 0x1060);
    CHECK_EQ(g->cpu.f, GB_FZ | GB_FH);
    gb_destroy(g);
    g = run1(0x29, 0, 0, 0, 0); /* ADD HL,HL */
    CHECK_EQ(gb_debug_reg(g, GB_REG_HL), 0x029A);
    CHECK_EQ(g->cpu.f, 0);
    gb_destroy(g);
    static const struct {
        uint16_t sp;
        uint8_t e;
        uint16_t want;
        uint8_t f;
    } cases[] = {{0xFFFE, 0x02, 0x0000, GB_FH | GB_FC}, {0x000F, 0x01, 0x0010, GB_FH},
                 {0x0100, 0xFF, 0x00FF, 0},             {0x00FF, 0xFF, 0x00FE, GB_FH | GB_FC},
                 {0x00F0, 0x10, 0x0100, GB_FC}};
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        for (int form = 0; form < 2; form++) {
            uint8_t code[2] = {form ? 0xF8 : 0xE8, cases[i].e}; /* LD HL,SP+e / ADD SP,e */
            g = gbtu_boot(code, 2);
            gb_debug_step(g);
            gb_debug_step(g);
            g->cpu.sp = cases[i].sp;
            g->cpu.f = 0xF0;
            gb_debug_step(g);
            CHECK_EQ(gb_debug_reg(g, form ? GB_REG_HL : GB_REG_SP), cases[i].want);
            CHECK_EQ(g->cpu.f, cases[i].f);
            gb_destroy(g);
        }
    }
}

/* DAA after every BCD add and subtract of two valid digits pairs gives the right BCD result. */
TEST(gb_cpu_daa) {
    for (int x = 0; x < 100; x += 7) {
        for (int y = 0; y < 100; y += 3) {
            for (int sub = 0; sub < 2; sub++) {
                uint8_t bx = (uint8_t)(x / 10 << 4 | x % 10), by = (uint8_t)(y / 10 << 4 | y % 10);
                uint8_t code[3] = {(uint8_t)(sub ? 0x90 : 0x80), 0x27, 0}; /* ADD/SUB A,B; DAA */
                gb_t *g = gbtu_boot(code, 3);
                gb_debug_step(g);
                gb_debug_step(g);
                g->cpu.a = bx;
                g->cpu.b = by;
                gb_debug_step(g);
                gb_debug_step(g);
                int r = sub ? (x - y + 100) % 100 : (x + y) % 100;
                int carry = sub ? x < y : x + y > 99;
                CHECK_EQ(g->cpu.a, (uint8_t)(r / 10 << 4 | r % 10));
                CHECK_EQ(g->cpu.f, (uint8_t)((r ? 0 : GB_FZ) | (sub ? GB_FN : 0) | (carry ? GB_FC : 0)));
                gb_destroy(g);
            }
        }
    }
}

TEST(gb_cpu_misc_flags) {
    gb_t *g = run1(0x2F, 0, 0x35, GB_FZ | GB_FC, 0); /* CPL */
    CHECK_EQ(g->cpu.a, 0xCA);
    CHECK_EQ(g->cpu.f, GB_FZ | GB_FN | GB_FH | GB_FC);
    gb_destroy(g);
    g = run1(0x37, 0, 0, GB_FZ | GB_FN | GB_FH, 0); /* SCF */
    CHECK_EQ(g->cpu.f, GB_FZ | GB_FC);
    gb_destroy(g);
    g = run1(0x3F, 0, 0, GB_FZ | GB_FN | GB_FH | GB_FC, 0); /* CCF */
    CHECK_EQ(g->cpu.f, GB_FZ);
    gb_destroy(g);
    g = run1(0x3F, 0, 0, 0, 0);
    CHECK_EQ(g->cpu.f, GB_FC);
    gb_destroy(g);
}

/* The model of the eight rotates and shifts. */
static uint8_t model_rot(int kind, uint8_t v, int cin, int *cout) {
    switch (kind) {
    case 0: *cout = v >> 7; return (uint8_t)(v << 1 | v >> 7);
    case 1: *cout = v & 1; return (uint8_t)(v >> 1 | v << 7);
    case 2: *cout = v >> 7; return (uint8_t)(v << 1 | cin);
    case 3: *cout = v & 1; return (uint8_t)(v >> 1 | cin << 7);
    case 4: *cout = v >> 7; return (uint8_t)(v << 1);
    case 5: *cout = v & 1; return (uint8_t)((v >> 1) | (v & 0x80));
    case 6: *cout = 0; return (uint8_t)((v & 15) << 4 | v >> 4);
    default: *cout = v & 1; return v >> 1;
    }
}

TEST(gb_cpu_rot_shift_bits) {
    static const uint8_t in[] = {0x00, 0x81, 0x5A};
    for (int op = 0; op < 256; op++) {
        for (int k = 0; k < 3; k++) {
            for (int cin = 0; cin < 2; cin++) {
                int z = op & 7, y = (op >> 3) & 7, x = op >> 6, cout;
                uint8_t code[2] = {0xCB, (uint8_t)op};
                gb_t *g = gbtu_boot(code, 2);
                gb_debug_step(g);
                gb_debug_step(g);
                gb_cpu_t *c = &g->cpu;
                uint8_t *regs[8] = {&c->b, &c->c, &c->d, &c->e, &c->h, &c->l, NULL, &c->a};
                c->h = 0xC0, c->l = 0x00; /* (HL) is RAM */
                uint8_t v = in[k];
                if (z == 6) g->mem.wram[0] = v;
                else *regs[z] = v;
                v = z == 6 ? g->mem.wram[0] : *regs[z]; /* H and L hold the address when they are the operand */
                c->f = cin ? GB_FC : 0;
                gb_debug_step(g);
                uint8_t got = z == 6 ? g->mem.wram[0] : *regs[z], want = v, want_f;
                if (x == 0) {
                    want = model_rot(y, v, cin, &cout);
                    want_f = (uint8_t)((want ? 0 : GB_FZ) | (cout ? GB_FC : 0));
                } else if (x == 1) {
                    want_f = (uint8_t)((cin ? GB_FC : 0) | GB_FH | ((v >> y) & 1 ? 0 : GB_FZ));
                } else {
                    want = x == 2 ? (uint8_t)(v & ~(1 << y)) : (uint8_t)(v | 1 << y);
                    want_f = cin ? GB_FC : 0;
                }
                CHECK_EQ(got, want);
                CHECK_EQ(c->f, want_f);
                gb_destroy(g);
            }
        }
    }
    for (int y = 0; y < 4; y++) { /* RLCA RRCA RLA RRA: Z is always clear */
        int cout;
        gb_t *g = run1((uint8_t)(0x07 | y << 3), 0, 0x00, GB_FZ | GB_FN | GB_FH, 0);
        CHECK_EQ(g->cpu.a, 0);
        CHECK_EQ(g->cpu.f, 0);
        gb_destroy(g);
        g = run1((uint8_t)(0x07 | y << 3), 0, 0x81, GB_FC, 0);
        CHECK_EQ(g->cpu.a, model_rot(y, 0x81, 1, &cout));
        CHECK_EQ(g->cpu.f, cout ? GB_FC : 0);
        gb_destroy(g);
    }
}
