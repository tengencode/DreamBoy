#include "machine.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "gb.h"
#include "gba.h"

struct machine {
    gba_t *gba; /* exactly one of the two is set */
    gb_t *gb;
};

static bool gba_header(const uint8_t *rom, size_t size) {
    if (size < 0xC0 || rom[0xB2] != 0x96) return false;
    uint8_t sum = 0;
    for (int i = 0xA0; i <= 0xBC; i++) sum = (uint8_t)(sum - rom[i]);
    return (uint8_t)(sum - 0x19) == rom[0xBD];
}

static bool gb_header(const uint8_t *rom, size_t size) {
    if (size < 0x150 || rom[0x148] > 8 || (size_t)0x8000 << rom[0x148] != size) return false;
    uint8_t sum = 0;
    for (int i = 0x134; i <= 0x14C; i++) sum = (uint8_t)(sum - rom[i] - 1);
    return sum == rom[0x14D];
}

static bool ends_with(const char *name, const char *ending) {
    size_t n = strlen(name), e = strlen(ending);
    if (n < e) return false;
    for (size_t i = 0; i < e; i++)
        if (tolower((unsigned char)name[n - e + i]) != ending[i]) return false;
    return true;
}

machine_kind_t machine_detect(const uint8_t *rom, size_t size, const char *name) {
    if (!rom) return MACHINE_NONE;
    if (gba_header(rom, size)) return MACHINE_GBA;
    bool color = size > 0x143 && (rom[0x143] & 0x80);
    if (gb_header(rom, size)) return color ? MACHINE_GBC : MACHINE_GB;
    if (!name) return MACHINE_NONE;
    if (ends_with(name, ".gba")) return MACHINE_GBA;
    if (ends_with(name, ".gb") || ends_with(name, ".gbc")) return color ? MACHINE_GBC : MACHINE_GB;
    return MACHINE_NONE;
}

machine_t *machine_create(machine_kind_t kind) {
    if (kind == MACHINE_NONE) return NULL;
    machine_t *m = calloc(1, sizeof *m);
    if (!m) return NULL;
    if (kind == MACHINE_GBA) m->gba = gba_create(NULL);
    else m->gb = gb_create();
    if (!m->gba && !m->gb) {
        free(m);
        return NULL;
    }
    return m;
}

void machine_destroy(machine_t *m) {
    if (!m) return;
    gba_destroy(m->gba);
    gb_destroy(m->gb);
    free(m);
}

machine_kind_t machine_kind(const machine_t *m) {
    return m->gba ? MACHINE_GBA : gb_is_color(m->gb) ? MACHINE_GBC : MACHINE_GB;
}

int machine_load_rom(machine_t *m, const uint8_t *rom, size_t size) {
    if (m->gba) {
        gba_err_t err = gba_load_rom(m->gba, rom, size);
        return err == GBA_OK ? MACHINE_OK : err == GBA_ERR_NOMEM ? MACHINE_ERR_NOMEM : MACHINE_ERR_ROM;
    }
    switch (gb_load_rom(m->gb, rom, size)) {
    case GB_OK: return MACHINE_OK;
    case GB_ERR_UNSUPPORTED_CART: return MACHINE_ERR_UNSUPPORTED;
    case GB_ERR_NOMEM: return MACHINE_ERR_NOMEM;
    default: return MACHINE_ERR_ROM;
    }
}

void machine_reset(machine_t *m) {
    if (m->gba) gba_reset(m->gba);
    else gb_reset(m->gb);
}

void machine_run_frame(machine_t *m) {
    if (m->gba) gba_run_frame(m->gba);
    else gb_run_frame(m->gb);
}

int machine_width(const machine_t *m) {
    return m->gba ? 240 : GB_WIDTH;
}

int machine_height(const machine_t *m) {
    return m->gba ? 160 : GB_HEIGHT;
}

const uint16_t *machine_framebuffer(const machine_t *m) {
    return m->gba ? gba_framebuffer(m->gba) : gb_framebuffer(m->gb);
}

size_t machine_audio_read(machine_t *m, int16_t *out, size_t max_frames) {
    return m->gba ? gba_audio_read(m->gba, out, max_frames) : gb_audio_read(m->gb, out, max_frames);
}

void machine_set_keys(machine_t *m, uint16_t pressed) {
    if (m->gba) gba_set_keys(m->gba, pressed);
    else gb_set_keys(m->gb, (uint8_t)pressed); /* the low 8 bits are the same buttons */
}

const char *machine_title(const machine_t *m) {
    return m->gba ? gba_rom_title(m->gba) : gb_rom_title(m->gb);
}

uint32_t machine_crc32(const machine_t *m) {
    return m->gba ? gba_rom_crc32(m->gba) : gb_rom_crc32(m->gb);
}

size_t machine_save_size(const machine_t *m) {
    return m->gba ? gba_saveram_size(m->gba) : gb_save_size(m->gb);
}

size_t machine_save_write(const machine_t *m, uint8_t *buf, size_t cap) {
    if (m->gb) return gb_save_write(m->gb, buf, cap);
    size_t size = gba_saveram_size(m->gba);
    if (!size || size > cap) return 0;
    memcpy(buf, gba_saveram(m->gba), size);
    return size;
}

int machine_save_read(machine_t *m, const uint8_t *data, size_t size) {
    if (m->gba) return gba_load_saveram(m->gba, data, size) == GBA_OK ? MACHINE_OK : MACHINE_ERR_SAVE;
    gb_save_read(m->gb, data, size);
    return MACHINE_OK;
}

int machine_save_dirty(machine_t *m) {
    return m->gba ? gba_saveram_dirty(m->gba) : gb_save_dirty(m->gb);
}

void machine_set_rtc(machine_t *m, int64_t unix_seconds, int32_t utc_offset_s) {
    if (m->gba) gba_set_rtc(m->gba, unix_seconds, utc_offset_s);
    else gb_set_rtc(m->gb, unix_seconds);
}

/* shortcut: no save states or rewind for the Game Boy core yet; plan Task 10 adds them. */
size_t machine_state_size(const machine_t *m) {
    return m->gba ? gba_state_size(m->gba) : 0;
}

size_t machine_state_save(const machine_t *m, void *buf, size_t cap) {
    return m->gba ? gba_state_save(m->gba, buf, cap) : 0;
}

int machine_state_load(machine_t *m, const void *buf, size_t len) {
    return m->gba && gba_state_load(m->gba, buf, len) == GBA_OK ? MACHINE_OK : MACHINE_ERR_STATE;
}

int machine_rewind_configure(machine_t *m, size_t budget_bytes, int interval_frames) {
    if (!m->gba) return MACHINE_ERR_NOMEM;
    return gba_rewind_configure(m->gba, budget_bytes, interval_frames) == GBA_OK ? MACHINE_OK : MACHINE_ERR_NOMEM;
}

int machine_rewind_step_back(machine_t *m) {
    return m->gba ? gba_rewind_step_back(m->gba) : 0;
}

struct gba *machine_gba(machine_t *m) {
    return m->gba;
}

struct gb *machine_gb(machine_t *m) {
    return m->gb;
}
