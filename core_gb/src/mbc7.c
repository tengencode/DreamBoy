/* The tilt cartridge chip, MBC7 (17c part 2), from Pan Docs' MBC7 page: behind two switches, 0xA000-0xAFFF holds
 * registers picked by bits 4-7 of the address — a two-axis tilt sensor that is read by "erase, then latch", and the
 * pins of the save chip, a 93LC56 serial EEPROM (128 words of 16 bits). The EEPROM's bytes are the cartridge RAM
 * (g->cart_ram, 256 bytes: word n is bytes 2n, low, and 2n + 1), so the save file, the dirty flag and the state's
 * CRAM chunk carry them like any other cartridge's. */
#include <string.h>

#include "gb_internal.h"

#define LEVEL 0x81D0 /* the reading with the cartridge lying flat */
#define ONE_G 0x70   /* what 1 g along an axis moves it by ("roughly", says Pan Docs) */
#define ERASED 0x8000

enum { P_IDLE, P_COMMAND, P_DATA_IN, P_DATA_OUT, P_DONE }; /* gb_mbc7_t.phase */

void gb_mbc7_reset(gb_t *g) {
    memset(&g->mbc7, 0, sizeof g->mbc7);
    g->mbc7.x = g->mbc7.y = ERASED;
    g->mbc7.dout = 1; /* ready */
}

bool gb_mbc7_values_ok(const gb_mbc7_t *m) { /* a state from a file: what indexes or shifts is in range */
    return m->phase <= P_DONE && m->count < 16 && m->address < 128 && (m->cs | m->clk | m->di | m->dout) <= 1;
}

static uint16_t word(const gb_t *g, unsigned n) { /* (n & 0x7F: never outside the 256 bytes, whatever a state said) */
    n &= 0x7F;
    return (uint16_t)(g->cart_ram[2 * n] | g->cart_ram[2 * n + 1] << 8);
}

static void set_word(gb_t *g, unsigned n, uint16_t v) {
    n &= 0x7F;
    g->cart_ram[2 * n] = (uint8_t)v;
    g->cart_ram[2 * n + 1] = (uint8_t)(v >> 8);
    g->cart.dirty = 1;
}

/* A rising clock edge with the chip selected: one bit in, and (while reading) the next bit out. */
static void eeprom_clock(gb_t *g, unsigned bit) {
    gb_mbc7_t *m = &g->mbc7;
    switch (m->phase) {
    case P_IDLE: /* zeros until the start bit */
        if (bit) m->phase = P_COMMAND, m->count = 0, m->shift = 0;
        break;
    case P_COMMAND: /* 2 opcode bits, then 8 address bits: the low 7 are the word */
        m->shift = (uint16_t)(m->shift << 1 | bit);
        if (++m->count < 10) break;
        m->address = m->shift & 0x7F;
        m->count = 0;
        m->all = 0;
        m->phase = P_DONE;
        m->dout = 1;
        switch (m->shift >> 6) { /* the opcode, and for opcode 00 the address's top two bits */
        case 0x0: m->write_on = 0; break;
        case 0x1: m->all = 1, m->phase = P_DATA_IN; break; /* write-all */
        case 0x2: /* erase-all */
            if (m->write_on)
                for (unsigned n = 0; n < 128; n++) set_word(g, n, 0xFFFF);
            break;
        case 0x3: m->write_on = 1; break;
        case 0x4: case 0x5: case 0x6: case 0x7: m->phase = P_DATA_IN; break; /* write */
        case 0x8: case 0x9: case 0xA: case 0xB: /* read: a 0 first, then the word */
            m->phase = P_DATA_OUT;
            m->dout = 0;
            break;
        default: /* erase */
            if (m->write_on) set_word(g, m->address, 0xFFFF);
            break;
        }
        m->shift = 0;
        break;
    case P_DATA_IN:
        m->shift = (uint16_t)(m->shift << 1 | bit);
        if (++m->count < 16) break;
        if (m->write_on) {
            if (m->all)
                for (unsigned n = 0; n < 128; n++) set_word(g, n, m->shift);
            else set_word(g, m->address, m->shift);
        }
        m->phase = P_DONE;
        m->dout = 1; /* done at once: ready */
        break;
    case P_DATA_OUT: /* high bit first, and on into the next word */
        m->dout = (word(g, m->address) >> (15 - m->count)) & 1;
        if (++m->count == 16) m->count = 0, m->address = (m->address + 1) & 0x7F;
        break;
    default: break; /* P_DONE: until chip select falls */
    }
}

uint8_t gb_mbc7_peek(const gb_t *g, uint16_t addr) {
    const gb_mbc7_t *m = &g->mbc7;
    switch (addr >> 4 & 0xF) {
    case 0x2: return (uint8_t)m->x;
    case 0x3: return (uint8_t)(m->x >> 8);
    case 0x4: return (uint8_t)m->y;
    case 0x5: return (uint8_t)(m->y >> 8);
    case 0x6: return 0x00;
    case 0x8: return (uint8_t)(m->cs << 7 | m->clk << 6 | m->di << 1 | m->dout);
    default: return 0xFF;
    }
}

static bool open(const gb_t *g, uint16_t addr) {
    return addr < 0xB000 && g->cart.ram_on && g->mbc7.ram_on2;
}

uint8_t gb_mbc7_read(const gb_t *g, uint16_t addr) {
    return open(g, addr) ? gb_mbc7_peek(g, addr) : 0xFF;
}

void gb_mbc7_write(gb_t *g, uint16_t addr, uint8_t value) {
    gb_mbc7_t *m = &g->mbc7;
    if (!open(g, addr)) return;
    switch (addr >> 4 & 0xF) {
    case 0x0:
        if (value != 0x55) break;
        m->x = m->y = ERASED;
        m->latch_step = 1;
        break;
    case 0x1: /* a reading is taken only after an erase */
        if (value != 0xAA || !m->latch_step) break;
        m->latch_step = 0;
        /* right edge down: X goes down; bottom edge down: Y goes down (Pan Docs: "lower values are towards the
         * right" / "the bottom"). Proven only with our own test ROM so far, not with a real tilt game. */
        m->x = (uint16_t)(LEVEL - g->tilt_x * ONE_G / 32767);
        m->y = (uint16_t)(LEVEL - g->tilt_y * ONE_G / 32767);
        break;
    case 0x8: {
        uint8_t cs = value >> 7, clk = value >> 6 & 1;
        m->di = value >> 1 & 1;
        if (!cs) m->phase = P_IDLE, m->count = 0, m->dout = 1; /* a command ends, finished or not */
        else if (clk && !m->clk) eeprom_clock(g, m->di);
        m->cs = cs;
        m->clk = clk;
        break;
    }
    default: break;
    }
}

int gb_has_tilt(const gb_t *g) {
    return g->info.mbc == MBC7;
}

void gb_set_tilt(gb_t *g, int16_t x, int16_t y) {
    g->tilt_x = x < -32767 ? -32767 : x;
    g->tilt_y = y < -32767 ? -32767 : y;
}
