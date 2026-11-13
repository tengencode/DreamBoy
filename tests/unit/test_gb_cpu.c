/* The SM83 CPU: loads, jumps, timing. Programs start at 0x150 after "NOP; JP 0x150" (2 steps). */
#include <stdlib.h>
#include <string.h>

#include "gbtestutil.h"
#include "test.h"

#define BOOT(...)                                    \
    static const uint8_t code_[] = {__VA_ARGS__};    \
    gb_t *g = gbtu_boot(code_, sizeof code_);        \
    gb_debug_step(g);                                \
    gb_debug_step(g)

static uint64_t step_ticks(gb_t *g) {
    uint64_t before = gb_debug_ticks(g);
    gb_debug_step(g);
    return gb_debug_ticks(g) - before;
}

TEST(gb_cpu_boot_registers) {
    BOOT(0x00);
    CHECK_EQ(gb_debug_reg(g, GB_REG_AF), 0x01B0);
    CHECK_EQ(gb_debug_reg(g, GB_REG_BC), 0x0013);
    CHECK_EQ(gb_debug_reg(g, GB_REG_DE), 0x00D8);
    CHECK_EQ(gb_debug_reg(g, GB_REG_HL), 0x014D);
    CHECK_EQ(gb_debug_reg(g, GB_REG_SP), 0xFFFE);
    CHECK_EQ(gb_debug_reg(g, GB_REG_PC), 0x0150);
    CHECK_EQ(gb_debug_ticks(g), 4 + 16);
    gb_destroy(g);
    uint8_t *rom = gbtu_rom(0x8000, 0, 0, 0x80);
    g = gb_create();
    gb_load_rom(g, rom, 0x8000);
    free(rom);
    CHECK_EQ(gb_debug_reg(g, GB_REG_AF), 0x1180);
    CHECK_EQ(gb_debug_reg(g, GB_REG_BC), 0x0000);
    CHECK_EQ(gb_debug_reg(g, GB_REG_DE), 0xFF56);
    CHECK_EQ(gb_debug_reg(g, GB_REG_HL), 0x000D);
    CHECK_EQ(gb_debug_reg(g, GB_REG_PC), 0x0100);
    gb_destroy(g);
}

TEST(gb_cpu_ld_8bit) {
    BOOT(0x06, 0x12,       /* LD B,0x12 */
         0x48,             /* LD C,B */
         0x21, 0x00, 0xC0, /* LD HL,0xC000 */
         0x70,             /* LD (HL),B */
         0x7E,             /* LD A,(HL) */
         0x36, 0x99,       /* LD (HL),0x99 */
         0x56,             /* LD D,(HL) */
         0x3E, 0x77,       /* LD A,0x77 */
         0xE0, 0x80,       /* LDH (0x80),A */
         0xF0, 0x80,       /* LDH A,(0x80) */
         0x0E, 0x81,       /* LD C,0x81 */
         0xE2,             /* LD (FF00+C),A */
         0xEA, 0x10, 0xC0, /* LD (0xC010),A */
         0xFA, 0x10, 0xC0, /* LD A,(0xC010) */
         0x22,             /* LD (HL+),A */
         0x32,             /* LD (HL-),A */
         0x2A,             /* LD A,(HL+) */
         0x01, 0x05, 0xC0, /* LD BC,0xC005 */
         0x02,             /* LD (BC),A */
         0x1A);            /* LD A,(DE) */
    for (int i = 0; i < 4; i++) gb_debug_step(g);
    CHECK_EQ(gb_debug_reg(g, GB_REG_BC), 0x1212);
    CHECK_EQ(gb_debug_peek(g, 0xC000), 0x12);
    gb_debug_step(g);
    CHECK_EQ(gb_debug_reg(g, GB_REG_AF) >> 8, 0x12);
    gb_debug_step(g);
    gb_debug_step(g);
    CHECK_EQ(gb_debug_reg(g, GB_REG_DE) >> 8, 0x99);
    for (int i = 0; i < 5; i++) gb_debug_step(g);
    CHECK_EQ(gb_debug_peek(g, 0xFF80), 0x77);
    CHECK_EQ(gb_debug_peek(g, 0xFF81), 0x77);
    gb_debug_step(g);
    gb_debug_step(g);
    CHECK_EQ(gb_debug_peek(g, 0xC010), 0x77);
    gb_debug_step(g); /* LD (HL+),A */
    CHECK_EQ(gb_debug_peek(g, 0xC000), 0x77);
    CHECK_EQ(gb_debug_reg(g, GB_REG_HL), 0xC001);
    gb_debug_step(g); /* LD (HL-),A */
    CHECK_EQ(gb_debug_peek(g, 0xC001), 0x77);
    CHECK_EQ(gb_debug_reg(g, GB_REG_HL), 0xC000);
    gb_debug_step(g);
    CHECK_EQ(gb_debug_reg(g, GB_REG_HL), 0xC001);
    gb_debug_step(g);
    gb_debug_step(g);
    CHECK_EQ(gb_debug_peek(g, 0xC005), 0x77);
    gb_destroy(g);
}

