/*
 * Headless ROM runner.
 *   testrunner <rom.gba> [--bios <file>] [--frames <n>] [--result-reg <n>] [--save <file.sav>]
 *              [--screenshot <file.png>] [--hash] [--expect-hash <16 hex digits>]
 *              [--wav <file.wav>] [--audio-hash] [--expect-audio-hash <16 hex digits>]
 *              [--keys <frame:KEY[+KEY],...>] [--suite <group> [--suite-min <n>]] [--state-at <frame>]
 *              [--rewind-at <frame>:<steps>] [--load-state <file>] [--write-state <file>] [--cheat <file>]...
 *              [--shots-every <n> --shots-dir <dir>] [--notes]
 * With --result-reg: run until the CPU spins on a branch-to-self (or <n> frames, default 600);
 * register n == 0 means PASS (jsmolka gba-tests keep the first failing test number there).
 * Otherwise with --frames: run exactly <n> frames. Otherwise: run until idle (at most 600 frames).
 * The sound of every frame is collected: --wav saves it, --audio-hash fingerprints it.
 * --save loads the file into the save chip first (when it exists) and writes the chip back after.
 * --rtc <unix seconds> sets the cartridge clock; --hardware <rtc,solar,gyro,rumble,tilt|none> forces
 * the cartridge extras.
 * --keys holds buttons from the given frames on: "10:DOWN,12:-,14:A+B" (- = none). Key names: A B
 * SELECT START RIGHT LEFT UP DOWN R L; CW / CCW turn the rotation sensor while held, and TILT_LEFT / TILT_RIGHT /
 * TILT_UP / TILT_DOWN tip a tilt cartridge. --keys @file reads the script from a file (any length).
 * --shots-every n (with --frames) writes a PNG every n frames into --shots-dir as <frame>.png (6 digits).
 * --notes prints the warning log as "NOTE <kind> <detail hex> <first frame> <count>" lines, and the game's own
 * debug messages (mGBA's print registers) as "PRINT <level> <first frame> <count> <text>" (GBA_NOTES builds only).
 * --suite runs one group of the mGBA test suite (suite.gba): memory io timing timer-countup timer-irq
 * shifter carry multiply-long bios-math dma sio-regs sio-timing misc. It prints "SUITE <group> p/t";
 * with --suite-min it fails when p is below n.
 * --state-at k (with --frames): after k frames, save a state, load it into a brand-new machine (same BIOS
 * and ROM) and run the rest there — the hashes then prove that a loaded state continues exactly.
 * --rewind-at f:s (with --frames): rewind on (64 MiB, a snapshot every 4 frames, as the app); after f frames,
 * step back s snapshots and run on from there to the end with the same keys - the hashes then prove that
 * rewinding is exact. The sound of the frames undone is taken out of the audio hash again.
 * --load-state starts from a save state (after the ROM, BIOS and save file). --write-state <file> saves a state of
 * the machine after the run. --cheat switches on the cheat in the file (up to 8): its codes, one per line, after
 * an optional first line "type: auto|ar|gs|cb|raw".
 * Game Boy / Game Boy Color ROMs run through the same options, except the Game Boy Advance-only ones (--bios,
 * --result-reg, --suite, --hardware, --cheat, --notes); without --frames they run 600 frames. Three ways to run
 * their test ROMs, each until the result shows or --frames (default 3600) pass:
 * --gb-serial <text>: PASS when the bytes sent on the serial port contain <text>, FAIL when they contain "Failed".
 * --gb-memtext <text>: the newer blargg ROMs, which write DE B0 61 to 0xA001 and their status to 0xA000 (0x80 while
 *   running, 0 = passed) and their text from 0xA004: PASS on status 0 with <text> in the text.
 * --gb-mooneye: until the game runs LD B,B; PASS when B C D E H L are 3 5 8 13 21 34.
 * Exit codes: 0 pass/done, 1 fail, 2 timeout, 3 usage/load error, 77 ROM missing (CTest skip).
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "file_util.h"
#include "frame_hash.h"
#include "gb.h"
#include "gb_debug.h"
#include "gba.h"
#include "gba_debug.h"
#include "key_script.h"
#include "machine.h"
#include "png.h"
#include "wav.h"

#define REWIND_BUDGET (64u * 1024 * 1024)
#define REWIND_EVERY 4
#define MAX_CHEATS 8

enum { EXIT_PASS = 0, EXIT_FAILED = 1, EXIT_TIMEOUT = 2, EXIT_USAGE = 3, EXIT_SKIP = 77 };


/* The buttons, the tilt sensor while a TILT_ key is held (either console) and the rotation sensor while CW / CCW
 * are (as gbabench); a sensor is only touched once a script has moved it. */
