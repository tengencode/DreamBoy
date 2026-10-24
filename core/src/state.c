/* Save states: the whole machine, one chunk per component holding that component's struct as bytes.
 *
 * Layout (little-endian hosts): header "GBAS", format version, ROM CRC-32, chunk count; then per chunk
 * a 4-letter tag, a version and a length, followed by the bytes. A state loads only into the same ROM
 * and only when every chunk's tag, version and length match this build: everything is checked first,
 * so a refused state leaves the machine untouched. Bump a chunk's version by hand when the meaning of
 * its struct changes without its size changing.
 *
 * A state may come from a file someone else made (a shared state, a restored backup), so the values that
 * pick a place in memory or keep the frame going are checked too (values_ok); the ROM's size, CRC, title
 * and code always stay those of the loaded ROM, and the scheduler's cached next event and the memory
 * timing table (from WAITCNT) are worked out again. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "gba_internal.h"
#include "rewind.h"

#define STATE_FORMAT 1u
#define HEADER_SIZE 16u
#define CHUNK_HEAD 12u
#define MISC_CHUNK SIZE_MAX /* not a struct inside gba_t: the fields below */

/* The machine's own fields, in a fixed layout without padding. */
typedef struct {
    uint64_t now;
    uint16_t keyinput;
    uint8_t frame_done;
    uint8_t hle_bios;
    uint32_t reserved;
} misc_t;

typedef struct {
    char tag[4];
    uint32_t version;
    size_t offset; /* in struct gba; MISC_CHUNK for misc_t */
    size_t size;
} chunk_t;

static const chunk_t chunks[] = {
    {{'C', 'P', 'U', ' '}, 1, offsetof(struct gba, cpu), sizeof(arm_cpu_t)},
    {{'B', 'U', 'S', ' '}, 1, offsetof(struct gba, bus), sizeof(bus_t)},
    {{'I', 'O', ' ', ' '}, 1, offsetof(struct gba, io), sizeof(io_t)},
    {{'P', 'P', 'U', ' '}, 1, offsetof(struct gba, ppu), sizeof(ppu_t)},
    {{'I', 'R', 'Q', ' '}, 1, offsetof(struct gba, irq), sizeof(irq_t)},
    {{'T', 'M', 'R', ' '}, 1, offsetof(struct gba, timers), sizeof(timers_t)},
    {{'D', 'M', 'A', ' '}, 1, offsetof(struct gba, dma), sizeof(dma_t)},
    {{'A', 'P', 'U', ' '}, 1, offsetof(struct gba, apu), sizeof(apu_t)},
    {{'S', 'I', 'O', ' '}, 1, offsetof(struct gba, sio), sizeof(sio_t)},
    {{'S', 'C', 'H', 'D'}, 1, offsetof(struct gba, sched), sizeof(sched_t)},
    {{'C', 'A', 'R', 'T'}, 1, offsetof(struct gba, cart), sizeof(cart_t)}, /* the ROM pointer is never kept */
    {{'M', 'I', 'S', 'C'}, 1, MISC_CHUNK, sizeof(misc_t)},
};
#define CHUNK_COUNT (sizeof chunks / sizeof chunks[0])

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

static const uint8_t *chunk_data(const uint8_t *const data[], size_t offset) {
    for (size_t i = 0; i < CHUNK_COUNT; i++)
        if (chunks[i].offset == offset) return data[i];
    return NULL;
}

/* Every value that indexes memory or shifts by a count is in range, the next line end is due within a
 * frame (else the frame never ends) and no event is more than a frame overdue (else catching up never
 * ends). `data` holds each chunk's bytes, in the order of `chunks`. */
static bool values_ok(const uint8_t *const data[]) {
    const uint8_t *cart = chunk_data(data, offsetof(struct gba, cart));
    const uint8_t *apu = chunk_data(data, offsetof(struct gba, apu));
    const uint8_t *sched = chunk_data(data, offsetof(struct gba, sched));
    const uint8_t *misc = chunk_data(data, MISC_CHUNK);
    if (FIELD(cart, cart_t, save.type) > GBA_SAVE_EEPROM8K || FIELD(cart, cart_t, save.flash.bank) > 1 ||
        FIELD(cart, cart_t, save.eeprom.reply_pos) > 68 || FIELD(cart, cart_t, hw.rtc.bit_pos) > 7) {
        return false;
    }
    for (size_t n = 0; n < 4; n++) { /* square 1, square 2: a duty step 0..7; wave: a sample 0..63 */
        size_t at = offsetof(apu_t, psg.ch) + n * sizeof(psg_channel_t) + offsetof(psg_channel_t, pos);
        if (field_at(apu, at, 1) > (n == 2 ? 63u : 7u)) return false;
    }
    for (size_t f = 0; f < 2; f++) {
        size_t at = offsetof(apu_t, fifo) + f * sizeof(apu_fifo_t);
        if (field_at(apu, at + offsetof(apu_fifo_t, read), 1) > 31 || field_at(apu, at + offsetof(apu_fifo_t, count), 1) > 32) {
            return false;
        }
    }
    if (FIELD(apu, apu_t, ring_read) >= APU_RING_FRAMES || FIELD(apu, apu_t, ring_count) > APU_RING_FRAMES) return false;
    uint64_t now = FIELD(misc, misc_t, now);
    if (now > UINT64_MAX / 2 || FIELD(sched, sched_t, when[EV_LINE_END]) > now + PPU_FRAME_CYCLES) return false;
    for (size_t i = 0; i < EV_COUNT; i++) {
        uint64_t when = field_at(sched, offsetof(sched_t, when) + i * sizeof(uint64_t), sizeof(uint64_t));
        if (when != SCHED_NEVER && when + PPU_FRAME_CYCLES < now) return false;
    }
    return true;
}