TEST(gb_cpu_ld_16bit_stack) {
    BOOT(0x31, 0x00, 0xD0, /* LD SP,0xD000 */
         0x01, 0x34, 0x12, /* LD BC,0x1234 */
         0xC5,             /* PUSH BC */
         0xD1,             /* POP DE */
         0x08, 0x20, 0xC0, /* LD (0xC020),SP */
         0x21, 0xFF, 0x12, /* LD HL,0x12FF */
         0xE5,             /* PUSH HL */
         0xF1,             /* POP AF: F's low nibble is always 0 */
         0xF5,             /* PUSH AF */
         0xF9);            /* LD SP,HL */
    gb_debug_step(g);
    gb_debug_step(g);
    CHECK_EQ(step_ticks(g), 16);
    CHECK_EQ(gb_debug_reg(g, GB_REG_SP), 0xCFFE);
    CHECK_EQ(gb_debug_peek(g, 0xCFFF), 0x12);
    CHECK_EQ(gb_debug_peek(g, 0xCFFE), 0x34);
    CHECK_EQ(step_ticks(g), 12);
    CHECK_EQ(gb_debug_reg(g, GB_REG_DE), 0x1234);
    CHECK_EQ(step_ticks(g), 20);
    CHECK_EQ(gb_debug_peek(g, 0xC020), 0x00);
    CHECK_EQ(gb_debug_peek(g, 0xC021), 0xD0);
    gb_debug_step(g);
    gb_debug_step(g);
    gb_debug_step(g);
    CHECK_EQ(gb_debug_reg(g, GB_REG_AF), 0x12F0);
    gb_debug_step(g);
    CHECK_EQ(gb_debug_peek(g, 0xCFFE), 0xF0);
    CHECK_EQ(step_ticks(g), 8);
    CHECK_EQ(gb_debug_reg(g, GB_REG_SP), 0x12FF);
    gb_destroy(g);
}

