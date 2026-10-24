#include "notes.h"

#ifdef GBA_NOTES
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "gba_internal.h"

/* Kinds whose detail tells entries apart (one per BIOS call, one per register); the others keep the first. */
static bool keyed(note_kind_t kind) {
    return kind == NOTE_BIOS_CALL || kind == NOTE_MIDLINE_IO;
}

void notes_add(gba_t *g, note_kind_t kind, uint32_t detail) {
    notes_t *n = &g->notes;
    for (uint32_t i = 0; i < n->count; i++) {
        note_t *e = &n->list[i];
        if (e->kind == kind && (!keyed(kind) || e->detail == detail)) {
            e->count++;
            return;
        }
    }
    if (n->count >= NOTES_MAX - 1 && kind != NOTE_DROPPED) { /* the last entry is kept for the dropped count */
        notes_add(g, NOTE_DROPPED, 0);
        return;
    }
    n->list[n->count++] = (note_t){(uint8_t)kind, detail, n->frame, 1};
}

void notes_drawing(gba_t *g, note_kind_t kind, uint32_t detail) {
    const ppu_t *p = &g->ppu;
    if (g->bus.in_bios || p->vcount >= PPU_HEIGHT || (p->dispstat & DISPSTAT_HBLANK) || (p->dispcnt & 0x0080)) return;
    notes_add(g, kind, detail);
}

/* I/O halfwords with a register behind them (GBATEK's I/O map); the rest of 0x000-0x3FF is unused. */
static const uint16_t REGISTERS[][2] = {
    {0x000, 0x04C}, {0x050, 0x054}, {0x060, 0x064}, {0x068, 0x068}, {0x06C, 0x06C}, {0x070, 0x074},
    {0x078, 0x078}, {0x07C, 0x07C}, {0x080, 0x084}, {0x088, 0x088}, {0x090, 0x0A6}, {0x0B0, 0x0DE},
    {0x100, 0x10E}, {0x120, 0x12A}, {0x130, 0x134}, {0x140, 0x140}, {0x150, 0x158}, {0x200, 0x204},
    {0x208, 0x208}, {0x300, 0x300},
};

static bool has_register(uint32_t off) {
    off &= ~1u;
    for (size_t i = 0; i < sizeof REGISTERS / sizeof REGISTERS[0]; i++)
        if (off >= REGISTERS[i][0] && off <= REGISTERS[i][1]) return true;
    return false;
}

enum { PRINT_AREA = 0xFFF600, PRINT_SEND = 0xFFF700, PRINT_END = 0xFFF800 }; /* I/O offsets of mGBA's print */

/* The message in the print area, kept once per text and level, then the area cleared (as mGBA does). */
static void game_print(gba_t *g, uint32_t level) {
    notes_t *n = &g->notes;
    for (uint32_t i = 0; i < n->print_count; i++) {
        note_print_t *p = &n->prints[i];
        if (p->level == level && !strncmp(p->text, n->print_area, NOTES_PRINT_LEN - 1)) {
            p->count++;
            memset(n->print_area, 0, sizeof n->print_area);
            return;
        }
    }
    if (n->print_count < NOTES_PRINTS) {
        note_print_t *p = &n->prints[n->print_count++];
        *p = (note_print_t){level, n->frame, 1, {0}};
        strncpy(p->text, n->print_area, NOTES_PRINT_LEN - 1);
    } else {
        notes_add(g, NOTE_DROPPED, 0);
    }
    memset(n->print_area, 0, sizeof n->print_area);
}

void notes_io_write(gba_t *g, uint32_t off, uint32_t value, unsigned size) {
    if (g->bus.in_bios) return;
    if (off >= PRINT_AREA && off < PRINT_SEND) {
        for (unsigned k = 0; k < size && off - PRINT_AREA + k < 256; k++)
            g->notes.print_area[off - PRINT_AREA + k] = (char)(value >> (8 * k));
        return;
    }
    if (off >= PRINT_SEND && off < PRINT_END) { /* send (0x700) and "is this mGBA?" (0x780) */
        if (off == PRINT_SEND && (value & 0x100)) game_print(g, value & 7);
        return;
    }
    bool used = off < IO_SIZE && (has_register(off) || (size == 4 && has_register(off + 2)));
    if (!used) notes_add(g, NOTE_IO_UNUSED, off);
}

void notes_fetch(gba_t *g, uint32_t addr) {
    bool empty;
    switch (addr >> 24) {
    case 0x0: empty = addr >= BIOS_SIZE; break;
    case 0x2:
    case 0x3:
    case 0x5:
    case 0x6:
    case 0x7: empty = false; break; /* RAM, palette, VRAM, OAM: odd places for code, but real memory */
    case 0x8:
    case 0x9:
    case 0xA:
    case 0xB:
    case 0xC:
    case 0xD: empty = (addr & 0x01FFFFFFu) >= g->cart.rom_size; break; /* past the ROM's end */
    default: empty = true; break; /* 0x1, I/O, the save area and everything above */
    }
    if (empty) notes_add(g, NOTE_EXEC_EMPTY, addr);
}

void notes_clear(gba_t *g) {
    memset(&g->notes, 0, sizeof g->notes);
}
#endif