static void set_input(machine_t *m, uint16_t keys) {
    static bool turned, tilted;
    machine_set_keys(m, keys & 0x3FF);
    if (keys & KEY_TILT_ANY) tilted = true;
    if (tilted) {
        machine_set_tilt(m, (int16_t)(((keys & KEY_TILT_RIGHT) ? TILT_HELD : 0) - ((keys & KEY_TILT_LEFT) ? TILT_HELD : 0)),
                         (int16_t)(((keys & KEY_TILT_DOWN) ? TILT_HELD : 0) - ((keys & KEY_TILT_UP) ? TILT_HELD : 0)));
    }
    gba_t *g = machine_gba(m);
    if (!g) return;
    if (keys & (KEY_CW | KEY_CCW)) turned = true;
    if (turned) gba_set_gyro(g, (keys & KEY_CW) ? SPIN_RATE : (keys & KEY_CCW) ? -SPIN_RATE : 0);
}

static const char *const SUITE_GROUPS[] = {"memory", "io", "timing", "timer-countup", "timer-irq",
                                           "shifter", "carry", "multiply-long", "bios-math", "dma",
                                           "sio-regs", "sio-timing", "misc"};
enum { SUITE_GROUP_COUNT = sizeof SUITE_GROUPS / sizeof SUITE_GROUPS[0], SUITE_MAX_FRAMES = 3600 };

static int suite_group_index(const char *name) {
    for (int i = 0; i < SUITE_GROUP_COUNT; i++)
        if (!strcmp(name, SUITE_GROUPS[i])) return i;
    return -1;
}

/* The suite draws its text as BG1 tiles (screen block 1 at 0x06000800, 32 per row) numbered
 * character - ' '. A finished group shows "%4u/%-4u" on row 1, columns 21-29. */
static bool read_suite_score(gba_t *g, unsigned *passed, unsigned *total) {
    char text[10];
    for (int col = 0; col < 9; col++) {
        uint32_t addr = 0x06000800u + (uint32_t)(32 + 21 + col) * 2;
        uint32_t word = gba_debug_peek32(g, addr);
        uint16_t entry = (addr & 2) ? (uint16_t)(word >> 16) : (uint16_t)word;
        text[col] = (char)((entry & 0x3FF) + ' ');
    }
    text[9] = '\0';
    return sscanf(text, "%u/%u", passed, total) == 2;
}

typedef struct {
    uint64_t hash;
    wav_writer_t wav;
    bool writing;
} audio_out_t;

static bool at_idle_loop(gba_t *g) {
    uint32_t pc = gba_debug_reg(g, 15);
    if (gba_debug_cpsr(g) & (1u << 5)) { /* Thumb: b . */
        uint32_t word = gba_debug_peek32(g, pc & ~3u);
        uint16_t op = (pc & 2) ? (uint16_t)(word >> 16) : (uint16_t)word;
        return op == 0xE7FE;
    }
    return gba_debug_peek32(g, pc) == 0xEAFFFFFE; /* ARM: b . */
}

/* One frame, then collect every sound sample it made. */
static void run_frame(machine_t *m, audio_out_t *audio) {
    int16_t buf[1024 * 2];
    size_t n;
    machine_run_frame(m);
    while ((n = machine_audio_read(m, buf, 1024)) > 0) {
        audio->hash = audio_hash_update(audio->hash, buf, n * 2);
        if (audio->writing) wav_write(&audio->wav, buf, n);
    }
}

