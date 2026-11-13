/* The memory map and the passing of time. */
#include "gb_internal.h"

/* A machine cycle is 4 clock ticks — 2 at a Game Boy Color's double speed, where the CPU, timer and OAM DMA run
 * twice as fast against the screen and the sound. */
void gb_tick(gb_t *g) {
    int dots = g->cgb.double_speed ? 2 : 4;
    gb_timer_tick(g);
    gb_dma_tick(g);
    gb_ppu_tick(g, dots);
    gb_apu_tick(g, dots);
    g->ticks += (unsigned)dots;
    g->frame_left -= dots;
}

static uint8_t *wram_at(gb_t *g, uint16_t addr) {
    uint16_t a = addr & 0x1FFF; /* 0xE000-0xFDFF shows 0xC000-0xDDFF again */
    return &g->mem.wram[a < 0x1000 ? a : g->mem.wram_bank * 0x1000u + (a & 0xFFF)];
}

/* cpu: the CPU is reading, so memory the screen is using reads 0xFF. */
static uint8_t read(const gb_t *g, uint16_t addr, bool cpu) {
    if (addr < 0x8000) return gb_cart_read(g, addr);
    if (addr < 0xA000) return cpu && gb_ppu_vram_blocked(g) ? 0xFF : g->ppu.vram[g->cgb.vbk * 0x2000u + (addr & 0x1FFF)];
    if (addr < 0xC000) return gb_cart_read(g, addr);
    if (addr < 0xFE00) return *wram_at((gb_t *)g, addr);
    if (addr < 0xFF00) {
        if (cpu && gb_ppu_oam_blocked(g)) return 0xFF;
        return addr < 0xFEA0 ? g->ppu.oam[addr - 0xFE00] : 0x00;
    }
    if (addr == 0xFF00) return gb_joypad_read(g);
    if (addr <= 0xFF02) return gb_serial_read(g, addr);
    if (addr >= 0xFF04 && addr <= 0xFF07) return gb_timer_read(g, addr);
    if (addr == 0xFF0F) return g->mem.iflag | 0xE0;
    if (addr >= 0xFF10 && addr <= 0xFF3F) return gb_apu_read(g, addr);
    if (addr >= 0xFF40 && addr <= 0xFF4B) return gb_ppu_read(g, addr);
    if (addr >= 0xFF4D && addr <= 0xFF75) return gb_cgb_read(g, addr);
    if (addr == 0xFFFF) return g->mem.ie;
    if (addr >= 0xFF80) return g->mem.hram[addr - 0xFF80];
    return 0xFF;
}

uint8_t gb_bus_peek(const gb_t *g, uint16_t addr) {
    return read(g, addr, false);
}

uint8_t gb_bus_read(gb_t *g, uint16_t addr) {
    gb_tick(g);
    return read(g, addr, true);
}

void gb_bus_write(gb_t *g, uint16_t addr, uint8_t value) {
    gb_tick(g);
    if (addr < 0x8000 || (addr >= 0xA000 && addr < 0xC000)) {
        gb_cart_write(g, addr, value);
    } else if (addr < 0xA000) {
        if (!gb_ppu_vram_blocked(g)) g->ppu.vram[g->cgb.vbk * 0x2000u + (addr & 0x1FFF)] = value;
    } else if (addr < 0xFE00) {
        *wram_at(g, addr) = value;
    } else if (addr < 0xFF00) {
        if (addr < 0xFEA0 && !gb_ppu_oam_blocked(g)) g->ppu.oam[addr - 0xFE00] = value;
    } else if (addr == 0xFF00) {
        gb_joypad_write(g, value);
    } else if (addr <= 0xFF02) {
        gb_serial_write(g, addr, value);
    } else if (addr >= 0xFF04 && addr <= 0xFF07) {
        gb_timer_write(g, addr, value);
    } else if (addr == 0xFF0F) {
        g->mem.iflag = value & 0x1F;
    } else if (addr >= 0xFF10 && addr <= 0xFF3F) {
        gb_apu_write(g, addr, value);
    } else if (addr >= 0xFF40 && addr <= 0xFF4B) {
        gb_ppu_write(g, addr, value);
    } else if (addr >= 0xFF4D && addr <= 0xFF75) {
        gb_cgb_write(g, addr, value);
    } else if (addr == 0xFFFF) {
        g->mem.ie = value;
    } else if (addr >= 0xFF80) {
        g->mem.hram[addr - 0xFF80] = value;
    }
}
