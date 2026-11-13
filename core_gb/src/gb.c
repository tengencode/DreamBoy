#include <stdlib.h>
#include <string.h>

#include "gb_internal.h"

gb_t *gb_create(void) {
    gb_t *g = calloc(1, sizeof(gb_t));
    if (g) gb_ppu_reset(g); /* a white picture until a game draws */
    return g;
}

void gb_destroy(gb_t *g) {
    if (!g) return;
    gb_cart_free(g);
    free(g);
}

gb_err_t gb_load_rom(gb_t *g, const uint8_t *rom, size_t size) {
    gb_err_t err = gb_cart_load(g, rom, size);
    if (err == GB_OK) gb_reset(g);
    return err;
}

void gb_reset(gb_t *g) {
    memset(&g->mem, 0, sizeof g->mem);
    g->mem.wram_bank = 1;
    memset(&g->timer, 0, sizeof g->timer);
    memset(&g->serial, 0, sizeof g->serial);
    g->joypad.select = 0;  /* the held keys stay: they are the player's, not the machine's */
    g->mem.iflag = 0x01;   /* the boot ROM leaves a VBlank request behind */
    g->timer.div = 0xABC8;   /* where the boot ROM leaves the divider */
    g->serial_len = 0;
    gb_cart_reset(g);
    gb_ppu_reset(g);
    gb_apu_reset(g);
    g->ticks = 0;
    g->frame_left = 0;
    gb_cpu_reset(g);
}

void gb_run_frame(gb_t *g) {
    g->frame_left += GB_FRAME_TICKS;
    while (g->frame_left > 0) gb_cpu_step(g);
}

const uint8_t *gb_wram(const gb_t *g) {
    return g->mem.wram;
}

int gb_is_color(const gb_t *g) {
    return g->info.color;
}

const char *gb_rom_title(const gb_t *g) {
    return g->info.title;
}

uint32_t gb_rom_crc32(const gb_t *g) {
    return g->info.crc32;
}

const uint16_t *gb_framebuffer(const gb_t *g) {
    return g->frame;
}

size_t gb_save_size(const gb_t *g) {
    return g->info.battery ? g->info.ram_size : 0;
}

/* shortcut: the cartridge RAM only; the clock's footer comes with plan Task 9. */
size_t gb_save_write(const gb_t *g, uint8_t *buf, size_t cap) {
    size_t size = gb_save_size(g);
    if (!size || size > cap) return 0;
    memcpy(buf, g->cart_ram, size);
    return size;
}

void gb_save_read(gb_t *g, const uint8_t *data, size_t size) {
    size_t ram = gb_save_size(g);
    if (!ram) return;
    size_t n = size < ram ? size : ram;
    memset(g->cart_ram, 0xFF, ram);
    if (data && n) memcpy(g->cart_ram, data, n);
}

void gb_set_rtc(gb_t *g, int64_t unix_seconds) {
}

int gb_save_dirty(gb_t *g) {
    int dirty = g->cart.dirty;
    g->cart.dirty = 0;
    return dirty;
}