/* Opens menu entry `group` (DOWN presses, then A) and runs until the result screen shows a score. */
static int run_suite(machine_t *m, int group, long min_score, audio_out_t *audio) {
    gba_t *g = machine_gba(m);
    key_event_t ev[2 * SUITE_GROUP_COUNT];
    key_script_t ks = {ev, 0, 2 * SUITE_GROUP_COUNT};
    for (int i = 0; i <= group; i++) {
        long at = 10 + 4L * i;
        ks.ev[ks.count++] = (key_event_t){at, i < group ? GBA_KEY_DOWN : GBA_KEY_A};
        ks.ev[ks.count++] = (key_event_t){at + 2, 0};
    }
    long pressed_a = 10 + 4L * group;
    unsigned passed = 0, total = 0;
    for (long frame = 0; frame < SUITE_MAX_FRAMES; frame++) {
        gba_set_keys(g, key_script_at(&ks, frame));
        run_frame(m, audio);
        if (frame > pressed_a + 2 && read_suite_score(g, &passed, &total)) {
            printf("SUITE %s %u/%u\n", SUITE_GROUPS[group], passed, total);
            if (min_score < 0 || (long)passed >= min_score) return EXIT_PASS;
            printf("FAIL suite %s: below the target of %ld\n", SUITE_GROUPS[group], min_score);
            return EXIT_FAILED;
        }
    }
    printf("TIMEOUT suite %s: no score after %d frames\n", SUITE_GROUPS[group], SUITE_MAX_FRAMES);
    return EXIT_TIMEOUT;
}

/* Prints PASS/FAIL for one fingerprint; true when it matches. */
static bool check_hash(const char *rom, const char *what, uint64_t actual, const char *expected) {
    uint64_t want = strtoull(expected, NULL, 16);
    if (actual == want) {
        printf("PASS %s (%s %016" PRIx64 ")\n", rom, what, actual);
        return true;
    }
    printf("FAIL %s: %s %016" PRIx64 ", expected %s\n", rom, what, actual, expected);
    return false;
}

/* "rtc,gyro" → GBA_HW_RTC | GBA_HW_GYRO; "none" → 0. False for an unknown name. */
static bool parse_hardware(const char *list, uint32_t *flags) {
    static const struct {
        const char *name;
        uint32_t flag;
    } names[] = {{"rtc", GBA_HW_RTC}, {"solar", GBA_HW_SOLAR}, {"gyro", GBA_HW_GYRO},
                 {"rumble", GBA_HW_RUMBLE}, {"tilt", GBA_HW_TILT}};
    char buf[128];
    *flags = 0;
    if (!strcmp(list, "none")) return true;
    snprintf(buf, sizeof buf, "%s", list);
    for (char *tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
        size_t k = 0;
        while (k < sizeof names / sizeof names[0] && strcmp(tok, names[k].name) != 0) k++;
        if (k == sizeof names / sizeof names[0]) return false;
        *flags |= names[k].flag;
    }
    return true;
}

/* Save a state of *m, load it into a new machine with the same BIOS and ROM, and carry on with that one. */
static bool switch_machine(machine_t **m, const uint8_t *bios, size_t bios_size, const uint8_t *rom, size_t rom_size) {
    size_t size = machine_state_size(*m);
    uint8_t *state = malloc(size ? size : 1);
    machine_t *fresh = machine_create(machine_kind(*m));
    bool ok = state && fresh && size && machine_state_save(*m, state, size) == size &&
              (!bios || gba_load_bios(machine_gba(fresh), bios, bios_size) == GBA_OK) &&
              machine_load_rom(fresh, rom, rom_size) == MACHINE_OK &&
              machine_state_load(fresh, state, size) == MACHINE_OK;
    free(state);
    if (!ok) {
        machine_destroy(fresh);
        return false;
    }
    machine_destroy(*m);
    *m = fresh;
    return true;
}

enum { GB_TEST_NONE, GB_TEST_SERIAL, GB_TEST_MEMTEXT, GB_TEST_MOONEYE };

