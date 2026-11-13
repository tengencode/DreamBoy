/* Save states and rewind: the whole machine, one chunk per component holding that component's struct as bytes
 * (the scheme of the Game Boy Advance core's state.c).
 *
 * Layout (little-endian hosts): header "GBCS", format version, ROM CRC-32, chunk count; then per chunk a 4-letter
 * tag, a version and a length, followed by the bytes. A state loads only into the same ROM and only when every
 * chunk's tag, version and length match this build: everything is checked first, so a refused state leaves the
 * machine untouched. Bump a chunk's version by hand when the meaning of its struct changes without its size
 * changing.
 *
 * A state may come from a file someone else made, so the values that pick a place in memory or keep a loop going
 * are checked too (values_ok), and what can be worked out again (where the cartridge's banks are) is. The ROM, the
 * sound not yet read and the rewind history are never in a state. */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "gb_internal.h"
#include "rewind_ring.h"

#define STATE_FORMAT 1u
#define HEADER_SIZE 16u
#define CHUNK_HEAD 12u
#define MAX_PERIOD ((112 << 15) + 8)  /* the longest channel period (noise), in ticks */
#define MAX_SUM (15 * 4 * 8 * 128)   /* 4 channels at full level and volume over one 128-tick sample */

/* The machine's own fields, in a fixed layout without padding. */
typedef struct {
    uint64_t ticks;
    int32_t frame_left;
    uint32_t reserved;
} misc_t;

typedef struct {
    char tag[4];
    uint32_t version;
    size_t offset; /* in struct gb */
    size_t size;   /* 0: as long as the cartridge RAM (the chunk points to it), or misc_t */
} chunk_t;

enum { C_CPU, C_MEM, C_TIMER, C_SERIAL, C_JOYPAD, C_CART, C_PPU, C_APU, C_CGB, C_PICTURE, C_RAM, C_MISC, CHUNK_COUNT };

static const chunk_t chunks[CHUNK_COUNT] = {
    [C_CPU] = {{'C', 'P', 'U', ' '}, 1, offsetof(struct gb, cpu), sizeof(gb_cpu_t)},
    [C_MEM] = {{'M', 'E', 'M', ' '}, 1, offsetof(struct gb, mem), sizeof(gb_mem_t)},
    [C_TIMER] = {{'T', 'M', 'R', ' '}, 1, offsetof(struct gb, timer), sizeof(gb_timer_t)},
    [C_SERIAL] = {{'S', 'E', 'R', ' '}, 1, offsetof(struct gb, serial), sizeof(gb_serial_t)},
    [C_JOYPAD] = {{'J', 'O', 'Y', ' '}, 1, offsetof(struct gb, joypad), sizeof(gb_joypad_t)},
    [C_CART] = {{'C', 'A', 'R', 'T'}, 1, offsetof(struct gb, cart), sizeof(gb_cart_t)},
    [C_PPU] = {{'P', 'P', 'U', ' '}, 1, offsetof(struct gb, ppu), sizeof(gb_ppu_t)},
    [C_APU] = {{'A', 'P', 'U', ' '}, 1, offsetof(struct gb, apu), sizeof(gb_apu_t)},
    [C_CGB] = {{'C', 'G', 'B', ' '}, 1, offsetof(struct gb, cgb), sizeof(gb_cgb_t)},
    /* both pictures: the one shown and the one being drawn (they are next to each other in struct gb) */
    [C_PICTURE] = {{'P', 'I', 'C', 'T'}, 1, offsetof(struct gb, frame), 2 * sizeof(uint16_t) * GB_WIDTH * GB_HEIGHT},
    [C_RAM] = {{'C', 'R', 'A', 'M'}, 1, 0, 0},
    [C_MISC] = {{'M', 'I', 'S', 'C'}, 1, 0, sizeof(misc_t)},
};

_Static_assert(offsetof(struct gb, back) == offsetof(struct gb, frame) + sizeof(((struct gb *)0)->frame),
               "the PICT chunk saves frame and back as one block: they must stay next to each other");

static size_t chunk_size(const gb_t *g, int i) {
    return i == C_RAM ? g->info.ram_size : chunks[i].size;
}

static void put32(uint8_t *p, uint32_t v) {
    memcpy(p, &v, 4);
}

