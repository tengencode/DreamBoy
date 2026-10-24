/* The built-in BIOS's sound helpers (milestone 13: a game asked for MidiKey2Freq thousands of times). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bios/hle.h"
#include "test.h"
#include "testutil.h"

/* A machine stopped at "swi 0x1F; b ." with WaveData at 0x03000000 (its frequency at +4) and r1/r2 set. */
static gba_t *midi_machine(uint32_t freq, uint32_t key, uint32_t fine) {
    const uint32_t ops[] = {0xEF1F0000, 0xEAFFFFFE}; /* swi 0x1F; b . */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    bus_write32(g, 0x03000004, freq, false);
    g->cpu.r[0] = 0x03000000;
    g->cpu.r[1] = key;
    g->cpu.r[2] = fine;
    return g;
}

/* The whole file, or NULL. */
static uint8_t *read_all(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    uint8_t *buf = malloc(0x10000);
    *size = buf ? fread(buf, 1, 0x10000, f) : 0;
    fclose(f);
    return buf;
}

static uint32_t hle_midi(uint32_t freq, uint32_t key, uint32_t fine) {
    gba_t *g = midi_machine(freq, key, fine);
    hle_swi(g, 0x1F);
    uint32_t r0 = g->cpu.r[0];
    gba_destroy(g);
    return r0;
}

TEST(hle_midikey2freq_halves_the_frequency_per_octave_below_key_180) {
    CHECK_EQ(hle_midi(0x00D10C00, 168, 0), 0x00D10C00 / 2); /* 13379 Hz << 10, one octave down */
    CHECK_EQ(hle_midi(0x00D10C00, 156, 0), 0x00D10C00 / 4);
    CHECK_EQ(hle_midi(0x00D10C00, 60, 0), 0x00D10C00 >> 10); /* middle C: ten octaves down */
    uint32_t c = hle_midi(0x00D10C00, 168, 0), cs = hle_midi(0x00D10C00, 169, 0), mid = hle_midi(0x00D10C00, 168, 128);
    CHECK(c < mid && mid < cs); /* the fine adjust goes between two keys */
}

/* With the user's own BIOS (tests/bios/gba_bios.bin, never committed; skipped without it): the same answers as the
 * real routine, run from a machine whose BIOS area holds the real BIOS after the built-in start-up. */
TEST(hle_midikey2freq_matches_the_real_bios) {
    size_t size;
    uint8_t *bios = read_all(GBA_TEST_BIOS, &size);
    if (!bios || size != 0x4000) {
        printf("  (skipped: no tests/bios/gba_bios.bin)\n");
        free(bios);
        return;
    }
    static const uint32_t freqs[] = {0x00D10C00, 44100u << 10, 8000u << 10, 0x7FFFFFFF, 1};
    static const uint32_t fines[] = {0, 1, 64, 128, 255};
    int mismatches = 0;
    for (size_t f = 0; f < TU_LEN(freqs); f++)
        for (uint32_t key = 0; key < 200; key += 3)
            for (size_t k = 0; k < TU_LEN(fines); k++) {
                gba_t *g = midi_machine(freqs[f], key, fines[k]);
                memcpy(g->bus.bios, bios, 0x4000);
                g->hle_bios = false;
                for (int i = 0; i < 5000 && g->cpu.r[15] != 0x08000004; i++) cpu_step(g); /* until "b ." */
                uint32_t want = g->cpu.r[0];
                gba_destroy(g);
                uint32_t got = hle_midi(freqs[f], key, fines[k]);
                if (got != want && mismatches++ < 5)
                    printf("  freq %08x key %u fine %u: built-in %08x, BIOS %08x\n", freqs[f], key, fines[k], got,
                           want);
            }
    CHECK_EQ(mismatches, 0);
    free(bios);
}