/* Runs a Game Boy test ROM until it reports (see the top of this file). */
static int run_gb_test(machine_t *m, int mode, const char *expect, long frames, audio_out_t *audio, const char *rom) {
    gb_t *g = machine_gb(m);
    static char text[4097];
    for (long frame = 0; frame < frames; frame++) {
        run_frame(m, audio);
        if (mode == GB_TEST_SERIAL) {
            gb_debug_serial(g, text, sizeof text);
            if (strstr(text, expect)) {
                printf("PASS %s\n", rom);
                return EXIT_PASS;
            }
            if (strstr(text, "Failed")) {
                printf("FAIL %s:\n%s\n", rom, text);
                return EXIT_FAILED;
            }
        } else if (mode == GB_TEST_MEMTEXT) {
            if (gb_debug_peek(g, 0xA001) != 0xDE || gb_debug_peek(g, 0xA002) != 0xB0 || gb_debug_peek(g, 0xA003) != 0x61 ||
                gb_debug_peek(g, 0xA000) == 0x80)
                continue;
            size_t n = 0;
            for (uint8_t c; n < 1024 && (c = gb_debug_peek(g, (uint16_t)(0xA004 + n))) != 0; n++) text[n] = (char)c;
            text[n] = 0;
            if (gb_debug_peek(g, 0xA000) == 0 && strstr(text, expect)) {
                printf("PASS %s\n", rom);
                return EXIT_PASS;
            }
            printf("FAIL %s (status %u):\n%s\n", rom, (unsigned)gb_debug_peek(g, 0xA000), text);
            return EXIT_FAILED;
        } else if (gb_debug_breakpoint(g)) {
            uint16_t bc = gb_debug_reg(g, GB_REG_BC), de = gb_debug_reg(g, GB_REG_DE), hl = gb_debug_reg(g, GB_REG_HL);
            if (bc == 0x0305 && de == 0x080D && hl == 0x1522) {
                printf("PASS %s\n", rom);
                return EXIT_PASS;
            }
            printf("FAIL %s: BC=%04X DE=%04X HL=%04X\n", rom, bc, de, hl);
            return EXIT_FAILED;
        }
    }
    if (mode == GB_TEST_SERIAL) {
        gb_debug_serial(g, text, sizeof text);
        printf("TIMEOUT %s after %ld frames; serial output:\n%s\n", rom, frames, text);
    } else {
        printf("TIMEOUT %s after %ld frames (pc=%04X)\n", rom, frames, gb_debug_reg(g, GB_REG_PC));
    }
    return EXIT_TIMEOUT;
}

/* A cheat file: an optional "type: auto|ar|gs|cb|raw" line, then the codes. */
static bool set_cheats(gba_t *g, const char *const *paths, int count) {
    static const struct {
        const char *name;
        gba_cheat_type_t type;
    } names[] = {{"auto", GBA_CHEAT_AUTO}, {"ar", GBA_CHEAT_ACTION_REPLAY}, {"gs", GBA_CHEAT_GAMESHARK},
                 {"cb", GBA_CHEAT_CODEBREAKER}, {"raw", GBA_CHEAT_RAW}};
    char *texts[MAX_CHEATS];
    const char *codes[MAX_CHEATS];
    gba_cheat_type_t types[MAX_CHEATS];
    bool ok = true;
    int n = 0;
    for (; n < count; n++) {
        size_t size;
        uint8_t *data = read_file(paths[n], &size);
        texts[n] = malloc(size + 1);
        if (!data || !texts[n]) {
            fprintf(stderr, "cannot read the cheat file %s\n", paths[n]);
            free(data);
            n++;
            ok = false;
            break;
        }
        memcpy(texts[n], data, size);
        texts[n][size] = '\0';
        free(data);
        codes[n] = texts[n];
        types[n] = GBA_CHEAT_AUTO;
        if (!strncmp(texts[n], "type:", 5)) {
            const char *t = texts[n] + 5;
            while (*t == ' ') t++;
            for (size_t k = 0; k < sizeof names / sizeof names[0]; k++) {
                if (!strncmp(t, names[k].name, strlen(names[k].name))) types[n] = names[k].type;
            }
            const char *nl = strchr(texts[n], '\n');
            codes[n] = nl ? nl + 1 : "";
        }
        int bad = 0;
        gba_cheat_type_t used = gba_cheat_check(codes[n], types[n], &bad);
        if (bad) {
            fprintf(stderr, "%s: line %d is not a code this runner can read\n", paths[n], bad);
            n++;
            ok = false;
            break;
        }
        static const char *const type_names[] = {"auto", "Action Replay v3", "GameShark v1/v2", "CodeBreaker", "address:value"};
        printf("CHEAT %s (%s)\n", paths[n], type_names[used]);
    }
    if (ok && gba_cheats_set(g, codes, types, (size_t)count) != GBA_OK) ok = false;
    for (int i = 0; i < n; i++) free(texts[i]);
    return ok;
}

