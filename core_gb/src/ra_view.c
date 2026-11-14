/* The machine's memory as RetroAchievements addresses it (17c). rcheevos fixes the layout for the Game Boy and the
 * Game Boy Color (its consoleinfo.c): the address space as the game sees it, except that 0xA000-0xBFFF is always
 * cartridge RAM bank 0 and 0xD000-0xDFFF always work RAM bank 1; then work RAM banks 2-7 at 0x10000 (Game Boy
 * Color only) and cartridge RAM banks 1-15 at 0x16000. Reading changes nothing in the machine, and reaches video
 * and sprite memory also while the CPU could not. */
#include "gb_internal.h"

/* The byte at one address; false: this machine has no memory there. */
static bool byte_at(const gb_t *g, uint32_t a, uint8_t *out) {
    if (a < 0x8000) {
        if (!g->info.rom) return false;
        *out = gb_cart_read(g, (uint16_t)a); /* bank 0, then the bank mapped at 0x4000 */
    } else if (a < 0xA000) {
        *out = g->ppu.vram[g->cgb.vbk * 0x2000u + (a & 0x1FFF)];
    } else if (a < 0xC000 && g->info.mbc == MBC7) { /* a tilt cartridge: its registers, whatever its switches say */
        *out = a < 0xB000 ? gb_mbc7_peek(g, (uint16_t)a) : 0xFF;
    } else if (a < 0xC000) {
        if (a - 0xA000 >= g->info.ram_size) return false;
        *out = g->cart_ram[a - 0xA000];
    } else if (a < 0xFE00) {
        *out = g->mem.wram[a & 0x1FFF]; /* banks 0 and 1, and their echo */
    } else if (a < 0xFEA0) {
        *out = g->ppu.oam[a - 0xFE00];
    } else if (a < 0xFF00) {
        *out = 0;
    } else if (a < 0x10000) {
        *out = gb_bus_peek(g, (uint16_t)a); /* registers (no side effects), high RAM, the interrupt enables */
    } else if (a < 0x16000) {
        if (!g->info.color) return false;
        *out = g->mem.wram[0x2000 + (a - 0x10000)];
    } else {
        uint32_t at = 0x2000 + (a - 0x16000);
        if (a >= 0x34000 || at >= g->info.ram_size || g->info.mbc == MBC7) return false;
        *out = g->cart_ram[at];
    }
    return true;
}

uint32_t gb_ra_peek(const gb_t *g, uint32_t address, uint8_t *buffer, uint32_t count) {
    for (uint32_t i = 0; i < count; i++)
        if (!byte_at(g, address + i, &buffer[i])) return i;
    return count;
}
