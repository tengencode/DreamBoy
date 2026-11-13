#include <stdlib.h>
#include <string.h>

#include "gb_internal.h"

gb_t *gb_create(void) {
    return calloc(1, sizeof(gb_t));
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

size_t gb_save_size(const gb_t *g) {
    return g->info.battery ? g->info.ram_size : 0;
}
