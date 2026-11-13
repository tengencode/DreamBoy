/* The memory map and the passing of time. */
#include "gb_internal.h"

void gb_tick(gb_t *g) {
    gb_timer_tick(g);
    g->ticks += 4;
    g->frame_left -= 4;
}

uint8_t gb_bus_peek(const gb_t *g, uint16_t addr) {
    if (addr < 0x8000) return gb_cart_read(g, addr);
    if (addr < 0xA000) return 0xFF;
    if (addr < 0xC000) return gb_cart_read(g, addr);
    if (addr < 0xFE00) { /* work RAM and its echo */
        uint16_t a = addr & 0x1FFF;
        return g->mem.wram[a < 0x1000 ? a : g->mem.wram_bank * 0x1000u + (a & 0xFFF)];
    }
    if (addr < 0xFF00) return 0xFF;
    if (addr == 0xFF00) return gb_joypad_read(g);
    if (addr <= 0xFF02) return gb_serial_read(g, addr);
    if (addr >= 0xFF04 && addr <= 0xFF07) return gb_timer_read(g, addr);
    if (addr == 0xFF0F) return g->mem.iflag | 0xE0;
    if (addr == 0xFFFF) return g->mem.ie;
    if (addr >= 0xFF80) return g->mem.hram[addr - 0xFF80];
    return 0xFF;
}

uint8_t gb_bus_read(gb_t *g, uint16_t addr) {
    gb_tick(g);
    return gb_bus_peek(g, addr);
}

void gb_bus_write(gb_t *g, uint16_t addr, uint8_t value) {
    gb_tick(g);
    if (addr < 0x8000 || (addr >= 0xA000 && addr < 0xC000)) {
        gb_cart_write(g, addr, value);
    } else if (addr < 0xA000) {
    } else if (addr < 0xFE00) {
        uint16_t a = addr & 0x1FFF;
        g->mem.wram[a < 0x1000 ? a : g->mem.wram_bank * 0x1000u + (a & 0xFFF)] = value;
    } else if (addr < 0xFF00) {
    } else if (addr == 0xFF00) {
        gb_joypad_write(g, value);
    } else if (addr <= 0xFF02) {
        gb_serial_write(g, addr, value);
    } else if (addr >= 0xFF04 && addr <= 0xFF07) {
        gb_timer_write(g, addr, value);
    } else if (addr == 0xFF0F) {
        g->mem.iflag = value & 0x1F;
    } else if (addr == 0xFFFF) {
        g->mem.ie = value;
    } else if (addr >= 0xFF80) {
        g->mem.hram[addr - 0xFF80] = value;
    }
}