static uint32_t get32(const uint8_t *p) {
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

/* A field of a chunk's struct, read from the state's bytes (little-endian hosts, like the layout). */
static uint64_t field_at(const uint8_t *data, size_t at, size_t n) {
    uint64_t v = 0;
    memcpy(&v, data + at, n);
    return v;
}
#define FIELD(data, type, member) field_at(data, offsetof(type, member), sizeof(((type *)0)->member))

/* Every value that indexes memory, shifts by a count or sets how long a loop runs is in range. */
static bool values_ok(const uint8_t *const data[]) {
    const uint8_t *ppu = data[C_PPU], *apu = data[C_APU];
    uint64_t bank = FIELD(data[C_MEM], gb_mem_t, wram_bank);
    if (bank < 1 || bank > 7 || FIELD(data[C_CGB], gb_cgb_t, vbk) > 1) return false;
    if (FIELD(ppu, gb_ppu_t, line) > 153 || FIELD(ppu, gb_ppu_t, phase) > 7 || FIELD(ppu, gb_ppu_t, dma_index) > 0xA0 ||
        FIELD(ppu, gb_ppu_t, dot) > 456 + 4 || FIELD(ppu, gb_ppu_t, next) > 456) {
        return false;
    }
    for (size_t n = 0; n < 4; n++) { /* a duty step 0-7 or a wave position 0-31; an 11-bit frequency */
        size_t at = offsetof(gb_apu_t, ch) + n * sizeof(gb_chan_t);
        int32_t timer;
        memcpy(&timer, apu + at + offsetof(gb_chan_t, timer), sizeof timer);
        if (field_at(apu, at + offsetof(gb_chan_t, pos), 1) > (n == 2 ? 31u : 7u) ||
            field_at(apu, at + offsetof(gb_chan_t, freq), 2) > 2047 || timer < -8 || timer > MAX_PERIOD) {
            return false; /* (a timer far below 0 would take for ever to catch up; far above, overflow) */
        }
    }
    if (FIELD(apu, gb_apu_t, sum_dots) >= 128 || FIELD(apu, gb_apu_t, pending) > 1024) return false;
    static const size_t sums[] = {offsetof(gb_apu_t, sum_left), offsetof(gb_apu_t, sum_right),
                                  offsetof(gb_apu_t, cap_left), offsetof(gb_apu_t, cap_right)};
    for (size_t i = 0; i < 4; i++) { /* the mixer's sums and filter: within what real sound can make them */
        int32_t v;
        memcpy(&v, apu + sums[i], sizeof v);
        int32_t limit = i < 2 ? MAX_SUM : MAX_SUM * 256;
        if (v < -limit || v > limit) return false;
    }
    misc_t misc;
    memcpy(&misc, data[C_MISC], sizeof misc);
    return misc.frame_left <= 0 && misc.frame_left > -4096; /* between two frames */
}

size_t gb_state_size(const gb_t *g) {
    size_t n = HEADER_SIZE;
    for (int i = 0; i < CHUNK_COUNT; i++) n += CHUNK_HEAD + chunk_size(g, i);
    return n;
}

size_t gb_state_save(const gb_t *g, void *buf, size_t cap) {
    size_t total = gb_state_size(g);
    if (!buf || cap < total) return 0;
    uint8_t *p = buf;
    memcpy(p, "GBCS", 4);
    put32(p + 4, STATE_FORMAT);
    put32(p + 8, g->info.crc32);
    put32(p + 12, CHUNK_COUNT);
    p += HEADER_SIZE;
    for (int i = 0; i < CHUNK_COUNT; i++) {
        size_t size = chunk_size(g, i);
        memcpy(p, chunks[i].tag, 4);
        put32(p + 4, chunks[i].version);
        put32(p + 8, (uint32_t)size);
        p += CHUNK_HEAD;
        if (i == C_MISC) {
            misc_t m = {g->ticks, g->frame_left, 0};
            memcpy(p, &m, sizeof m);
        } else if (i == C_RAM) {
            if (size) memcpy(p, g->cart_ram, size);
        } else {
            memcpy(p, (const uint8_t *)g + chunks[i].offset, size);
        }
        p += size;
    }
    return total;
}

/* gb_state_load without forgetting the rewind history (rewind loads its own snapshots with it). */
static gb_err_t state_load(gb_t *g, const void *buf, size_t len) {
    const uint8_t *p = buf;
    if (!buf || len != gb_state_size(g)) return GB_ERR_STATE;
    if (memcmp(p, "GBCS", 4) != 0 || get32(p + 4) != STATE_FORMAT || get32(p + 8) != g->info.crc32 ||
        get32(p + 12) != CHUNK_COUNT) {
        return GB_ERR_STATE;
    }
    const uint8_t *q = p + HEADER_SIZE;
    const uint8_t *data[CHUNK_COUNT];
    for (int i = 0; i < CHUNK_COUNT; i++) { /* check everything before changing anything */
        size_t size = chunk_size(g, i);
        if (memcmp(q, chunks[i].tag, 4) != 0 || get32(q + 4) != chunks[i].version || get32(q + 8) != size)
            return GB_ERR_STATE;
        data[i] = q + CHUNK_HEAD;
        q += CHUNK_HEAD + size;
    }
    if (!values_ok(data)) return GB_ERR_STATE;
    for (int i = 0; i < CHUNK_COUNT; i++) {
        size_t size = chunk_size(g, i);
        if (i == C_MISC) {
            misc_t m;
            memcpy(&m, data[i], sizeof m);
            g->ticks = m.ticks;
            g->frame_left = m.frame_left;
        } else if (i == C_RAM) {
            if (size) memcpy(g->cart_ram, data[i], size);
        } else {
            memcpy((uint8_t *)g + chunks[i].offset, data[i], size);
        }
    }
    gb_cart_remap(g); /* where the banks are is worked out from the registers, never taken from the file */
    return GB_OK;
}

gb_err_t gb_state_load(gb_t *g, const void *buf, size_t len) {
    gb_err_t err = state_load(g, buf, len);
    if (err == GB_OK) rewind_ring_clear(g->rewind);
    return err;
}

gb_err_t gb_rewind_configure(gb_t *g, size_t budget_bytes, int interval_frames) {
    rewind_ring_free(g->rewind);
    g->rewind = NULL;
    g->rewind_budget = budget_bytes;
    g->rewind_interval = interval_frames;
    if (budget_bytes == 0 || interval_frames < 1) return GB_OK;
    g->rewind = rewind_ring_new(gb_state_size(g), budget_bytes, interval_frames);
    return g->rewind ? GB_OK : GB_ERR_NOMEM;
}

void gb_rewind_after_frame(gb_t *g) {
    uint8_t *snapshot = rewind_ring_begin(g->rewind);
    if (!snapshot) return;
    gb_state_save(g, snapshot, gb_state_size(g));
    rewind_ring_commit(g->rewind);
}

int gb_rewind_step_back(gb_t *g) {
    const uint8_t *snapshot = rewind_ring_back(g->rewind);
    if (!snapshot) return 0;
    if (state_load(g, snapshot, gb_state_size(g)) != GB_OK) {
        rewind_ring_clear(g->rewind);
        return 0;
    }
    return 1;
}
