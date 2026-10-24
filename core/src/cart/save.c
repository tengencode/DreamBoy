#include "cart/save.h"

#include <string.h>

#include "gba_internal.h"

static const struct {
    const char *text;
    gba_save_type_t type;
} markers[] = {
    {"EEPROM_V", GBA_SAVE_EEPROM},     {"SRAM_V", GBA_SAVE_SRAM},        {"SRAM_F_V", GBA_SAVE_SRAM},
    {"FLASH1M_V", GBA_SAVE_FLASH128}, {"FLASH_V", GBA_SAVE_FLASH64}, {"FLASH512_V", GBA_SAVE_FLASH64},
};

/* The save libraries leave their name in the ROM, word aligned; the first one found wins. */
static gba_save_type_t detect(const uint8_t *rom, size_t size) {
    for (size_t off = 0; off + 8 <= size; off += 4) {
        uint8_t c = rom[off];
        if (c != 'E' && c != 'S' && c != 'F') continue;
        for (size_t k = 0; k < sizeof markers / sizeof markers[0]; k++) {
            size_t n = strlen(markers[k].text);
            if (off + n <= size && memcmp(rom + off, markers[k].text, n) == 0) return markers[k].type;
        }
    }
    return GBA_SAVE_UNKNOWN;
}

size_t save_type_size(gba_save_type_t type) {
    switch (type) {
    case GBA_SAVE_SRAM: return 0x8000;
    case GBA_SAVE_FLASH64: return 0x10000;
    case GBA_SAVE_FLASH128: return 0x20000;
    case GBA_SAVE_EEPROM512: return 512;
    case GBA_SAVE_EEPROM8K: return 8192;
    default: return 0;
    }
}

static void blank(save_t *s, gba_save_type_t type) {
    s->type = type;
    s->dirty = false;
    memset(s->data, 0xFF, sizeof s->data);
    save_reset(s);
}

void save_setup(save_t *s, const uint8_t *rom, size_t size) {
    blank(s, detect(rom, size));
}

void save_reset(save_t *s) {
    memset(&s->flash, 0, sizeof s->flash);
    memset(&s->eeprom, 0, sizeof s->eeprom);
}

uint8_t save_read8(gba_t *g, uint32_t addr) {
    if (tilt_mapped(g, addr)) return tilt_read(&g->cart.hw, addr);
    save_t *s = &g->cart.save;
    switch (s->type) {
    case GBA_SAVE_SRAM: return s->data[addr & 0x7FFF]; /* 32 KiB, repeated */
    case GBA_SAVE_FLASH64:
    case GBA_SAVE_FLASH128: return flash_read(s, addr & 0xFFFF); /* 64 KiB window, repeated */
    default: return 0xFF; /* no chip here (yet) */
    }
}

void save_write8(gba_t *g, uint32_t addr, uint8_t value) {
    if (tilt_mapped(g, addr)) {
        tilt_write(&g->cart.hw, addr, value);
        return;
    }
    save_t *s = &g->cart.save;
    if (s->type == GBA_SAVE_UNKNOWN) { /* the first write decides: a Flash command prefix, or SRAM */
        s->type = ((addr & 0xFFFF) == 0x5555 && value == 0xAA) ? GBA_SAVE_FLASH64 : GBA_SAVE_SRAM;
    }
    switch (s->type) {
    case GBA_SAVE_SRAM:
        s->data[addr & 0x7FFF] = value;
        s->dirty = true;
        break;
    case GBA_SAVE_FLASH64:
    case GBA_SAVE_FLASH128: flash_write(s, addr & 0xFFFF, value); break;
    default: break;
    }
}

/* ---- public API ---- */

gba_save_type_t gba_save_type(const gba_t *g) {
    return g->cart.save.type;
}

uint8_t *gba_saveram(gba_t *g) {
    return g->cart.save.data;
}

size_t gba_saveram_size(const gba_t *g) {
    return save_type_size(g->cart.save.type);
}

/* The type a save file of `size` bytes implies; NONE when it fits nothing. */
static gba_save_type_t type_for_size(size_t size, bool eeprom_only) {
    if (size == 512) return GBA_SAVE_EEPROM512;
    if (size == 8192) return GBA_SAVE_EEPROM8K;
    if (eeprom_only) return GBA_SAVE_NONE;
    if (size == 0x8000) return GBA_SAVE_SRAM;
    if (size == 0x10000) return GBA_SAVE_FLASH64;
    if (size == 0x20000) return GBA_SAVE_FLASH128;
    return GBA_SAVE_NONE;
}

gba_err_t gba_load_saveram(gba_t *g, const uint8_t *data, size_t size) {
    save_t *s = &g->cart.save;
    if ((size & 0xFF) == 16) size -= 16; /* mGBA's clock footer (games with a clock): our clock follows the phone's */
    gba_save_type_t type = s->type;
    if (type == GBA_SAVE_UNKNOWN || type == GBA_SAVE_EEPROM) type = type_for_size(size, type == GBA_SAVE_EEPROM);
    if (size == 0 || size != save_type_size(type)) return GBA_ERR_SAVE_SIZE;
    s->type = type;
    memcpy(s->data, data, size);
    return GBA_OK;
}

int gba_saveram_dirty(gba_t *g) {
    int dirty = g->cart.save.dirty;
    g->cart.save.dirty = false;
    return dirty;
}

void gba_set_save_type(gba_t *g, gba_save_type_t type) {
    blank(&g->cart.save, type);
}

/* ---- EEPROM window (0x0D) ---- */

static bool is_eeprom(gba_save_type_t type) {
    return type == GBA_SAVE_EEPROM || type == GBA_SAVE_EEPROM512 || type == GBA_SAVE_EEPROM8K;
}

/* All of 0x0D for ROMs up to 16 MiB; only its last 256 bytes for bigger ROMs. */
static bool eeprom_window(const gba_t *g, uint32_t addr) {
    if ((addr >> 24) != 0xD) return false;
    return g->cart.rom_size <= 0x1000000u || (addr & 0x00FFFFFF) >= 0x00FFFF00;
}

bool save_eeprom_mapped(const gba_t *g, uint32_t addr) {
    return is_eeprom(g->cart.save.type) && eeprom_window(g, addr);
}

uint16_t save_eeprom_read(gba_t *g) {
    return eeprom_read_bit(&g->cart.save);
}

void save_eeprom_write(gba_t *g, uint32_t value) {
    eeprom_write_bit(&g->cart.save, value & 1);
}

void save_eeprom_dma(gba_t *g, uint32_t dst, uint32_t count) {
    save_t *s = &g->cart.save;
    if (!eeprom_window(g, dst)) return;
    if (s->type == GBA_SAVE_UNKNOWN) s->type = GBA_SAVE_EEPROM;
    if (s->type != GBA_SAVE_EEPROM) return;
    /* Read requests are 2 + address + 1 bits, writes 2 + address + 64 + 1: 6-bit or 14-bit addresses. */
    if (count == 9 || count == 73) s->type = GBA_SAVE_EEPROM512;
    else if (count == 17 || count == 81) s->type = GBA_SAVE_EEPROM8K;
}