TEST(gb_cpu_jumps) {
    {
        BOOT(0x18, 0x02, 0x00, 0x00, /* JR +2 */
             0xAF,                   /* XOR A: Z set */
             0x20, 0x10,             /* JR NZ: not taken */
             0x28, 0x01, 0x00,       /* JR Z,+1: taken */
             0xC3, 0x60, 0x01);      /* JP 0x160 */
        CHECK_EQ(step_ticks(g), 12);
        CHECK_EQ(gb_debug_reg(g, GB_REG_PC), 0x154);
        gb_debug_step(g);
        CHECK_EQ(step_ticks(g), 8);
        CHECK_EQ(gb_debug_reg(g, GB_REG_PC), 0x157);
        CHECK_EQ(step_ticks(g), 12);
        CHECK_EQ(gb_debug_reg(g, GB_REG_PC), 0x15A);
        CHECK_EQ(step_ticks(g), 16);
        CHECK_EQ(gb_debug_reg(g, GB_REG_PC), 0x160);
        gb_destroy(g);
    }
    {
        BOOT(0xAF,             /* 150 XOR A: Z set, C clear */
             0xC2, 0x00, 0x02, /* 151 JP NZ: not taken */
             0xCC, 0x60, 0x01, /* 154 CALL Z,0x160: taken */
             0xDC, 0x00, 0x02, /* 157 CALL C: not taken */
             0xD7,             /* 15A RST 0x10 */
             0, 0, 0, 0, 0,    /* 15B */
             0xC0,             /* 160 RET NZ: not taken */
             0xC8);            /* 161 RET Z: taken */
        gb_debug_step(g);
        CHECK_EQ(step_ticks(g), 12);
        CHECK_EQ(step_ticks(g), 24);
        CHECK_EQ(gb_debug_reg(g, GB_REG_PC), 0x160);
        CHECK_EQ(gb_debug_reg(g, GB_REG_SP), 0xFFFC);
        CHECK_EQ(gb_debug_peek(g, 0xFFFC), 0x57);
        CHECK_EQ(gb_debug_peek(g, 0xFFFD), 0x01);
        CHECK_EQ(step_ticks(g), 8);
        CHECK_EQ(step_ticks(g), 20);
        CHECK_EQ(gb_debug_reg(g, GB_REG_PC), 0x157);
        CHECK_EQ(step_ticks(g), 12);
        CHECK_EQ(step_ticks(g), 16);
        CHECK_EQ(gb_debug_reg(g, GB_REG_PC), 0x0010);
        gb_destroy(g);
    }
    {
        BOOT(0x21, 0x56, 0x01, /* LD HL,0x156 */
             0xE9,             /* JP HL */
             0x00, 0x00,
             0xCD, 0x5A, 0x01, /* 156 CALL 0x15A */
             0x00,
             0xC9);            /* 15A RET */
        gb_debug_step(g);
        CHECK_EQ(step_ticks(g), 4);
        CHECK_EQ(gb_debug_reg(g, GB_REG_PC), 0x156);
        CHECK_EQ(step_ticks(g), 24);
        CHECK_EQ(step_ticks(g), 16);
        CHECK_EQ(gb_debug_reg(g, GB_REG_PC), 0x159);
        gb_destroy(g);
    }
}

/* Ticks of every opcode that takes one fixed time (conditional ones are in gb_cpu_jumps; 0 = not checked here). */
TEST(gb_cpu_ticks_table) {
    static const uint8_t ticks[256] = {
        4,  12, 8,  8,  4,  4,  8,  4,  20, 8,  8,  8, 4,  4,  8, 4,  /* 0x */
        4,  12, 8,  8,  4,  4,  8,  4,  12, 8,  8,  8, 4,  4,  8, 4,  /* 1x */
        0,  12, 8,  8,  4,  4,  8,  4,  0,  8,  8,  8, 4,  4,  8, 4,  /* 2x */
        0,  12, 8,  8,  12, 12, 12, 4,  0,  8,  8,  8, 4,  4,  8, 4,  /* 3x */
        4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4, 4,  4,  8, 4,  /* 4x */
        4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4, 4,  4,  8, 4,  /* 5x */
        4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4, 4,  4,  8, 4,  /* 6x */
        8,  8,  8,  8,  8,  8,  4,  8,  4,  4,  4,  4, 4,  4,  8, 4,  /* 7x */
        4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4, 4,  4,  8, 4,  /* 8x */
        4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4, 4,  4,  8, 4,  /* 9x */
        4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4, 4,  4,  8, 4,  /* Ax */
        4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4, 4,  4,  8, 4,  /* Bx */
        0,  12, 0,  16, 0,  16, 8,  16, 0,  16, 0,  0, 0,  24, 8, 16, /* Cx */
        0,  12, 0,  0,  0,  16, 8,  16, 0,  16, 0,  0, 0,  0,  8, 16, /* Dx */
        12, 12, 8,  0,  0,  16, 8,  16, 16, 4,  16, 0, 0,  0,  8, 16, /* Ex */
        12, 12, 8,  4,  0,  16, 8,  16, 12, 8,  16, 4, 0,  0,  8, 16, /* Fx */
    };
    for (int op = 0; op < 256; op++) {
        if (!ticks[op]) continue;
        uint8_t code[3] = {(uint8_t)op, 0x00, 0xC0};
        gb_t *g = gbtu_boot(code, sizeof code);
        gb_debug_step(g);
        gb_debug_step(g);
        uint64_t t = step_ticks(g);
        if (t != ticks[op]) fprintf(stderr, "  opcode %02X: %d ticks\n", op, (int)t);
        CHECK_EQ(t, ticks[op]);
        gb_destroy(g);
    }
    for (int op = 0; op < 256; op++) { /* the CB page */
        uint8_t code[2] = {0xCB, (uint8_t)op};
        gb_t *g = gbtu_boot(code, sizeof code);
        gb_debug_step(g);
        gb_debug_step(g);
        g->cpu.h = 0xC0;
        uint64_t want = (op & 7) != 6 ? 8 : (op >> 6) == 1 ? 12 : 16;
        CHECK_EQ(step_ticks(g), want);
        gb_destroy(g);
    }
}

