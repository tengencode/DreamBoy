#include <stdlib.h>
#include <string.h>

#include "gb_internal.h"
#include "rewind_ring.h"

gb_t *gb_create(void) {
    gb_t *g = calloc(1, sizeof(gb_t));
    if (g) {
        gb_set_look(g, GB_LOOK_GREY);
        gb_ppu_reset(g); /* a white picture until a game draws */
    }
    return g;
}

void gb_destroy(gb_t *g) {
    if (!g) return;
    gb_cart_free(g);
    rewind_ring_free(g->rewind);
    free(g);
}

gb_err_t gb_load_rom(gb_t *g, const uint8_t *rom, size_t size) {
    gb_err_t err = gb_cart_load(g, rom, size);
    if (err != GB_OK) return err;
    gb_look_refresh(g);
    gb_reset(g);
    gb_rewind_configure(g, g->rewind_budget, g->rewind_interval); /* the state's size may be another now */
    return GB_OK;
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
    gb_cgb_reset(g);
    gb_cart_reset(g);
    gb_ppu_reset(g);
    gb_apu_reset(g);
    g->ticks = 0;
    g->frame_left = 0;
    rewind_ring_clear(g->rewind);
    gb_cpu_reset(g);
}

void gb_run_frame(gb_t *g) {
    g->frame_left += GB_FRAME_TICKS;
    while (g->frame_left > 0) gb_cpu_step(g);
    gb_apu_sync(g); /* every sample of the frame is there to be read, and a state taken now has nothing pending */
    gb_rewind_after_frame(g);
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

int gb_save_dirty(gb_t *g) {
    int dirty = g->cart.dirty;
    g->cart.dirty = 0;
    return dirty;
}