/* A PNG of the current picture as <dir>/<frame, 6 digits>.png. */
static bool write_shot(machine_t *m, const char *dir, long frame) {
    char path[1024];
    snprintf(path, sizeof path, "%s/%06ld.png", dir, frame);
    if (png_write_bgr555(path, machine_framebuffer(m), machine_width(m), machine_height(m)) != 0) {
        fprintf(stderr, "cannot write %s\n", path);
        return false;
    }
    return true;
}

static int usage(void) {
    fprintf(stderr, "usage: testrunner <rom.gba> [--bios <file>] [--frames <n>] [--result-reg <n>] [--save <file>]\n"
                    "                  [--screenshot <file.png>] [--hash] [--expect-hash <hex>]\n"
                    "                  [--wav <file.wav>] [--audio-hash] [--expect-audio-hash <hex>]\n"
                    "                  [--rtc <unix seconds>] [--hardware <rtc,solar,gyro,rumble,tilt|none>]\n"
                    "                  [--keys <frame:KEY[+KEY],...>] [--suite <group> [--suite-min <n>]]\n"
                    "                  [--state-at <frame>] [--rewind-at <frame>:<steps>]\n"
                    "                  [--load-state <file>] [--write-state <file>] [--cheat <file>]...\n"
                    "                  [--shots-every <n> --shots-dir <dir>] [--notes] [--keys @<file>]\n"
                    "                  [--gb-serial <text> | --gb-memtext <text> | --gb-mooneye]\n");
    return EXIT_USAGE;
}