TEST(gb_cpu_illegal_opcode_hangs_safely) {
    static const uint8_t illegal[] = {0xD3, 0xDB, 0xDD, 0xE3, 0xE4, 0xEB, 0xEC, 0xED, 0xF4, 0xFC, 0xFD};
    for (size_t i = 0; i < sizeof illegal; i++) {
        gb_t *g = gbtu_boot(&illegal[i], 1);
        gb_debug_step(g);
        gb_debug_step(g);
        gb_debug_step(g);
        uint16_t pc = gb_debug_reg(g, GB_REG_PC);
        uint64_t before = gb_debug_ticks(g);
        gb_run_frame(g);
        gb_run_frame(g);
        CHECK_EQ(gb_debug_reg(g, GB_REG_PC), pc);
        CHECK(gb_debug_ticks(g) - before >= 2 * GB_FRAME_TICKS - 24);
        gb_destroy(g);
    }
}

/* Review focus: a game that stops the CPU for good must not stop gb_run_frame from returning. */
TEST(gb_run_frame_always_returns) {
    static const uint8_t programs[][3] = {{0xF3, 0x76, 0x00}, {0xF3, 0x10, 0x00}, {0x18, 0xFE, 0x00}};
    for (int i = 0; i < 3; i++) {
        gb_t *g = gbtu_boot(programs[i], 3);
        for (int f = 0; f < 3; f++) gb_run_frame(g);
        uint64_t t = gb_debug_ticks(g);
        CHECK(t >= 3 * GB_FRAME_TICKS && t < 3 * GB_FRAME_TICKS + 24);
        gb_destroy(g);
    }
}

TEST(gb_run_frame_is_exact_over_time) {
    BOOT(0xCD, 0x50, 0x01); /* CALL 0x150 for ever (24 ticks each): frames do not drift */
    uint64_t start = gb_debug_ticks(g);
    for (int f = 0; f < 100; f++) gb_run_frame(g);
    uint64_t t = gb_debug_ticks(g) - start;
    CHECK(t >= 100u * GB_FRAME_TICKS - 24 && t < 100u * GB_FRAME_TICKS + 24);
    gb_destroy(g);
}

TEST(gb_cpu_breakpoint_mark) {
    BOOT(0x00, 0x40);
    gb_debug_step(g);
    CHECK_EQ(gb_debug_breakpoint(g), 0);
    gb_debug_step(g);
    CHECK_EQ(gb_debug_breakpoint(g), 1);
    gb_destroy(g);
}
