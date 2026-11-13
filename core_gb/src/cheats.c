/* Cheats (17c). A cheat is its codes as text, one per line or separated by '+' or spaces:
 *
 * - Game Genie, ABC-DEF or ABC-DEF-GHI (dashes optional): a byte of the cartridge ROM reads as a new value. Value
 *   AB; address F C D E with F XORed by 0xF; the nine-digit form has a compare byte, (G I rotated right by 2) XOR
 *   0xBA — the byte is only replaced where the ROM holds that value (H is a check digit, not checked). Applied by
 *   patching the loaded ROM when the cheats are set: below 0x4000 the byte in bank 0, from 0x4000 the byte at that
 *   place in every bank from 1 up that fits the compare. The old bytes are kept and put back.
 * - GameShark, TTVVLLHH: the value VV is written to HHLL at the end of every frame, straight into the memory. TT 8x /
 *   9x names bank x of the memory the address is in (cartridge RAM; work RAM at 0xD000 on a Game Boy Color, where
 *   0 means 1); anything else is the bank mapped now. A bank that is not there: no write.
 *
 * Cheats are the frontend's: no save state or rewind snapshot holds them. */
#include <stdlib.h>
#include <string.h>

#include "gb_internal.h"

#define MAX_CODES 256

static int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* One code (len characters, no spaces) into *out. false: not a code of this type. */
static bool decode(const char *s, size_t len, gb_cheat_type_t type, gb_code_t *out, gb_cheat_type_t *used) {
    int d[9];
    size_t n = 0;
    bool dashes = false;
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '-') {
            dashes = true;
            continue;
        }
        int v = hex(s[i]);
        if (v < 0 || n == 9) return false;
        d[n++] = v;
    }
    bool genie = n == 6 || n == 9;
    if (!genie && (n != 8 || dashes)) return false;
    if (type != GB_CHEAT_AUTO && (type == GB_CHEAT_GAME_GENIE) != genie) return false;
    memset(out, 0, sizeof *out);
    if (genie) {
        out->genie = 1;
        out->value = (uint8_t)(d[0] << 4 | d[1]);
        out->addr = (uint16_t)((d[5] ^ 0xF) << 12 | d[2] << 8 | d[3] << 4 | d[4]);
        if (out->addr >= 0x8000) return false;
        if (n == 9) {
            uint8_t t = (uint8_t)(d[6] << 4 | d[8]);
            out->has_compare = 1;
            out->compare = (uint8_t)((t >> 2 | t << 6) ^ 0xBA);
        }
    } else {
        out->bank = (uint8_t)(d[0] << 4 | d[1]);
        out->value = (uint8_t)(d[2] << 4 | d[3]);
        out->addr = (uint16_t)(d[6] << 12 | d[7] << 8 | d[4] << 4 | d[5]);
        bool ram = (out->addr >= 0xA000 && out->addr < 0xE000) || (out->addr >= 0xFF80 && out->addr < 0xFFFF);
        if (!ram) return false;
    }
    *used = genie ? GB_CHEAT_GAME_GENIE : GB_CHEAT_GAMESHARK;
    return true;
}

/* Reads a cheat's text, appending to codes (NULL: only checking) while *count < cap. Returns 0, or the first bad
 * line (1-based, blank lines counted; a text with no code at all: line 1); -1 = more codes than fit. */
static int read_text(const char *text, gb_cheat_type_t type, gb_code_t *codes, size_t *count, size_t cap,
                     gb_cheat_type_t *used) {
    int line = 1;
    bool any = false;
    gb_cheat_type_t first = type;
    const char *p = text;
    while (*p) {
        size_t len = strcspn(p, "\n+ \t\r");
        if (len) { /* (nothing between two separators is no code and no mistake) */
            gb_code_t c;
            gb_cheat_type_t got;
            if (!decode(p, len, type, &c, &got)) return line;
            if (!any) first = got; /* (Auto reads each code as what it is; the cheat is named after its first) */
            any = true;
            if (codes) {
                if (*count >= cap) return -1;
                codes[(*count)++] = c;
            }
        }
        p += len;
        if (*p == '\n') line++;
        if (*p) p++;
    }
    if (!any) return 1;
    *used = first;
    return 0;
}

gb_cheat_type_t gb_cheat_check(const char *text, gb_cheat_type_t type, int *bad_line) {
    gb_cheat_type_t used = type;
    size_t count = 0;
    *bad_line = read_text(text, type, NULL, &count, 0, &used);
    return used;
}

static void unpatch(gb_t *g) {
    for (size_t i = g->cheats.npatches; i-- > 0;) g->info.rom[g->cheats.patches[i].at] = g->cheats.patches[i].old;
    g->cheats.npatches = 0;
}

void gb_cheats_clear(gb_t *g) {
    if (g->info.rom) unpatch(g);
    free(g->cheats.codes);
    free(g->cheats.patches);
    memset(&g->cheats, 0, sizeof g->cheats);
}

static void patch(gb_t *g, uint32_t at, const gb_code_t *c) {
    if (at >= g->info.rom_size || (c->has_compare && g->info.rom[at] != c->compare)) return;
    g->cheats.patches[g->cheats.npatches].at = at;
    g->cheats.patches[g->cheats.npatches++].old = g->info.rom[at];
    g->info.rom[at] = c->value;
}

gb_err_t gb_cheats_set(gb_t *g, const char *const *texts, const gb_cheat_type_t *types, size_t count) {
    gb_cheats_clear(g);
    if (!count || !g->info.rom) return GB_OK;
    gb_code_t *codes = malloc(MAX_CODES * sizeof *codes);
    if (!codes) return GB_ERR_NOMEM;
    size_t n = 0, genies = 0;
    for (size_t i = 0; i < count; i++) {
        gb_cheat_type_t used;
        if (read_text(texts[i], types[i], codes, &n, MAX_CODES, &used) != 0) {
            free(codes);
            return GB_ERR_CHEAT;
        }
    }
    for (size_t i = 0; i < n; i++) genies += codes[i].genie;
    uint32_t banks = g->info.rom_size / 0x4000;
    g->cheats.patches = genies ? malloc(genies * banks * sizeof *g->cheats.patches) : NULL;
    if (genies && !g->cheats.patches) {
        free(codes);
        return GB_ERR_NOMEM;
    }
    g->cheats.codes = codes;
    g->cheats.ncodes = n;
    for (size_t i = 0; i < n; i++) {
        const gb_code_t *c = &codes[i];
        if (!c->genie) continue;
        if (c->addr < 0x4000) patch(g, c->addr, c);
        else
            for (uint32_t bank = 1; bank < banks; bank++) patch(g, bank * 0x4000 + (c->addr & 0x3FFF), c);
    }
    return GB_OK;
}

void gb_cheats_run(gb_t *g) {
    for (size_t i = 0; i < g->cheats.ncodes; i++) {
        const gb_code_t *c = &g->cheats.codes[i];
        if (c->genie) continue;
        int bank = c->bank >= 0x80 ? c->bank & 0x0F : -1; /* -1: the one mapped now */
        if (c->addr >= 0xFF80) g->mem.hram[c->addr - 0xFF80] = c->value;
        else if (c->addr < 0xC000) gb_cart_poke(g, bank, c->addr, c->value);
        else if (c->addr < 0xD000) g->mem.wram[c->addr - 0xC000] = c->value;
        else {
            if (bank < 0 || !g->info.color) bank = g->mem.wram_bank;
            if (bank == 0) bank = 1;
            if (bank < 8) g->mem.wram[bank * 0x1000 + (c->addr & 0x0FFF)] = c->value;
        }
    }
}