int main(int argc, char **argv) {
    const char *rom_path = NULL, *bios_path = NULL, *shot_path = NULL, *expect_hash = NULL;
    const char *wav_path = NULL, *expect_audio = NULL, *save_path = NULL;
    const char *hardware = NULL;
    const char *load_state = NULL;
    const char *write_state = NULL;
    const char *cheat_paths[MAX_CHEATS];
    int cheat_count = 0;
    const char *keys_arg = NULL, *suite_arg = NULL;
    long suite_min = -1;
    long state_at = -1;
    long rewind_at = -1, rewind_steps = 0;
    bool rtc_given = false;
    long long rtc_seconds = 0;
    long frames = 600;
    bool frames_given = false, print_hash = false, print_audio = false, print_notes = false;
    const char *shots_dir = NULL;
    long shots_every = 0;
    char *keys_text = NULL;
    int result_reg = -1;
    int gb_test = GB_TEST_NONE;
    const char *gb_expect = "";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--bios") && i + 1 < argc) bios_path = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
            frames = strtol(argv[++i], NULL, 0);
            frames_given = true;
        } else if (!strcmp(argv[i], "--result-reg") && i + 1 < argc) result_reg = (int)strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) shot_path = argv[++i];
        else if (!strcmp(argv[i], "--hash")) print_hash = true;
        else if (!strcmp(argv[i], "--expect-hash") && i + 1 < argc) expect_hash = argv[++i];
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc) wav_path = argv[++i];
        else if (!strcmp(argv[i], "--audio-hash")) print_audio = true;
        else if (!strcmp(argv[i], "--expect-audio-hash") && i + 1 < argc) expect_audio = argv[++i];
        else if (!strcmp(argv[i], "--save") && i + 1 < argc) save_path = argv[++i];
        else if (!strcmp(argv[i], "--rtc") && i + 1 < argc) {
            rtc_seconds = strtoll(argv[++i], NULL, 0);
            rtc_given = true;
        } else if (!strcmp(argv[i], "--hardware") && i + 1 < argc) hardware = argv[++i];
        else if (!strcmp(argv[i], "--keys") && i + 1 < argc) keys_arg = argv[++i];
        else if (!strcmp(argv[i], "--suite") && i + 1 < argc) suite_arg = argv[++i];
        else if (!strcmp(argv[i], "--suite-min") && i + 1 < argc) suite_min = strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--state-at") && i + 1 < argc) state_at = strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--load-state") && i + 1 < argc) load_state = argv[++i];
        else if (!strcmp(argv[i], "--write-state") && i + 1 < argc) write_state = argv[++i];
        else if (!strcmp(argv[i], "--shots-every") && i + 1 < argc) shots_every = strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--shots-dir") && i + 1 < argc) shots_dir = argv[++i];
        else if (!strcmp(argv[i], "--notes")) print_notes = true;
        else if (!strcmp(argv[i], "--gb-serial") && i + 1 < argc) gb_test = GB_TEST_SERIAL, gb_expect = argv[++i];
        else if (!strcmp(argv[i], "--gb-memtext") && i + 1 < argc) gb_test = GB_TEST_MEMTEXT, gb_expect = argv[++i];
        else if (!strcmp(argv[i], "--gb-mooneye")) gb_test = GB_TEST_MOONEYE;
        else if (!strcmp(argv[i], "--cheat") && i + 1 < argc && cheat_count < MAX_CHEATS) cheat_paths[cheat_count++] = argv[++i];
        else if (!strcmp(argv[i], "--rewind-at") && i + 1 < argc) {
            char *end;
            rewind_at = strtol(argv[++i], &end, 10);
            if (*end != ':') return usage();
            rewind_steps = strtol(end + 1, NULL, 10);
        }
        else if (argv[i][0] != '-' && !rom_path) rom_path = argv[i];
        else return usage();
    }
    uint32_t hw_flags = 0;
    if (!rom_path || result_reg > 15 || frames <= 0) return usage();
    if (state_at >= 0 && (!frames_given || result_reg >= 0 || state_at > frames)) return usage();
    if (hardware && !parse_hardware(hardware, &hw_flags)) return usage();
    /* The snapshot the steps reach: snapshots are taken after frames 4, 8, ...; from between two, the first
     * step goes to the newer one. */
    long rewind_to = rewind_at - rewind_at % REWIND_EVERY -
                     REWIND_EVERY * (rewind_steps - (rewind_at % REWIND_EVERY ? 1 : 0));
    if (rewind_at >= 0 && (!frames_given || result_reg >= 0 || state_at >= 0 || wav_path || rewind_at > frames ||
                           rewind_steps < 1 || rewind_to < REWIND_EVERY)) {
        return usage();
    }
    if (shots_every < 0 || (shots_every > 0 && (!shots_dir || !frames_given || result_reg >= 0 || rewind_at >= 0)))
        return usage();
    if (print_notes && !gba_debug_notes_enabled()) {
        fprintf(stderr, "--notes: this testrunner was built without the warning log (GBA_NOTES)\n");
        return EXIT_USAGE;
    }
    key_script_t keys = {NULL, 0, 0};
    if (keys_arg && keys_arg[0] == '@') { /* a long script from a file (the game check's) */
        size_t n;
        uint8_t *data = read_file(keys_arg + 1, &n);
        if (!data || !(keys_text = malloc(n + 1))) {
            fprintf(stderr, "cannot read the key script %s\n", keys_arg + 1);
            free(data);
            return EXIT_USAGE;
        }
        memcpy(keys_text, data, n);
        while (n && (keys_text[n - 1] == '\n' || keys_text[n - 1] == '\r' || keys_text[n - 1] == ' ')) n--;
        keys_text[n] = '\0';
        free(data);
        keys_arg = keys_text;
    }
    if (keys_arg && !key_script_parse(keys_arg, &keys)) {
        free(keys.ev);
        free(keys_text);
        return usage();
    }
    int suite_group = -1;
    if (suite_arg && (suite_group = suite_group_index(suite_arg)) < 0) return usage();

    size_t rom_size;
    uint8_t *rom = read_file(rom_path, &rom_size);
    if (!rom) {
        printf("SKIP %s (not found; run tools/fetch_test_roms.py)\n", rom_path);
        free(keys.ev);
        free(keys_text);
        return EXIT_SKIP;
    }

    machine_t *m = machine_create(machine_detect(rom, rom_size, rom_path));
    gba_t *g = m ? machine_gba(m) : NULL; /* NULL: a Game Boy game */
    audio_out_t audio = {AUDIO_HASH_INIT, {NULL, 0, 0}, false};
    int code = EXIT_USAGE;
    if (!m) {
        fprintf(stderr, "%s: not a Game Boy, Game Boy Color or Game Boy Advance ROM\n", rom_path);
        goto done;
    }
    if (!g && (bios_path || result_reg >= 0 || suite_group >= 0 || hardware || cheat_count || print_notes)) {
        fprintf(stderr, "%s: --bios, --result-reg, --suite, --hardware, --cheat and --notes are for Game Boy Advance ROMs\n",
                rom_path);
        goto done;
    }
    if (gb_test != GB_TEST_NONE && (g || state_at >= 0 || rewind_at >= 0 || shots_every > 0)) {
        fprintf(stderr, "%s: the --gb-... test options are for Game Boy ROMs, without --state-at, --rewind-at or --shots-every\n",
                rom_path);
        goto done;
    }
    if (gb_test != GB_TEST_NONE && !frames_given) frames = 3600;
    if (!g) frames_given = true; /* no idle-loop detection for the Game Boy: always a fixed number of frames */
    uint8_t *bios = NULL; /* kept for --state-at's new machine */
    size_t bios_size = 0;
    if (bios_path) {
        bios = read_file(bios_path, &bios_size);
        gba_err_t err = bios ? gba_load_bios(g, bios, bios_size) : GBA_ERR_BIOS_SIZE;
        if (err != GBA_OK) {
            fprintf(stderr, "cannot load BIOS %s\n", bios_path);
            goto done;
        }
    }
    int load_err = machine_load_rom(m, rom, rom_size);
    if (load_err != MACHINE_OK) {
        fprintf(stderr, "cannot load ROM %s%s\n", rom_path,
                load_err == MACHINE_ERR_UNSUPPORTED ? ": a cartridge chip this emulator does not have" : "");
        goto done;
    }
    if (hardware) gba_set_cart_hardware(g, hw_flags);
    if (rtc_given) machine_set_rtc(m, rtc_seconds, 0);
    if (save_path) {
        size_t save_size;
        uint8_t *save = read_file(save_path, &save_size);
        if (save) {
            int err = machine_save_read(m, save, save_size);
            free(save);
            if (err != MACHINE_OK) {
                fprintf(stderr, "cannot use %s: wrong size for this game's save chip\n", save_path);
                goto done;
            }
        }
    }
    if (load_state) {
        size_t state_size;
        uint8_t *state = read_file(load_state, &state_size);
        int err = state ? machine_state_load(m, state, state_size) : MACHINE_ERR_STATE;
        free(state);
        if (err != MACHINE_OK) {
            fprintf(stderr, "cannot load the state %s (another game, another version, or damaged)\n", load_state);
            goto done;
        }
    }
    if (cheat_count && !set_cheats(g, cheat_paths, cheat_count)) goto done;
    if (wav_path) {
        if (wav_open(&audio.wav, wav_path, MACHINE_AUDIO_RATE) != 0) {
            fprintf(stderr, "cannot write %s\n", wav_path);
            goto done;
        }
        audio.writing = true;
    }

    bool exact = frames_given && result_reg < 0;
    long frame = 0;
    if (suite_group >= 0) {
        code = run_suite(m, suite_group, suite_min, &audio);
    } else if (gb_test != GB_TEST_NONE) {
        code = run_gb_test(m, gb_test, gb_expect, frames, &audio, rom_path);
    } else {
        if (exact) {
            uint64_t *hash_before = NULL; /* the audio hash before each frame's sound was read */
            if (rewind_at >= 0) {
                hash_before = malloc(sizeof(uint64_t) * (size_t)frames);
                if (!hash_before || machine_rewind_configure(m, REWIND_BUDGET, REWIND_EVERY) != MACHINE_OK) {
                    fprintf(stderr, "no memory for rewind\n");
                    free(hash_before);
                    goto done;
                }
            }
            bool rewound = false;
            for (; frame < frames; frame++) {
                if (frame == state_at) {
                    if (!switch_machine(&m, bios, bios_size, rom, rom_size)) {
                        printf("FAIL %s: the state saved at frame %ld did not load\n", rom_path, frame);
                        code = EXIT_FAILED;
                        goto done;
                    }
                    g = machine_gba(m);
                }
                if (frame == rewind_at && !rewound) {
                    for (long s = 0; s < rewind_steps; s++) {
                        if (!machine_rewind_step_back(m)) {
                            printf("FAIL %s: rewind step %ld of %ld found no snapshot\n", rom_path, s + 1, rewind_steps);
                            free(hash_before);
                            code = EXIT_FAILED;
                            goto done;
                        }
                    }
                    /* The snapshot came after frame rewind_to - 1 ran, its sound not read yet: that is read next. */
                    frame = rewind_to;
                    audio.hash = hash_before[rewind_to - 1];
                    rewound = true;
                }
                if (hash_before) hash_before[frame] = audio.hash;
                set_input(m, key_script_at(&keys, frame));
                run_frame(m, &audio);
                if (shots_every > 0 && (frame + 1) % shots_every == 0 && !write_shot(m, shots_dir, frame + 1)) {
                    free(hash_before);
                    code = EXIT_USAGE;
                    goto done;
                }
            }
            free(hash_before);
        } else {
            while (frame < frames && !at_idle_loop(g)) {
                set_input(m, key_script_at(&keys, frame));
                run_frame(m, &audio);
                frame++;
            }
        }

        code = EXIT_PASS;
        if (!exact && !at_idle_loop(g)) {
            printf("TIMEOUT %s after %ld frames (pc=%08X)\n", rom_path, frames, (unsigned)gba_debug_reg(g, 15));
            code = EXIT_TIMEOUT;
        } else if (result_reg >= 0) {
            uint32_t result = gba_debug_reg(g, (unsigned)result_reg);
            if (result == 0) {
                printf("PASS %s\n", rom_path);
            } else {
                printf("FAIL %s: test %u\n", rom_path, (unsigned)result);
                code = EXIT_FAILED;
            }
        } else if (!exact) {
            printf("DONE %s after %ld frames\n", rom_path, frame);
        }
    }

    const uint16_t *fb = machine_framebuffer(m);
    uint64_t hash = frame_hash(fb, (size_t)machine_width(m) * (size_t)machine_height(m));
    if (print_hash) printf("HASH %016" PRIx64 "\n", hash);
    if (print_audio) printf("AUDIO %016" PRIx64 "\n", audio.hash);
    if (print_notes) {
        gba_note_t notes[GBA_DEBUG_NOTES_MAX];
        size_t n = gba_debug_notes(g, notes, GBA_DEBUG_NOTES_MAX);
        for (size_t i = 0; i < n && i < GBA_DEBUG_NOTES_MAX; i++)
            printf("NOTE %s %08" PRIx32 " %" PRIu32 " %" PRIu32 "\n", notes[i].kind, notes[i].detail,
                   notes[i].first_frame, notes[i].count);
        gba_print_t prints[GBA_DEBUG_PRINTS_MAX];
        n = gba_debug_prints(g, prints, GBA_DEBUG_PRINTS_MAX);
        for (size_t i = 0; i < n && i < GBA_DEBUG_PRINTS_MAX; i++)
            printf("PRINT %" PRIu32 " %" PRIu32 " %" PRIu32 " %s\n", prints[i].level, prints[i].first_frame,
                   prints[i].count, prints[i].text);
    }
    if (shot_path && png_write_bgr555(shot_path, fb, machine_width(m), machine_height(m)) != 0) {
        fprintf(stderr, "cannot write %s\n", shot_path);
        code = EXIT_USAGE;
    }
    if (expect_hash && code == EXIT_PASS && !check_hash(rom_path, "frame", hash, expect_hash)) code = EXIT_FAILED;
    if (expect_audio && code == EXIT_PASS && !check_hash(rom_path, "audio", audio.hash, expect_audio)) {
        code = EXIT_FAILED;
    }

    size_t save_bytes = machine_save_size(m);
    if (save_path && save_bytes) {
        uint8_t *save = malloc(save_bytes);
        if (!save || machine_save_write(m, save, save_bytes) != save_bytes || write_file(save_path, save, save_bytes) != 0) {
            fprintf(stderr, "cannot write %s\n", save_path);
            code = EXIT_USAGE;
        }
        free(save);
    }
    if (write_state && code == EXIT_PASS) {
        size_t size = machine_state_size(m);
        uint8_t *state = malloc(size ? size : 1);
        if (!state || !size || machine_state_save(m, state, size) != size || write_file(write_state, state, size) != 0) {
            fprintf(stderr, "cannot write the state %s\n", write_state);
            code = EXIT_USAGE;
        }
        free(state);
    }

done:
    if (audio.writing && wav_close(&audio.wav) != 0) {
        fprintf(stderr, "cannot write %s\n", wav_path);
        code = EXIT_USAGE;
    }
    machine_destroy(m);
    free(bios);
    free(rom);
    free(keys.ev);
    free(keys_text);
    return code;
}
