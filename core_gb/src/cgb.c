/* What only a Game Boy Color has: the double-speed switch, banked work and video RAM, palette RAM, and the
 * memory copies into video RAM (HDMA). For a game that runs as a plain Game Boy all of these registers are dead. */
#include <string.h>

#include "gb_internal.h"

/* Copies 16 bytes into video RAM; the CPU stands still meanwhile (8 cycles; 16 at double speed). */
static void copy_block(gb_t *g) {
    gb_cgb_t *c = &g->cgb;
    for (int i = 0; i < 16; i++) {
        g->ppu.vram[c->vbk * 0x2000u + (c->hdma_dst & 0x1FFF)] = gb_bus_peek(g, c->hdma_src);
        c->hdma_src++;
        c->hdma_dst++;
    }
    c->stall = (uint16_t)(c->stall + (c->double_speed ? 16 : 8));
}

/* A visible line's HBlank began: a running per-line copy moves its next 16 bytes. */
void gb_cgb_hblank(gb_t *g) {
    gb_cgb_t *c = &g->cgb;
    if (!c->hdma_active) return;
    copy_block(g);
    if (c->hdma_left-- == 0) {
        c->hdma_active = 0;
        c->hdma_left = 0x7F;
    }
}

/* STOP with the switch armed: the CPU's clock doubles or halves; the screen and sound keep their pace. */
bool gb_cgb_speed_switch(gb_t *g) {
    gb_cgb_t *c = &g->cgb;
    if (!g->info.color || !c->speed_armed) return false;
    c->speed_armed = 0;
    c->double_speed = !c->double_speed;
    return true;
}

uint8_t gb_cgb_read(const gb_t *g, uint16_t addr) {
    const gb_cgb_t *c = &g->cgb;
    if (!g->info.color) return 0xFF;
    switch (addr) {
    case 0xFF4D: return (uint8_t)(0x7E | c->double_speed << 7 | c->speed_armed);
    case 0xFF4F: return 0xFE | c->vbk;
    case 0xFF55: return c->hdma_active ? c->hdma_left : 0x80 | c->hdma_left;
    case 0xFF56: return 0x3E | c->rp; /* nothing shines into the infrared port */
    case 0xFF68: return 0x40 | c->bcps;
    case 0xFF69: return gb_ppu_vram_blocked(g) ? 0xFF : c->bg_palette[c->bcps & 0x3F];
    case 0xFF6A: return 0x40 | c->ocps;
    case 0xFF6B: return gb_ppu_vram_blocked(g) ? 0xFF : c->obj_palette[c->ocps & 0x3F];
    case 0xFF6C: return 0xFE | c->opri;
    case 0xFF70: return 0xF8 | c->svbk;
    case 0xFF72: return c->ff72;
    case 0xFF73: return c->ff73;
    case 0xFF74: return c->ff74;
    case 0xFF75: return 0x8F | c->ff75;
    default: return 0xFF;
    }
}

/* Palette data: the byte goes in unless a line is being drawn; the index moves on either way when asked to. */
static void palette_write(const gb_t *g, uint8_t *ram, uint8_t *index, uint8_t value) {
    if (!gb_ppu_vram_blocked(g)) ram[*index & 0x3F] = value;
    if (*index & 0x80) *index = (uint8_t)(0x80 | ((*index + 1) & 0x3F));
}

void gb_cgb_write(gb_t *g, uint16_t addr, uint8_t value) {
    gb_cgb_t *c = &g->cgb;
    if (!g->info.color) return;
    switch (addr) {
    case 0xFF4D: c->speed_armed = value & 1; break;
    case 0xFF4F: c->vbk = value & 1; break;
    case 0xFF51: c->hdma_src = (uint16_t)((c->hdma_src & 0x00FF) | value << 8); break;
    case 0xFF52: c->hdma_src = (uint16_t)((c->hdma_src & 0xFF00) | (value & 0xF0)); break;
    case 0xFF53: c->hdma_dst = (uint16_t)((c->hdma_dst & 0x00FF) | (value & 0x1F) << 8); break;
    case 0xFF54: c->hdma_dst = (uint16_t)((c->hdma_dst & 0xFF00) | (value & 0xF0)); break;
    case 0xFF55:
        if (c->hdma_active && !(value & 0x80)) { /* stops a per-line copy where it is */
            c->hdma_active = 0;
            break;
        }
        c->hdma_left = value & 0x7F;
        if (value & 0x80) { /* 16 bytes in each HBlank; if this is one (or the screen is off), the first now */
            c->hdma_active = 1;
            if (!(g->ppu.lcdc & 0x80) || (g->ppu.mode == 0 && g->ppu.line < GB_HEIGHT)) gb_cgb_hblank(g);
        } else { /* all at once */
            for (int blocks = c->hdma_left + 1; blocks > 0; blocks--) copy_block(g);
            c->hdma_left = 0x7F;
        }
        break;
    case 0xFF56: c->rp = value & 0xC1; break;
    case 0xFF68: c->bcps = value & 0xBF; break;
    case 0xFF69: palette_write(g, c->bg_palette, &c->bcps, value); break;
    case 0xFF6A: c->ocps = value & 0xBF; break;
    case 0xFF6B: palette_write(g, c->obj_palette, &c->ocps, value); break;
    case 0xFF6C: c->opri = value & 1; break;
    case 0xFF70:
        c->svbk = value & 7;
        g->mem.wram_bank = c->svbk ? c->svbk : 1;
        break;
    case 0xFF72: c->ff72 = value; break;
    case 0xFF73: c->ff73 = value; break;
    case 0xFF74: c->ff74 = value; break;
    case 0xFF75: c->ff75 = value & 0x70; break;
    default: break;
    }
}

void gb_cgb_reset(gb_t *g) {
    memset(&g->cgb, 0, sizeof g->cgb);
    g->cgb.hdma_left = 0x7F;
    memset(g->cgb.bg_palette, 0xFF, sizeof g->cgb.bg_palette); /* white, as the boot ROM leaves the background */
}