size_t gba_state_size(const gba_t *g) {
    (void)g;
    size_t n = HEADER_SIZE;
    for (size_t i = 0; i < CHUNK_COUNT; i++) n += CHUNK_HEAD + chunks[i].size;
    return n;
}

size_t gba_state_save(const gba_t *g, void *buf, size_t cap) {
    size_t total = gba_state_size(g);
    if (!buf || cap < total) return 0;
    uint8_t *p = buf;
    memcpy(p, "GBAS", 4);
    put32(p + 4, STATE_FORMAT);
    put32(p + 8, g->cart.crc32);
    put32(p + 12, (uint32_t)CHUNK_COUNT);
    p += HEADER_SIZE;
    for (size_t i = 0; i < CHUNK_COUNT; i++) {
        const chunk_t *c = &chunks[i];
        memcpy(p, c->tag, 4);
        put32(p + 4, c->version);
        put32(p + 8, (uint32_t)c->size);
        p += CHUNK_HEAD;
        if (c->offset == MISC_CHUNK) {
            misc_t m;
            memset(&m, 0, sizeof m);
            m.now = g->now;
            m.keyinput = g->keyinput;
            m.frame_done = g->frame_done;
            m.hle_bios = g->hle_bios;
            memcpy(p, &m, sizeof m);
        } else {
            memcpy(p, (const uint8_t *)g + c->offset, c->size);
            if (c->offset == offsetof(struct gba, cart)) memset(p + offsetof(cart_t, rom), 0, sizeof(uint8_t *));
        }
        p += c->size;
    }
    return total;
}

gba_err_t gba_state_load(gba_t *g, const void *buf, size_t len) {
    gba_err_t err = state_load(g, buf, len);
    if (err == GBA_OK) rewind_clear(g->rewind);
    return err;
}

gba_err_t state_load(gba_t *g, const void *buf, size_t len) {
    const uint8_t *p = buf;
    if (!buf || len != gba_state_size(g)) return GBA_ERR_STATE;
    if (memcmp(p, "GBAS", 4) != 0 || get32(p + 4) != STATE_FORMAT || get32(p + 8) != g->cart.crc32 ||
        get32(p + 12) != CHUNK_COUNT) {
        return GBA_ERR_STATE;
    }
    const uint8_t *q = p + HEADER_SIZE;
    const uint8_t *data[CHUNK_COUNT];
    for (size_t i = 0; i < CHUNK_COUNT; i++) { /* check everything before changing anything */
        const chunk_t *c = &chunks[i];
        if (memcmp(q, c->tag, 4) != 0 || get32(q + 4) != c->version || get32(q + 8) != c->size) return GBA_ERR_STATE;
        /* made with the other kind of BIOS (built-in vs a BIOS file): its SWIs would run the wrong BIOS */
        if (c->offset == MISC_CHUNK && (q[CHUNK_HEAD + offsetof(misc_t, hle_bios)] != 0) != g->hle_bios) return GBA_ERR_STATE;
        data[i] = q + CHUNK_HEAD;
        q += CHUNK_HEAD + c->size;
    }
    if (!values_ok(data)) return GBA_ERR_STATE;
    q = p + HEADER_SIZE;
    for (size_t i = 0; i < CHUNK_COUNT; i++) {
        const chunk_t *c = &chunks[i];
        q += CHUNK_HEAD;
        if (c->offset == MISC_CHUNK) {
            misc_t m;
            memcpy(&m, q, sizeof m);
            g->now = m.now;
            g->keyinput = m.keyinput;
            g->frame_done = m.frame_done != 0;
            g->hle_bios = m.hle_bios != 0;
        } else if (c->offset == offsetof(struct gba, cart)) {
            uint8_t *rom = g->cart.rom; /* the loaded ROM stays; everything else comes from the state */
            uint32_t rom_size = g->cart.rom_size, crc32 = g->cart.crc32;
            char title[sizeof g->cart.title], code[sizeof g->cart.code];
            memcpy(title, g->cart.title, sizeof title);
            memcpy(code, g->cart.code, sizeof code);
            memcpy(&g->cart, q, sizeof(cart_t));
            g->cart.rom = rom;
            g->cart.rom_size = rom_size;
            g->cart.crc32 = crc32;
            memcpy(g->cart.title, title, sizeof title);
            memcpy(g->cart.code, code, sizeof code);
        } else {
            memcpy((uint8_t *)g + c->offset, q, c->size);
        }
        q += c->size;
    }
    sched_refresh(&g->sched);
    bus_rebuild_timing(g);
    return GBA_OK;
}
