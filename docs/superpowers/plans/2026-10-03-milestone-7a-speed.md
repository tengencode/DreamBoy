# Milestone 7a — Speed: Measuring + C Speed-ups — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. (This project always executes plans inline with superpowers:executing-plans — see `CLAUDE.md`.)

**Goal:** A speed tool (`gbabench`, 6 scenes, phone commands, a profiler), C speed-ups that change no result (drawing, instruction fetch, memory access, link-time optimisation on Android) and a Show speed label — every game scene at the agreed speed on phone B's small core.

**Architecture:** The core keeps its structure; only its hot paths get shortcuts — text backgrounds drawn a tile row at a time, the compositor looking at each layer once, unrotated bitmap lines read as one row, instruction fetches and RAM/ROM data accesses straight from memory, ARM conditions from a table. Exactness is proven three ways: unit tests comparing each fast path with a copy of the old code on random data, the 120 CTest fingerprints, and the 6 speed scenes' hashes (recorded from today's code). The Android builds add link-time optimisation. The app gets Settings → Picture → Show speed.

**Tech Stack:** C11 (core, tools), CMake/Ninja, NDK clang 27 (arm64 benchmark), Kotlin + Jetpack Compose, Git Bash `tools/dev.sh`, adb.

**Spec:** `docs/superpowers/specs/2026-10-02-milestone-7a-speed-design.md` (finish line in §1).

---

## Conventions (as 6c)

- Git Bash from the repo root. PC: `bash tools/dev.sh <configure|build|unit [filter]|test|run|release|bench [args]>` (a fresh worktree needs `configure` first). Android emulator: `bash tools/dev.sh <apk|jvmtest|emu|install|launch|androidtest>`. Gradle `--offline`; a missing artifact → stop and ask the user before any download.
- **The user's games** are read from `$GBA_GAMES` (default `~/Downloads`): `hack-1.gba` + `.sav`, `game-b.gba`, `game-c.gba`, `game-d.gba`. Never copy them into the repo or git.
- **The user's phones** (spec §4): only through `dev.sh`'s `phone-*` commands, which refuse unless exactly one USB phone is plugged in. 7a measures on the **phone B** (its serial, small cores cpu0-3, Cortex-A55 1.77 GHz). Allowed there: `gbabench`, `simpleperf`-free profiling and the games in `/data/local/tmp/gbabench`, installing the app and its test package. If phone B is not plugged in when a step needs it, stop and ask the user to plug it in. Ask before restarting or cold-booting the emulator.
- `adb shell run-as <pkg> sh -c "…"` does not work (adb re-splits the words): plain `run-as <pkg> <command>` calls.
- Test-first where a test can fail first; for a speed-up the tests are written first and **pass on today's code** (they pin today's results), and must still pass after it.
- One commit per task; messages end with the Co-Authored-By trailer. Each commit also includes `docs/PROGRESS.md` and this plan (ticked boxes) — `CLAUDE.md`. The tasks run one after another without stopping; the only stop is Task 8, the user's check. After every task: all documents updated and checked for anything stale.
- This plan's code is the prototype's (worktree `<scratchpad>/p7a`, branch `proto-7a`), which the user tried on phone B on 2026-10-03 ('Everything works').

## File map

```
tools/bench/main.c, tools/bench/CMakeLists.txt, tools/bench/profile_report.py (new); tests/bench/scenes.txt (new);
CMakeLists.txt; tools/dev.sh                                                                    Task 1
tests/unit/test_speedups.c (new)                                                                Task 2
core/src/ppu/ppu_bg.c, core/src/ppu/ppu_compose.c                                               Task 3
core/src/mem/bus.c, core/src/cpu/cpu.c                                                          Task 4
platform/android/CMakeLists.txt                                                                 Task 5
app/src/main/java/com/tengen/gbaemu/SpeedMeter.kt (new), Settings.kt, SettingsScreens.kt, Screens.kt, MainActivity.kt;
app/src/test/java/com/tengen/gbaemu/SpeedMeterTest.kt (new), SettingsTest.kt                                                    Task 6
app/src/androidTest/java/com/tengen/gbaemu/RewindTest.kt                                                                               Task 7
```

---
### Task 1: The speed tool

**Files:**
- Create: `tools/bench/main.c`, `tools/bench/CMakeLists.txt`, `tools/bench/profile_report.py`, `tests/bench/scenes.txt`
- Modify: `CMakeLists.txt` (gbabench on the PC; `GBA_BENCH_ONLY` for the phone build), `tools/dev.sh` (`bench`, `phone-bench`, `phone-profile`, `phone-androidtest`, `phone-clean`)

(A tool: its check is that today's code reproduces the scene hashes in `tests/bench/scenes.txt`, recorded from today's code, on the PC and on the phone.)

- [x] **Step 1: Write the files**

Create `tools/bench/main.c`:

```c
/*
 * Speed benchmark: plays the scenes of a scene list and times them.
 *   gbabench [--scenes <file>] [--roms <dir>] [--runs <n>] [--only <scene>] [--check]
 *            [--shot <frame>:<file.png>]... [--cpu <n>] [--profile <file>]
 * Scene list: one scene per line, fields separated by '|' (blank lines and lines starting with # skipped):
 *   name | rom file | save file or - | keys | warm-up frames | timed frames | picture hash | sound hash
 * keys: the testrunner's --keys syntax ("10:DOWN,12:-,14:A+B"), or - for none; CW / CCW also turn a rotation
 * sensor (game D) clockwise / counter-clockwise while held. The ROM and save are read from
 * --roms (default: the current folder); a ROM name starting with tonc/ is read from tests/roms/ instead when
 * --roms holds no such file. The save is only read, never written. The clock of every cartridge is fixed
 * (2026-01-01 12:00 UTC), so runs are identical.
 * Per scene it prints the timed part's best frames per second of --runs runs (default 3), as × real time,
 * and the picture hash after the last frame + the hash of all the sound, with ok / DIFFERENT against the
 * list ("-" in the list: not checked). Every run of a scene must give the same hashes.
 * The warm-up is played once; every timed run starts from a save state of its end, in a new machine.
 * --check: exit 1 when any hash is DIFFERENT (or runs disagree). --shot (with --only): a PNG of that frame.
 * --cpu n (Linux/Android): the warm-up on any core, the timed runs on core n only.
 * --profile <file> (Linux/Android): samples of where the timed runs spend their time (code addresses in this
 * program's file, one per line; tools/dev.sh phone-profile looks them up).
 * Exit codes: 0 done, 1 a hash differs, 3 usage/load error.
 */
#ifdef __linux__
#define _GNU_SOURCE /* sched_setaffinity */
#endif
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "file_util.h"
#include "frame_hash.h"
#include "gba.h"
#include "png.h"

#ifdef __linux__
#include <sched.h>
#include <signal.h>
#include <sys/time.h>
#include <ucontext.h>
#include <unistd.h>
#endif
#ifdef _WIN32
#include <windows.h>
static double now_seconds(void) {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}
#else
#include <time.h>
static double now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec / 1e9;
}
#endif

#define GBA_FPS 59.7275
#define CLOCK_SECONDS 1767268800LL /* 2026-01-01 12:00 UTC */
#define MAX_KEY_EVENTS 512
#define MAX_SHOTS 16
#define KEY_CW (1u << 14)  /* not GBA buttons: the rotation sensor turned while held */
#define KEY_CCW (1u << 15)
#define SPIN_RATE 16000

typedef struct {
    long frame;
    uint16_t keys;
} key_event_t;

typedef struct {
    char name[64], rom[256], save[256];
    key_event_t *keys;
    int key_count;
    long warmup, timed;
    char picture[32], sound[32];
} scene_t;

typedef struct {
    long frame;
    const char *path;
} shot_t;

static char *trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = '\0';
    return s;
}

/* "10:DOWN,12:-,14:A+B" → events in frame order; false for a bad entry or frames that do not increase. */
static bool parse_keys(const char *list, scene_t *sc) {
    static const struct {
        const char *name;
        uint16_t key;
    } names[] = {{"A", GBA_KEY_A},         {"B", GBA_KEY_B},         {"SELECT", GBA_KEY_SELECT},
                 {"START", GBA_KEY_START}, {"RIGHT", GBA_KEY_RIGHT}, {"LEFT", GBA_KEY_LEFT},
                 {"UP", GBA_KEY_UP},       {"DOWN", GBA_KEY_DOWN},   {"R", GBA_KEY_R},
                 {"L", GBA_KEY_L},         {"CW", KEY_CW},           {"CCW", KEY_CCW}};
    const size_t name_count = sizeof names / sizeof names[0];
    sc->keys = malloc(sizeof(key_event_t) * MAX_KEY_EVENTS);
    sc->key_count = 0;
    if (!sc->keys) return false;
    if (!strcmp(list, "-")) return true;
    const char *p = list;
    while (*p) {
        char *end;
        long frame = strtol(p, &end, 10);
        if (end == p || *end != ':' || frame < 0 || sc->key_count == MAX_KEY_EVENTS) return false;
        if (sc->key_count > 0 && frame <= sc->keys[sc->key_count - 1].frame) return false;
        p = end + 1;
        uint16_t keys = 0;
        if (*p == '-') {
            p++;
        } else {
            for (;;) {
                size_t len = strcspn(p, "+,");
                size_t k = 0;
                while (k < name_count && !(strlen(names[k].name) == len && !strncmp(p, names[k].name, len))) k++;
                if (k == name_count) return false;
                keys |= names[k].key;
                p += len;
                if (*p != '+') break;
                p++;
            }
        }
        sc->keys[sc->key_count++] = (key_event_t){frame, keys};
        if (*p == ',') p++;
        else if (*p) return false;
    }
    return true;
}

static uint16_t keys_at(const scene_t *sc, long frame) {
    uint16_t keys = 0;
    for (int i = 0; i < sc->key_count && sc->keys[i].frame <= frame; i++) keys = sc->keys[i].keys;
    return keys;
}

/* Reads the scene list; returns the number of scenes (-1 on an error, printed). */
static int read_scenes(const char *path, scene_t **out) {
    size_t size;
    uint8_t *data = read_file(path, &size);
    if (!data) {
        fprintf(stderr, "cannot read the scene list %s\n", path);
        return -1;
    }
    char *text = malloc(size + 1);
    memcpy(text, data, size);
    text[size] = '\0';
    free(data);
    int count = 0, cap = 16;
    scene_t *scenes = calloc((size_t)cap, sizeof(scene_t));
    for (char *line = strtok(text, "\n"); line; line = strtok(NULL, "\n")) {
        char *t = trim(line);
        if (!*t || *t == '#') continue;
        char *f[8];
        int n = 0;
        for (char *p = t; n < 8;) {
            f[n++] = p;
            char *bar = strchr(p, '|');
            if (!bar) break;
            *bar = '\0';
            p = bar + 1;
        }
        if (n != 8) {
            fprintf(stderr, "%s: a scene needs 8 fields separated by |: %s\n", path, t);
            free(text);
            return -1;
        }
        if (count == cap) scenes = realloc(scenes, sizeof(scene_t) * (size_t)(cap *= 2));
        scene_t *sc = &scenes[count];
        memset(sc, 0, sizeof *sc);
        snprintf(sc->name, sizeof sc->name, "%s", trim(f[0]));
        snprintf(sc->rom, sizeof sc->rom, "%s", trim(f[1]));
        snprintf(sc->save, sizeof sc->save, "%s", trim(f[2]));
        sc->warmup = strtol(trim(f[4]), NULL, 10);
        sc->timed = strtol(trim(f[5]), NULL, 10);
        snprintf(sc->picture, sizeof sc->picture, "%s", trim(f[6]));
        snprintf(sc->sound, sizeof sc->sound, "%s", trim(f[7]));
        if (!parse_keys(trim(f[3]), sc) || sc->warmup < 0 || sc->timed <= 0) {
            fprintf(stderr, "%s: bad keys or frame counts in scene %s\n", path, sc->name);
            free(text);
            return -1;
        }
        count++;
    }
    free(text);
    *out = scenes;
    return count;
}

static uint8_t *read_game_file(const char *roms, const char *name, size_t *size) {
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", roms, name);
    uint8_t *data = read_file(path, size);
    if (!data && !strncmp(name, "tonc/", 5)) {
        snprintf(path, sizeof path, "tests/roms/%s", name);
        data = read_file(path, size);
    }
    if (!data) fprintf(stderr, "cannot read %s\n", path);
    return data;
}

typedef struct {
    double seconds;
    uint64_t picture, sound;
} run_result_t;

/* --profile (Linux/Android): SIGPROF 4000 times per second of CPU time; each sample is the interrupted code's
 * address as an offset into this program's file, so it can be looked up on the PC (llvm-symbolizer). */
#define MAX_SAMPLES (4 * 1024 * 1024)
static uintptr_t *samples;
static volatile size_t sample_count;

#if defined(__linux__) && (defined(__aarch64__) || defined(__x86_64__))
static uintptr_t exe_base;

static void on_sigprof(int sig, siginfo_t *info, void *ctx) {
    (void)sig;
    (void)info;
    const ucontext_t *uc = ctx;
#ifdef __aarch64__
    uintptr_t pc = (uintptr_t)uc->uc_mcontext.pc;
#else
    uintptr_t pc = (uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
#endif
    if (sample_count < MAX_SAMPLES) samples[sample_count++] = pc - exe_base;
}

/* Where this program's file starts in memory: the first mapping of /proc/self/exe's path. */
static uintptr_t find_exe_base(void) {
    char exe[512], line[1024];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n <= 0) return 0;
    exe[n] = '\0';
    FILE *f = fopen("/proc/self/maps", "r");
    uintptr_t base = 0;
    while (f && fgets(line, sizeof line, f)) {
        if (strstr(line, exe)) {
            base = (uintptr_t)strtoull(line, NULL, 16);
            break;
        }
    }
    if (f) fclose(f);
    return base;
}

static bool profile_start(void) {
    if (!samples && !(samples = malloc(sizeof(uintptr_t) * MAX_SAMPLES))) return false;
    exe_base = find_exe_base();
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_sigprof;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigaction(SIGPROF, &sa, NULL);
    struct itimerval it = {{0, 250}, {0, 250}};
    return setitimer(ITIMER_PROF, &it, NULL) == 0;
}

static void profile_stop(void) {
    struct itimerval it = {{0, 0}, {0, 0}};
    setitimer(ITIMER_PROF, &it, NULL);
}
#else
static bool profile_start(void) {
    fprintf(stderr, "--profile works on Linux/Android only\n");
    return false;
}
static void profile_stop(void) {}
#endif

/* A new machine with the scene's game, its save and the fixed clock; NULL (printed) when it does not load. */
static gba_t *new_machine(const scene_t *sc, const uint8_t *rom, size_t rom_size, const uint8_t *save, size_t save_size) {
    gba_t *g = gba_create(NULL);
    if (!g || gba_load_rom(g, rom, rom_size) != GBA_OK) {
        fprintf(stderr, "%s: cannot load %s\n", sc->name, sc->rom);
        gba_destroy(g);
        return NULL;
    }
    gba_set_rtc(g, CLOCK_SECONDS, 0);
    if (save && gba_load_saveram(g, save, save_size) != GBA_OK) {
        fprintf(stderr, "%s: the save does not fit this game's save chip\n", sc->name);
        gba_destroy(g);
        return NULL;
    }
    return g;
}

/* Frames [from, to) of the scene: its keys, the sound into *sound, the screenshots asked for. */
static void play(gba_t *g, const scene_t *sc, long from, long to, uint64_t *sound, const shot_t *shots, int shot_count) {
    int16_t buf[1024 * 2];
    size_t n;
    for (long frame = from; frame < to; frame++) {
        uint16_t keys = keys_at(sc, frame);
        gba_set_keys(g, keys & 0x3FF);
        gba_set_gyro(g, (keys & KEY_CW) ? SPIN_RATE : (keys & KEY_CCW) ? -SPIN_RATE : 0);
        gba_run_frame(g);
        while ((n = gba_audio_read(g, buf, 1024)) > 0) *sound = audio_hash_update(*sound, buf, n * 2);
        for (int i = 0; i < shot_count; i++)
            if (shots[i].frame == frame && png_write_bgr555(shots[i].path, gba_framebuffer(g), 240, 160) != 0)
                fprintf(stderr, "cannot write %s\n", shots[i].path);
    }
}

/* Runs this program on core `cpu` only, or on every core when cpu < 0 (Linux/Android; elsewhere nothing). */
static void pin_to_cpu(int cpu) {
#ifdef __linux__
    cpu_set_t set;
    CPU_ZERO(&set);
    long cores = sysconf(_SC_NPROCESSORS_CONF);
    for (long c = 0; c < cores; c++)
        if (cpu < 0 || c == cpu) CPU_SET(c, &set);
    if (sched_setaffinity(0, sizeof set, &set) != 0) fprintf(stderr, "cannot move to cpu%d\n", cpu);
#else
    (void)cpu;
#endif
}

static const char *verdict(const char *expected, uint64_t actual, bool *bad) {
    if (!strcmp(expected, "-")) return "";
    if (strtoull(expected, NULL, 16) == actual) return " ok";
    *bad = true;
    return " DIFFERENT";
}

static int usage(void) {
    fprintf(stderr, "usage: gbabench [--scenes <file>] [--roms <dir>] [--runs <n>] [--only <scene>] [--check]\n"
                    "                [--shot <frame>:<file.png>]... [--cpu <n>] [--profile <file>]\n");
    return 3;
}

int main(int argc, char **argv) {
    const char *scenes_path = "tests/bench/scenes.txt", *roms = ".", *only = NULL;
    int runs = 3, shot_count = 0, cpu = -1;
    const char *profile = NULL;
    bool check = false;
    shot_t shots[MAX_SHOTS];
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--scenes") && i + 1 < argc) scenes_path = argv[++i];
        else if (!strcmp(argv[i], "--roms") && i + 1 < argc) roms = argv[++i];
        else if (!strcmp(argv[i], "--runs") && i + 1 < argc) runs = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--only") && i + 1 < argc) only = argv[++i];
        else if (!strcmp(argv[i], "--check")) check = true;
        else if (!strcmp(argv[i], "--cpu") && i + 1 < argc) cpu = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--profile") && i + 1 < argc) profile = argv[++i];
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc && shot_count < MAX_SHOTS) {
            char *colon;
            long frame = strtol(argv[++i], &colon, 10);
            if (*colon != ':') return usage();
            shots[shot_count++] = (shot_t){frame, colon + 1};
        } else return usage();
    }
    if (runs < 1 || (shot_count && !only)) return usage();

    scene_t *scenes;
    int count = read_scenes(scenes_path, &scenes);
    if (count < 0) return 3;
    bool bad = false, any = false;
    double sum_x = 0;
    int timed_scenes = 0;
    for (int s = 0; s < count; s++) {
        const scene_t *sc = &scenes[s];
        if (only && strcmp(only, sc->name)) continue;
        any = true;
        size_t rom_size, save_size = 0;
        uint8_t *rom = read_game_file(roms, sc->rom, &rom_size);
        uint8_t *save = strcmp(sc->save, "-") ? read_game_file(roms, sc->save, &save_size) : NULL;
        if (!rom || (strcmp(sc->save, "-") && !save)) return 3;
        /* The warm-up once (on any core); every timed run starts from a state of its end in a new machine. */
        if (cpu >= 0) pin_to_cpu(-1);
        gba_t *g = new_machine(sc, rom, rom_size, save, save_size);
        if (!g) return 3;
        uint64_t warm_sound = AUDIO_HASH_INIT;
        play(g, sc, 0, sc->warmup, &warm_sound, shots, shot_count);
        size_t state_size = gba_state_size(g);
        uint8_t *state = malloc(state_size);
        if (!state || gba_state_save(g, state, state_size) != state_size) {
            fprintf(stderr, "%s: cannot save the warm-up state\n", sc->name);
            return 3;
        }
        gba_destroy(g);
        if (cpu >= 0) pin_to_cpu(cpu);
        run_result_t best = {0}, first = {0};
        for (int r = 0; r < runs; r++) {
            run_result_t res;
            g = new_machine(sc, rom, rom_size, save, save_size);
            if (!g || gba_state_load(g, state, state_size) != GBA_OK) {
                fprintf(stderr, "%s: cannot load the warm-up state\n", sc->name);
                return 3;
            }
            res.sound = warm_sound;
            if (profile && !profile_start()) return 3;
            double start = now_seconds();
            play(g, sc, sc->warmup, sc->warmup + sc->timed, &res.sound, shots, r == 0 ? shot_count : 0);
            res.seconds = now_seconds() - start;
            if (profile) profile_stop();
            res.picture = frame_hash(gba_framebuffer(g));
            gba_destroy(g);
            if (r == 0) first = best = res;
            else {
                if (res.picture != first.picture || res.sound != first.sound) {
                    printf("%-14s runs disagree: %016" PRIx64 "/%016" PRIx64 " then %016" PRIx64 "/%016" PRIx64 "\n",
                           sc->name, first.picture, first.sound, res.picture, res.sound);
                    bad = true;
                }
                if (res.seconds < best.seconds) best.seconds = res.seconds;
            }
        }
        free(state);
        double fps = (double)sc->timed / best.seconds;
        printf("%-14s %7.1f fps %6.2fx   picture %016" PRIx64 "%s   sound %016" PRIx64 "%s\n", sc->name, fps,
               fps / GBA_FPS, first.picture, verdict(sc->picture, first.picture, &bad), first.sound,
               verdict(sc->sound, first.sound, &bad));
        fflush(stdout);
        sum_x += fps / GBA_FPS;
        timed_scenes++;
        free(rom);
        free(save);
    }
    if (!any) {
        fprintf(stderr, "no scene named %s\n", only);
        return 3;
    }
    if (profile) {
        FILE *f = fopen(profile, "w");
        if (!f) {
            fprintf(stderr, "cannot write %s\n", profile);
            return 3;
        }
        for (size_t i = 0; i < sample_count; i++) fprintf(f, "0x%zx\n", (size_t)samples[i]);
        fclose(f);
        printf("%zu samples in %s\n", (size_t)sample_count, profile);
    }
    if (timed_scenes > 1) printf("%-14s %18.2fx (average)\n", "all", sum_x / timed_scenes);
    return check && bad ? 1 : 0;
}
```

Create `tools/bench/CMakeLists.txt`:

```cmake
add_executable(gbabench main.c)
target_link_libraries(gbabench PRIVATE gbacore gbatools)
target_compile_options(gbabench PRIVATE -Wall -Wextra)
```

Create `tools/bench/profile_report.py`:

```python
"""Where the time went: gbabench --profile samples (code addresses) looked up in the program with llvm-symbolizer.
usage: python profile_report.py <llvm-symbolizer> <gbabench binary> <samples file> [top n]
Prints the functions (the outer one, where code was inlined) and the source lines (the innermost) by share."""
import collections
import subprocess
import sys

symbolizer, binary, samples_path = sys.argv[1:4]
top = int(sys.argv[4]) if len(sys.argv) > 4 else 25
counts = collections.Counter(line.strip() for line in open(samples_path) if line.strip())
total = sum(counts.values())
addrs = list(counts)
out = subprocess.run([symbolizer, '--obj=' + binary, '--inlining', '--relative-address'], input='\n'.join(addrs) + '\n',
                     capture_output=True, text=True, check=True).stdout
# One block per address: pairs of lines (function, file:line:col), innermost first, a blank line after.
blocks = out.strip('\n').split('\n\n')
functions, lines = collections.Counter(), collections.Counter()
for addr, block in zip(addrs, blocks):
    parts = block.split('\n')
    pairs = [(parts[i], parts[i + 1] if i + 1 < len(parts) else '?') for i in range(0, len(parts), 2)]
    n = counts[addr]
    functions[pairs[-1][0]] += n
    fn, loc = pairs[0]
    loc = '/'.join(loc.replace('\\', '/').split('/')[-2:])
    lines[f'{fn}  {loc.rsplit(":", 1)[0]}'] += n
print(f'{total} samples')
print('\nfunctions (where inlined code went: the function it was inlined into)')
for name, n in functions.most_common(top):
    print(f'{100 * n / total:6.1f}%  {name}')
print('\nsource lines')
for name, n in lines.most_common(top):
    print(f'{100 * n / total:6.1f}%  {name}')
```

Create `tests/bench/scenes.txt`:

```text
# Speed scenes for gbabench (tools/bench/main.c). Games are the user's own (never in git); hashes from the unchanged
# code. name | rom | save | keys | warm-up frames | timed frames | picture hash | sound hash
game_a    | hack-1.gba | hack-1.sav | 400:START,404:-,1000:A,1004:-,1150:A,1154:-,1350:RIGHT,1420:-,1500:A,1504:-,1560:A,1564:-,1620:A,1624:-,1680:A,1684:-,1740:A,1744:-,1800:A,1804:-,1860:A,1864:-,1920:A,1924:-,1980:A,1984:-,2040:A,2044:-,2100:A,2104:-,2160:A,2164:-,2220:A,2224:-,2280:A,2284:-,2340:A,2344:-,2400:A,2404:-,2460:A,2464:-,2520:A,2524:-,2580:A,2584:-,2640:A,2644:-,2700:A,2704:-,2760:A,2764:-,2820:A,2824:-,2880:A,2884:-,2940:A,2944:-,3000:A,3004:-,3060:A,3064:-,3120:A,3124:- | 1400 | 1800 | c3f089ac93169e68 | e83efb4110121bc5
game_b       | game-b.gba | - | 950:START,956:-,1010:A,1016:-,1070:A,1076:-,1130:A,1136:-,1200:UP,1500:UP+RIGHT,1560:UP,1800:UP+LEFT,1880:UP,2100:RIGHT,2160:UP,2400:UP+LEFT,2440:UP,2700:LEFT,2760:UP | 1200 | 1800 | d980e2d8414ac086 | 479d514c071d458d
game_c  | game-c.gba | - | 700:A,704:-,800:A,804:-,850:A,854:-,900:A,904:-,950:A,954:-,1000:A,1004:-,1050:A,1054:-,1100:A,1104:-,1150:A,1154:-,1200:A,1204:-,1250:A,1254:-,1300:A,1304:-,1350:A,1354:-,1400:A,1404:-,1450:A,1454:-,1500:A,1504:-,1550:A,1554:-,1600:A,1604:-,1650:A,1654:-,1700:A,1704:-,1750:A,1754:-,1800:A,1804:-,1850:A,1854:-,1900:A,1904:-,1950:A,1954:-,2000:A,2004:-,2050:A,2054:-,2100:A,2104:-,2150:A,2154:-,2200:A,2204:-,2250:A,2254:-,2300:A,2304:-,2350:A,2354:-,2400:A,2404:-,2450:A,2454:-,2500:A,2504:-,2550:A,2554:-,2600:A,2604:-,2650:A,2654:-,2700:A,2704:-,2750:A,2754:-,2800:A,2804:-,2850:A,2854:-,2900:A,2904:-,2950:A,2954:-,3000:A,3004:-,3050:A,3054:-,3100:A,3104:-,3150:A,3154:-,3200:A,3204:-,3250:A,3254:-,3300:A,3304:-,3350:A,3354:-,3400:A,3404:-,3450:A,3454:-,3500:A,3504:-,3550:A,3554:-,3600:A,3604:-,3650:A,3654:-,3700:A,3704:-,3750:A,3754:-,3800:A,3804:-,3850:A,3854:-,3900:A,3904:-,3950:A,3954:-,4000:A,4004:- | 2200 | 1800 | 5a6ac0006dd8be0f | 8118e361080d72ab
game_d  | game-d.gba | - | 300:A,304:-,900:START,904:-,930:A,934:-,960:START,964:-,990:A,994:-,1020:START,1024:-,1050:A,1054:-,1080:START,1084:-,1110:A,1114:-,1140:START,1144:-,1170:A,1174:-,1200:START,1204:-,1230:A,1234:-,1260:START,1264:-,1290:A,1294:-,1320:START,1324:-,1350:A,1354:-,1380:START,1384:-,1410:A,1414:-,1440:START,1444:-,1470:A,1474:-,1500:START,1504:-,1530:A,1534:-,1560:START,1564:-,1590:A,1594:-,1620:START,1624:-,1650:A,1654:-,1680:START,1684:-,1710:A,1714:-,1740:START,1744:-,1770:A,1774:-,1800:START,1804:-,1830:A,1834:-,1860:START,1864:-,1890:A,1894:-,1920:START,1924:-,1950:A,1954:-,1980:START,1984:-,2010:A,2014:-,2040:START,2044:-,2070:A,2074:-,2100:START,2104:-,2130:A,2134:-,2160:START,2164:-,2190:A,2194:-,2220:START,2224:-,2250:A,2254:-,2280:START,2284:-,2310:A,2314:-,2340:START,2344:-,2370:A,2374:-,2400:START,2404:-,2430:A,2434:-,2460:START,2464:-,2490:A,2494:-,2520:START,2524:-,2550:A,2554:-,2580:START,2584:-,2610:A,2614:-,2640:START,2644:-,2670:A,2674:-,2700:START,2704:-,2730:A,2734:-,2760:START,2764:-,2790:A,2794:-,2820:START,2824:-,2850:A,2854:-,2880:START,2884:-,2910:A,2914:-,2940:START,2944:-,2970:A,2974:-,3000:START,3004:-,3030:A,3034:-,3060:START,3064:-,3090:A,3094:-,3120:START,3124:-,3150:A,3154:-,3180:START,3184:-,3210:A,3214:-,3240:START,3244:-,3270:A,3274:-,3300:START,3304:-,3330:A,3334:-,3360:START,3364:-,3390:A,3394:-,3420:START,3424:-,3450:A,3454:-,3480:START,3484:-,3510:A,3514:-,3540:START,3544:-,3570:A,3574:-,3600:START,3604:-,3630:A,3634:-,3660:START,3664:-,3690:A,3694:-,3720:START,3724:-,3750:A,3754:-,3780:START,3784:-,3810:A,3814:-,3840:START,3844:-,3870:A,3874:-,3900:START,3904:-,3930:A,3934:-,3960:START,3964:-,3990:A,3994:-,4020:START,4024:-,4050:A,4054:-,4080:START,4084:-,4110:A,4114:-,4140:START,4144:-,4170:A,4174:-,4200:START,4204:-,4230:A,4234:-,4260:START,4264:-,4290:A,4294:-,4320:START,4324:-,4350:A,4354:-,4380:START,4384:-,4410:A,4414:-,4440:START,4444:-,4470:A,4474:-,4500:START,4504:-,4530:A,4534:-,4600:CW+A,4604:CW,4620:CCW,4640:CW+A,4644:CW,4660:CCW,4680:CW+A,4684:CW,4700:CCW,4720:CW+A,4724:CW,4740:CCW,4760:CW+A,4764:CW,4780:CCW,4800:CW+A,4804:CW,4820:CCW,4840:CW+A,4844:CW,4860:CCW,4880:CW+A,4884:CW,4900:CCW,4920:CW+A,4924:CW,4940:CCW,4960:CW+A,4964:CW,4980:CCW,5000:CW+A,5004:CW,5020:CCW,5040:CW+A,5044:CW,5060:CCW,5080:CW+A,5084:CW,5100:CCW,5120:CW+A,5124:CW,5140:CCW,5160:CW+A,5164:CW,5180:CCW,5200:CW+A,5204:CW,5220:CCW,5240:CW+A,5244:CW,5260:CCW,5280:CW+A,5284:CW,5300:CCW,5320:CW+A,5324:CW,5340:CCW,5360:CW+A,5364:CW,5380:CCW,5400:CW+A,5404:CW,5420:CCW,5440:CW+A,5444:CW,5460:CCW,5480:CW+A,5484:CW,5500:CCW,5520:CW+A,5524:CW,5540:CCW,5560:CW+A,5564:CW,5580:CCW,5600:CW+A,5604:CW,5620:CCW,5640:CW+A,5644:CW,5660:CCW,5680:CW+A,5684:CW,5700:CCW,5720:CW+A,5724:CW,5740:CCW,5760:CW+A,5764:CW,5780:CCW,5800:CW+A,5804:CW,5820:CCW,5840:CW+A,5844:CW,5860:CCW,5880:CW+A,5884:CW,5900:CCW,5920:CW+A,5924:CW,5940:CCW,5960:CW+A,5964:CW,5980:CCW,6000:CW+A,6004:CW,6020:CCW,6040:CW+A,6044:CW,6060:CCW,6080:CW+A,6084:CW,6100:CCW,6120:CW+A,6124:CW,6140:CCW,6160:CW+A,6164:CW,6180:CCW,6200:CW+A,6204:CW,6220:CCW,6240:CW+A,6244:CW,6260:CCW,6280:CW+A,6284:CW,6300:CCW,6320:CW+A,6324:CW,6340:CCW,6360:CW+A,6364:CW,6380:CCW,6400:CW+A,6404:CW,6420:CCW,6440:CW+A,6444:CW,6460:CCW,6480:CW+A,6484:CW,6500:CCW,6520:CW+A,6524:CW,6540:CCW,6560:CW+A,6564:CW,6580:CCW,6600:CW+A,6604:CW,6620:CCW,6640:CW+A,6644:CW,6660:CCW,6680:CW+A,6684:CW,6700:CCW,6720:CW+A,6724:CW,6740:CCW,6760:CW+A,6764:CW,6780:CCW,6800:CW+A,6804:CW,6820:CCW,6840:CW+A,6844:CW,6860:CCW,6880:CW+A,6884:CW,6900:CCW,6920:CW+A,6924:CW,6940:CCW,6960:CW+A,6964:CW,6980:CCW,7000:CW+A,7004:CW,7020:CCW,7040:CW+A,7044:CW,7060:CCW,7080:CW+A,7084:CW,7100:CCW,7120:CW+A,7124:CW,7140:CCW,7160:CW+A,7164:CW,7180:CCW,7200:CW+A,7204:CW,7220:CCW,7240:CW+A,7244:CW,7260:CCW,7280:CW+A,7284:CW,7300:CCW,7320:CW+A,7324:CW,7340:CCW,7360:CW+A,7364:CW,7380:CCW,7400:CW+A,7404:CW,7420:CCW,7440:CW+A,7444:CW,7460:CCW,7480:CW+A,7484:CW,7500:CCW,7520:CW+A,7524:CW,7540:CCW,7560:CW+A,7564:CW,7580:CCW,7600:CW+A,7604:CW,7620:CCW,7640:CW+A,7644:CW,7660:CCW,7680:CW+A,7684:CW,7700:CCW,7720:CW+A,7724:CW,7740:CCW,7760:CW+A,7764:CW,7780:CCW,7800:CW+A,7804:CW,7820:CCW,7840:CW+A,7844:CW,7860:CCW,7880:CW+A,7884:CW,7900:CCW,7920:CW+A,7924:CW,7940:CCW,7960:CW+A,7964:CW,7980:CCW,8000:CW+A,8004:CW,8020:CCW | 4600 | 1800 | b9cd62085af128f1 | dc8d71c5cc96d585
m7_demo    | tonc/m7_demo.gba | - | 60:UP,400:UP+LEFT,520:UP,900:UP+RIGHT,1000:UP,1300:LEFT,1400:DOWN,1700:DOWN+RIGHT,1800:- | 60 | 1800 | 8d17421337b37d36 | 0e1ae05b39c9d605
snd1_demo  | tonc/snd1_demo.gba | - | 100:A,110:-,300:B,310:-,500:UP,510:-,700:DOWN,710:-,900:LEFT,910:-,1100:RIGHT,1110:-,1300:A,1310:-,1500:B,1510:- | 60 | 1800 | 45a2cbd3db7573b9 | 06984b8842381435
```

Replace `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.22)
project(gba_emulator LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(GBA_SANITIZE "Build host binaries with AddressSanitizer and UBSan" OFF)
if(GBA_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

add_subdirectory(core)

# The speed benchmark: on the PC with the other tools, for Android on its own (tools/dev.sh phone-bench).
option(GBA_BENCH_ONLY "Build only the core and gbabench (an Android executable for the phone)" OFF)
if(GBA_BENCH_ONLY)
    add_subdirectory(tools/common)
    add_subdirectory(tools/bench)
    return()
endif()

if(NOT ANDROID)
    enable_testing()
    add_subdirectory(tests/unit)
    add_subdirectory(tools/common)
    add_subdirectory(tools/testrunner)
    add_subdirectory(tools/bench)
    if(WIN32)
        add_subdirectory(tools/viewer)
    endif()

    # jsmolka/gba-tests ROMs, fetched by tools/fetch_test_roms.py; skipped when missing.
    # Each entry is name:result-register (the Thumb ROM keeps its result in r7).
    set(JSMOLKA_DIR ${PROJECT_SOURCE_DIR}/tests/roms/jsmolka)
    foreach(entry arm:12 thumb:7 memory:12 bios:12 sram:12 flash64:12 flash128:12 none:12)
        string(REPLACE ":" ";" parts "${entry}")
        list(GET parts 0 name)
        list(GET parts 1 reg)
        add_test(NAME jsmolka_${name} COMMAND testrunner ${JSMOLKA_DIR}/${name}.gba --result-reg ${reg})
        set_tests_properties(jsmolka_${name} PROPERTIES SKIP_RETURN_CODE 77)
    endforeach()

    # Our own test ROMs that report through r12 (0 = pass).
    foreach(name save_eeprom512 save_eeprom8k)
        add_test(NAME own_${name} COMMAND testrunner ${PROJECT_SOURCE_DIR}/tests/own_roms/${name}.gba --result-reg 12)
    endforeach()

    # Approved frame fingerprints; each runs a ROM for N frames and compares the picture hash.
    set(FRAMES_FILE ${PROJECT_SOURCE_DIR}/tests/expected_frames.json)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${FRAMES_FILE})
    file(READ ${FRAMES_FILE} FRAMES_JSON)
    string(JSON frame_count LENGTH "${FRAMES_JSON}" frames)
    if(frame_count GREATER 0)
        math(EXPR last_frame "${frame_count} - 1")
        foreach(i RANGE ${last_frame})
            string(JSON f_name GET "${FRAMES_JSON}" frames ${i} name)
            string(JSON f_rom GET "${FRAMES_JSON}" frames ${i} rom)
            string(JSON f_frames GET "${FRAMES_JSON}" frames ${i} frames)
            string(JSON f_hash GET "${FRAMES_JSON}" frames ${i} hash)
            string(JSON f_audio ERROR_VARIABLE audio_missing GET "${FRAMES_JSON}" frames ${i} audio)
            set(audio_args)
            if(NOT audio_missing)
                set(audio_args --expect-audio-hash ${f_audio})
            endif()
            add_test(NAME frame_${f_name}
                     COMMAND testrunner ${PROJECT_SOURCE_DIR}/tests/roms/${f_rom} --frames ${f_frames}
                             --expect-hash ${f_hash} ${audio_args})
            set_tests_properties(frame_${f_name} PROPERTIES SKIP_RETURN_CODE 77)
            # The same run through a save state: half-way it continues in a brand-new machine.
            math(EXPR f_half "${f_frames} / 2")
            if(f_half LESS 1)
                set(f_half 1)
            endif()
            add_test(NAME state_${f_name}
                     COMMAND testrunner ${PROJECT_SOURCE_DIR}/tests/roms/${f_rom} --frames ${f_frames}
                             --state-at ${f_half} --expect-hash ${f_hash} ${audio_args})
            set_tests_properties(state_${f_name} PROPERTIES SKIP_RETURN_CODE 77)
            # And through rewind (a snapshot every 4 frames): at two thirds (short runs: the last frame but one),
            # step back half of the snapshots so far, at least 2 - so every run unpacks at least one difference.
            math(EXPR f_rewind_at "${f_frames} * 2 / 3")
            if(f_rewind_at LESS 9)
                math(EXPR f_rewind_at "${f_frames} - 1")
            endif()
            math(EXPR f_rewind_steps "${f_rewind_at} / 4 / 2")
            if(f_rewind_steps LESS 2)
                set(f_rewind_steps 2)
            endif()
            add_test(NAME rewind_${f_name}
                     COMMAND testrunner ${PROJECT_SOURCE_DIR}/tests/roms/${f_rom} --frames ${f_frames}
                             --rewind-at ${f_rewind_at}:${f_rewind_steps} --expect-hash ${f_hash} ${audio_args})
            set_tests_properties(rewind_${f_name} PROPERTIES SKIP_RETURN_CODE 77)
        endforeach()
    endif()

    # mGBA test suite (tests/roms/mgba/suite.gba): each group must reach its minimum score.
    set(SUITE_FILE ${PROJECT_SOURCE_DIR}/tests/suite_targets.json)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${SUITE_FILE})
    file(READ ${SUITE_FILE} SUITE_JSON)
    string(JSON suite_count LENGTH "${SUITE_JSON}" groups)
    math(EXPR last_group "${suite_count} - 1")
    foreach(i RANGE ${last_group})
        string(JSON s_group MEMBER "${SUITE_JSON}" groups ${i})
        string(JSON s_min GET "${SUITE_JSON}" groups ${s_group})
        add_test(NAME mgba_suite_${s_group}
                 COMMAND testrunner ${PROJECT_SOURCE_DIR}/tests/roms/mgba/suite.gba --suite ${s_group} --suite-min ${s_min})
        set_tests_properties(mgba_suite_${s_group} PROPERTIES SKIP_RETURN_CODE 77)
    endforeach()
endif()
```

Replace `tools/dev.sh`:

```bash
#!/usr/bin/env bash
# Host build helper (Git Bash on Windows).
#   tools/dev.sh configure        configure build/host (Debug; sanitizers unless GBA_SANITIZE=OFF)
#   tools/dev.sh build            build everything
#   tools/dev.sh unit [filter]    build, then run unit tests whose name contains filter
#   tools/dev.sh test             build, then run every CTest test
#   tools/dev.sh run <args>       build, then run tools/testrunner with args
#   tools/dev.sh view <args>      build the optimised build/release, then run its gbaview (Windows)
#   tools/dev.sh release          configure (if needed) and build build/release (Release, no sanitizers)
#   tools/dev.sh bench <args>     build/release, then gbabench (speed scenes, tests/bench/scenes.txt) with the
#                                 games from $GBA_GAMES (default ~/Downloads); e.g. bench --check, bench --only game_b
# Android (Gradle project in app/; always the Pixel 9 phone emulator, never a USB phone; Gradle runs --offline):
#   tools/dev.sh apk              build the debug app (app/build/outputs/apk/debug/app-debug.apk)
#   tools/dev.sh jvmtest          Kotlin unit tests on the PC
#   tools/dev.sh emu              start the phone emulator if it is not running; wait until it has booted
#   tools/dev.sh install          emu + install the debug app
#   tools/dev.sh launch           emu + start the app
#   tools/dev.sh androidtest      emu + run the on-device tests
#   tools/dev.sh push <file>      emu + copy a file into the phone emulator's Download folder
# The user's real phone — ONLY when the user asks; exactly one USB phone (never an emulator), else nothing happens:
#   tools/dev.sh phone-install    build the debug app and install it on the phone (keeps the app's data)
#   tools/dev.sh phone-uninstall  remove the app from the phone
#   tools/dev.sh phone-push <file>  copy a file into the phone's Download folder
#   tools/dev.sh phone-bench <args>   build gbabench for arm64, copy it, the scenes and the games to
#                                 /data/local/tmp/gbabench (only what changed), run it (timed parts on the slowest core)
#   tools/dev.sh phone-profile <scene>  the scene once with gbabench --profile; prints where the time went
#   tools/dev.sh phone-androidtest    the on-device tests on the phone (installs the app and its tests)
#   tools/dev.sh phone-clean          delete /data/local/tmp/gbabench from the phone
# The Debug + sanitizer build is for tests; it runs real games at only ~30-45 fps, so the viewer
# always uses the release build (about 9x faster than real time).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LAD="$(cygpath -u "${LOCALAPPDATA}")"
SDK="${ANDROID_HOME:+$(cygpath -u "$ANDROID_HOME")}"
SDK="${SDK:-$LAD/Android/Sdk}"
export PATH="$SDK/cmake/3.31.6/bin:$PATH"

if [ -z "${LLVM_MINGW_BIN:-}" ]; then
    for d in "$LAD"/Microsoft/WinGet/Packages/MartinStorsjo.LLVM-MinGW.UCRT*/llvm-mingw-*/bin; do
        [ -x "$d/clang.exe" ] && LLVM_MINGW_BIN="$d"
    done
fi
if [ -n "${LLVM_MINGW_BIN:-}" ]; then
    export PATH="$LLVM_MINGW_BIN:$PATH"
fi
command -v clang >/dev/null || {
    echo "clang not found: install llvm-mingw (winget install MartinStorsjo.LLVM-MinGW.UCRT) or set LLVM_MINGW_BIN" >&2
    exit 1
}

BUILD="$ROOT/build/host"
RELEASE="$ROOT/build/release"

build_release() {
    if [ ! -f "$RELEASE/build.ninja" ]; then
        cmake -S "$ROOT" -B "$RELEASE" -G Ninja -DCMAKE_BUILD_TYPE=Release             -DCMAKE_C_COMPILER=clang -DGBA_SANITIZE=OFF
    fi
    cmake --build "$RELEASE"
}
# ---- Android ----
JBR="/c/Program Files/Android/Android Studio/jbr" # Android Studio's own Java, which runs Gradle
AVD="Pixel_9_API_36_1"
APP_ID="com.tengen.gbaemu"
# MSYS_NO_PATHCONV: stop Git Bash rewriting phone paths such as /sdcard/... into Windows paths.
adb() { MSYS_NO_PATHCONV=1 "$SDK/platform-tools/adb" "$@"; }
gradle_run() {
    [ -f "$ROOT/local.properties" ] || printf 'sdk.dir=%s\n' "$(cygpath -m "$SDK")" > "$ROOT/local.properties"
    (cd "$ROOT" && JAVA_HOME="$(cygpath -m "$JBR")" ./gradlew --offline "$@")
}
emu_serial() { adb devices | awk '$1 ~ /^emulator-/ && $2 == "device" { print $1; exit }'; }
# The one USB-connected real phone (never an emulator); an error unless there is exactly one.
phone_serial() {
    local all n
    all=$(adb devices | awk 'NR > 1 && $2 == "device" && $1 !~ /^emulator-/ { print $1 }')
    n=$(printf '%s' "$all" | grep -c . || true)
    if [ "$n" != 1 ]; then
        echo "need exactly one USB phone with USB debugging on; found $n" >&2
        return 1
    fi
    echo "$all"
}
phone_name() { echo "$(adb -s "$1" shell getprop ro.product.model | tr -d '\r') ($1)"; }
# Start the phone emulator if needed, then point adb AND Gradle at it alone (ANDROID_SERIAL), so a
# real phone plugged in over USB is never used.
emu_up() {
    if [ -z "$(emu_serial)" ]; then
        echo "starting the phone emulator ($AVD)..."
        nohup "$SDK/emulator/emulator" -avd "$AVD" -no-boot-anim >/dev/null 2>&1 &
        disown
    fi
    until [ -n "$(emu_serial)" ]; do sleep 2; done
    export ANDROID_SERIAL="$(emu_serial)"
    until [ "$(adb shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = "1" ]; do sleep 2; done
}

# ---- gbabench on the phone ----
NDK="$SDK/ndk/27.2.12479018"
PHONE_BUILD="$ROOT/build/phone-arm64"
PHONE_DIR=/data/local/tmp/gbabench
GAMES="${GBA_GAMES:-$HOME/Downloads}"
# gbabench for arm64 with the app's own optimisation (-O2, link-time optimisation), so the numbers are the app's.
phone_bench_build() {
    if [ ! -f "$PHONE_BUILD/build.ninja" ]; then
        cmake -S "$ROOT" -B "$PHONE_BUILD" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
            -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=26 -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS_RELEASE="-O2 -g" \
            -DGBA_BENCH_ONLY=ON -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON >/dev/null 2>&1
    fi
    local out
    if ! out=$(cmake --build "$PHONE_BUILD" 2>&1); then
        echo "$out" | grep -E "error" >&2
        return 1
    fi
}
# Copy $1 to the phone as $2 unless a file of the same size is there already
# (</dev/null: adb would otherwise eat the caller's list of games).
phone_copy() {
    local size; size=$(stat -c %s "$1")
    if [ "$(adb -s "$PHONE" shell stat -c %s "'$2'" </dev/null 2>/dev/null | tr -d '\r')" != "$size" ]; then
        echo "copying $(basename "$1") to the phone"
        adb -s "$PHONE" push "$(cygpath -w "$1")" "$2" </dev/null >/dev/null 2>&1
    fi
}
# Everything gbabench needs on the phone: itself (always), the scene list, and every game a scene names.
phone_bench_push() {
    phone_bench_build
    adb -s "$PHONE" shell mkdir -p "$PHONE_DIR/roms/tonc"
    adb -s "$PHONE" push "$(cygpath -w "$PHONE_BUILD/tools/bench/gbabench")" "$PHONE_DIR/gbabench" >/dev/null 2>&1
    adb -s "$PHONE" shell chmod 755 "$PHONE_DIR/gbabench"
    adb -s "$PHONE" push "$(cygpath -w "$ROOT/tests/bench/scenes.txt")" "$PHONE_DIR/scenes.txt" >/dev/null 2>&1
    local rom save
    while IFS='|' read -r _ rom save _; do
        rom=$(echo "$rom" | xargs); save=$(echo "$save" | xargs)
        for f in "$rom" "$save"; do
            [ -z "$f" ] || [ "$f" = "-" ] && continue
            case "$f" in tonc/*) phone_copy "$ROOT/tests/roms/$f" "$PHONE_DIR/roms/$f" ;;
                *) phone_copy "$GAMES/$f" "$PHONE_DIR/roms/$f" ;; esac
        done
    done < <(grep -v '^#' "$ROOT/tests/bench/scenes.txt" | grep '|')
}
# The slowest core: the last of those with the lowest cpu_capacity, or the lowest top clock speed on phones that
# have no cpu_capacity (cpu0 also serves much of the phone's own work).
phone_small_core() {
    adb -s "$PHONE" shell 'for c in /sys/devices/system/cpu/cpu[0-9]*; do
        echo "$(cat $c/cpu_capacity 2>/dev/null || cat $c/cpufreq/cpuinfo_max_freq) ${c##*cpu}"; done' |
        tr -d '\r' | sort -n -k1,1 -k2,2 | awk 'NR == 1 { cap = $1 } $1 == cap { core = $2 } END { print core }'
}

cmd="${1:-}"
shift || true
case "$cmd" in
    configure)
        cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
            -DCMAKE_C_COMPILER=clang -DGBA_SANITIZE="${GBA_SANITIZE:-ON}" ;;
    build) cmake --build "$BUILD" ;;
    unit)  cmake --build "$BUILD" && "$BUILD/tests/unit/unit_tests" "$@" ;;
    test)  cmake --build "$BUILD" && ctest --test-dir "$BUILD" --output-on-failure "$@" ;;
    run)   cmake --build "$BUILD" && "$BUILD/tools/testrunner/testrunner" "$@" ;;
    view)  build_release && "$RELEASE/tools/viewer/gbaview" "$@" ;;
    release) build_release ;;
    apk) gradle_run :app:assembleDebug ;;
    jvmtest) gradle_run :app:testDebugUnitTest ;;
    emu) emu_up ;;
    install) emu_up && gradle_run :app:installDebug ;;
    launch) emu_up && adb shell am start -a android.intent.action.MAIN -c android.intent.category.LAUNCHER -n "$APP_ID/.MainActivity" ;; # as the app icon does
    androidtest) emu_up && gradle_run :app:connectedDebugAndroidTest ;;
    push) emu_up && adb push "$(cygpath -w "$1")" /sdcard/Download/ ;; # adb is a Windows program: give it a Windows path
    phone-install)
        s=$(phone_serial) && echo "installing the app on $(phone_name "$s")" && gradle_run :app:assembleDebug &&
            adb -s "$s" install -r "$(cygpath -w "$ROOT/app/build/outputs/apk/debug/app-debug.apk")" ;;
    phone-uninstall) s=$(phone_serial) && echo "removing the app from $(phone_name "$s")" && adb -s "$s" uninstall "$APP_ID" ;;
    bench) build_release && (cd "$ROOT" && "$RELEASE/tools/bench/gbabench" --roms "$GAMES" "$@") ;;
    phone-bench)
        PHONE=$(phone_serial) && echo "speed scenes on $(phone_name "$PHONE")" && phone_bench_push &&
            core=$(phone_small_core) && echo "pinned to cpu$core" &&
            adb -s "$PHONE" shell "cd $PHONE_DIR && ./gbabench --roms roms --scenes scenes.txt --cpu $core $*" ;;
    phone-profile)
        PHONE=$(phone_serial) && phone_bench_push && core=$(phone_small_core) &&
            adb -s "$PHONE" shell "cd $PHONE_DIR && ./gbabench --roms roms --scenes scenes.txt --runs 1 --cpu $core --profile samples.txt --only $1" &&
            adb -s "$PHONE" pull "$PHONE_DIR/samples.txt" "$(cygpath -w "$PHONE_BUILD/samples.txt")" >/dev/null 2>&1 &&
            python "$(cygpath -w "$ROOT/tools/bench/profile_report.py")" \
                "$(cygpath -w "$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-symbolizer.exe")" \
                "$(cygpath -w "$PHONE_BUILD/tools/bench/gbabench")" "$(cygpath -w "$PHONE_BUILD/samples.txt")" ;;
    phone-androidtest) PHONE=$(phone_serial) && echo "on-device tests on $(phone_name "$PHONE")" && ANDROID_SERIAL="$PHONE" gradle_run :app:connectedDebugAndroidTest ;;
    phone-clean) PHONE=$(phone_serial) && adb -s "$PHONE" shell rm -rf "$PHONE_DIR" && echo "removed $PHONE_DIR" ;;
    phone-push) s=$(phone_serial) && echo "copying to $(phone_name "$s")" && adb -s "$s" push "$(cygpath -w "$1")" /sdcard/Download/ ;;
    *) echo "usage: tools/dev.sh configure|build|unit [filter]|test|run <args>|view <args>|release|apk|jvmtest|emu|install|launch|androidtest|push <file>|bench <args>|phone-install|phone-uninstall|phone-push <file>|phone-bench <args>|phone-profile <scene>|phone-androidtest|phone-clean" >&2; exit 2 ;;
esac
```

- [x] **Step 2: Run the scenes on the PC and on phone B**

```bash
bash tools/dev.sh bench --runs 1 --check; echo "exit $?"
bash tools/dev.sh phone-bench --runs 1 --check
```

Expected: 6 scene lines, each `picture … ok   sound … ok`, `exit 0`; on phone B the same 6 hashes `ok` (`pinned to cpu3`; the first run copies the games, ~65 MB). Phone B's speeds are today's: about game A 1.23×, game B 0.84×, game C 1.60×, game D 1.13×, `m7_demo` 2.30×, `snd1_demo` 3.71× (`gbabench` built with link-time optimisation already: a few per cent more).

- [x] **Step 3: The profiler on phone B**

```bash
bash tools/dev.sh phone-profile game_b | head -12
```

Expected: `game_b … ok`, `N samples in samples.txt`, then the functions by share (`cpu_step`, `bus_fetch32`, `arm_data_processing`, `ppu_compose`, …).

- [x] **Step 4: Commit**

```bash
git add tools/bench/main.c tools/bench/CMakeLists.txt tools/bench/profile_report.py tests/bench/scenes.txt CMakeLists.txt tools/dev.sh docs/PROGRESS.md docs/superpowers/plans/2026-10-03-milestone-7a-speed.md
git commit -m "feat(tools): gbabench - speed scenes on the PC and the phone, with a profiler" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 2: Tests that pin today's results

**Files:**
- Create: `tests/unit/test_speedups.c`

- [x] **Step 1: Write the tests**

Create `tests/unit/test_speedups.c`:

```c
/*
 * Milestone 7a's speed-ups must not change a single result. Each fast path is compared here with the code it
 * replaced (copied below as ref_*), on random VRAM, registers and line buffers; plus the CPU shortcuts.
 */
#include <string.h>

#include "cpu/cpu.h"
#include "gba_internal.h"
#include "mem/bus.h"
#include "ppu/ppu_internal.h"
#include "test.h"
#include "testutil.h"

static uint32_t rng_state = 0x12345678u;

static uint32_t rnd(void) { /* xorshift32: the same numbers on every run */
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static void random_vram(gba_t *g) {
    for (uint32_t i = 0; i < VRAM_SIZE; i++) g->bus.vram[i] = (uint8_t)rnd();
    for (uint32_t i = 0; i < PAL_SIZE; i++) g->bus.pal[i] = (uint8_t)rnd();
}

/* ---- text backgrounds: the pixel-by-pixel code before 7a ---- */

static void ref_text_bg(gba_t *g, int bg, int y, layer_px_t *out) {
    uint16_t cnt = ppu_reg(g, 0x008 + (uint32_t)bg * 2);
    uint32_t hofs = ppu_reg(g, 0x010 + (uint32_t)bg * 4) & 0x1FF;
    uint32_t vofs = ppu_reg(g, 0x012 + (uint32_t)bg * 4) & 0x1FF;
    uint8_t prio = cnt & 3;
    uint32_t char_base = ((cnt >> 2) & 3) * 0x4000u;
    bool bpp8 = (cnt & 0x80) != 0;
    uint32_t screen_base = ((cnt >> 8) & 0x1F) * 0x800u;
    uint32_t width = (cnt & 0x4000) ? 512 : 256, height = (cnt & 0x8000) ? 512 : 256;
    uint16_t m = ppu_reg(g, 0x04C);
    int mh = (cnt & 0x40) ? (m & 0xF) + 1 : 1, mv = (cnt & 0x40) ? ((m >> 4) & 0xF) + 1 : 1;
    uint32_t py = ((uint32_t)(y - y % mv) + vofs) & (height - 1);
    for (int x = 0; x < PPU_WIDTH; x++) {
        uint32_t px = ((uint32_t)(x - x % mh) + hofs) & (width - 1);
        uint32_t block = (px >> 8) + (py >> 8) * (width >> 8);
        uint16_t entry = vram16(g, screen_base + block * 0x800 + ((py >> 3) & 31) * 64 + ((px >> 3) & 31) * 2);
        uint32_t tile = entry & 0x3FF;
        uint32_t tx = (entry & 0x400) ? 7 - (px & 7) : px & 7;
        uint32_t ty = (entry & 0x800) ? 7 - (py & 7) : py & 7;
        uint32_t index;
        if (bpp8) {
            uint32_t addr = char_base + tile * 64 + ty * 8 + tx;
            if (addr >= 0x10000) continue;
            index = g->bus.vram[addr];
        } else {
            uint32_t addr = char_base + tile * 32 + ty * 4 + tx / 2;
            if (addr >= 0x10000) continue;
            index = (tx & 1) ? g->bus.vram[addr] >> 4 : g->bus.vram[addr] & 0xFu;
            if (index) index |= (uint32_t)(entry >> 12) << 4;
        }
        if (!index) continue;
        put_px(&out[x], pal_color(g, index), prio, PX_OPAQUE);
    }
}

TEST(speedup_text_bg_matches_the_pixel_by_pixel_code) {
    gba_t *g = gba_create(NULL);
    static layer_px_t fast[PPU_WIDTH], ref[PPU_WIDTH];
    for (int trial = 0; trial < 400; trial++) {
        if (trial % 50 == 0) random_vram(g);
        int bg = (int)(rnd() & 3);
        uint16_t cnt = (uint16_t)rnd();
        if (trial % 4) cnt &= (uint16_t)~0x40; /* mostly without mosaic (the tile-row path) */
        tu_io16(g, 0x008 + (uint32_t)bg * 2, cnt);
        tu_io16(g, 0x010 + (uint32_t)bg * 4, (uint16_t)rnd());
        tu_io16(g, 0x012 + (uint32_t)bg * 4, (uint16_t)rnd());
        tu_io16(g, 0x04C, (uint16_t)rnd());
        int y = (int)(rnd() % 160);
        memset(fast, 0, sizeof fast);
        memset(ref, 0, sizeof ref);
        ppu_render_text_bg(g, bg, y, fast);
        ref_text_bg(g, bg, y, ref);
        CHECK(memcmp(fast, ref, sizeof fast) == 0);
    }
    gba_destroy(g);
}

/* ---- the compositor: the code before 7a ---- */

static void ref_find_top_two(const line_buffers_t *lb, int x, uint32_t visible, uint16_t backdrop, int layer[2],
                             uint16_t color[2], bool *semi) {
    int found = 0;
    layer[0] = layer[1] = LAYER_BD;
    color[0] = color[1] = backdrop;
    *semi = false;
    for (uint8_t prio = 0; prio < 4 && found < 2; prio++) {
        const layer_px_t *o = &lb->obj[x];
        if ((visible & (1u << LAYER_OBJ)) && (o->flags & PX_OPAQUE) && o->prio == prio) {
            if (found == 0) *semi = (o->flags & PX_SEMI) != 0;
            layer[found] = LAYER_OBJ;
            color[found++] = o->color;
        }
        for (int bg = 0; bg < 4 && found < 2; bg++) {
            const layer_px_t *p = &lb->bg[bg][x];
            if ((visible & (1u << bg)) && (p->flags & PX_OPAQUE) && p->prio == prio) {
                layer[found] = bg;
                color[found++] = p->color;
            }
        }
    }
}

static bool ref_in_span(uint16_t reg, int v) {
    int start = reg >> 8, end = reg & 0xFF;
    if (start <= end) return v >= start && v < end;
    return v >= start || v < end;
}

static uint32_t ref_cap16(uint32_t v) {
    return v > 16 ? 16 : v;
}

static uint16_t ref_blend(uint16_t a, uint16_t b, uint32_t eva, uint32_t evb) {
    uint16_t out = 0;
    for (int s = 0; s < 15; s += 5) {
        uint32_t c = (((a >> s) & 31u) * eva + ((b >> s) & 31u) * evb) >> 4;
        out |= (uint16_t)((c > 31 ? 31 : c) << s);
    }
    return out;
}

static uint16_t ref_brighten(uint16_t a, uint32_t evy) {
    uint16_t out = 0;
    for (int s = 0; s < 15; s += 5) {
        uint32_t c = (a >> s) & 31u;
        out |= (uint16_t)((c + (((31 - c) * evy) >> 4)) << s);
    }
    return out;
}

static uint16_t ref_darken(uint16_t a, uint32_t evy) {
    uint16_t out = 0;
    for (int s = 0; s < 15; s += 5) {
        uint32_t c = (a >> s) & 31u;
        out |= (uint16_t)((c - ((c * evy) >> 4)) << s);
    }
    return out;
}

static void ref_compose(gba_t *g, int y, const line_buffers_t *lb, uint16_t *dst) {
    uint16_t dispcnt = g->ppu.dispcnt;
    uint16_t winin = ppu_reg(g, 0x048), winout = ppu_reg(g, 0x04A);
    uint16_t win0h = ppu_reg(g, 0x040), win1h = ppu_reg(g, 0x042);
    bool win0 = (dispcnt & 0x2000) && ref_in_span(ppu_reg(g, 0x044), y);
    bool win1 = (dispcnt & 0x4000) && ref_in_span(ppu_reg(g, 0x046), y);
    bool objwin = (dispcnt & 0x8000) != 0;
    bool windowing = (dispcnt & 0xE000) != 0;
    uint16_t bldcnt = ppu_reg(g, 0x050), bldalpha = ppu_reg(g, 0x052);
    uint32_t mode = (bldcnt >> 6) & 3;
    uint32_t eva = ref_cap16(bldalpha & 0x1F), evb = ref_cap16((bldalpha >> 8) & 0x1F);
    uint32_t evy = ref_cap16(ppu_reg(g, 0x054) & 0x1F);
    uint16_t backdrop = pal_color(g, 0);
    for (int x = 0; x < PPU_WIDTH; x++) {
        uint32_t mask = 0x3F;
        if (windowing) {
            if (win0 && ref_in_span(win0h, x)) mask = winin & 0x3F;
            else if (win1 && ref_in_span(win1h, x)) mask = (winin >> 8) & 0x3F;
            else if (objwin && lb->objwin[x]) mask = (winout >> 8) & 0x3F;
            else mask = winout & 0x3F;
        }
        int layer[2];
        uint16_t color[2];
        bool semi;
        ref_find_top_two(lb, x, mask, backdrop, layer, color, &semi);
        uint16_t out = color[0];
        if (mask & 0x20) {
            bool first = (bldcnt >> layer[0]) & 1;
            bool second = (bldcnt >> (8 + layer[1])) & 1;
            if (semi && second) {
                out = ref_blend(color[0], color[1], eva, evb);
            } else if (first) {
                if (mode == 1 && second) out = ref_blend(color[0], color[1], eva, evb);
                else if (mode == 2) out = ref_brighten(color[0], evy);
                else if (mode == 3) out = ref_darken(color[0], evy);
            }
        }
        dst[x] = out;
    }
}

static void random_px(layer_px_t *p, bool obj) {
    uint32_t r = rnd();
    uint8_t flags = (r & 3) ? PX_OPAQUE : 0; /* three in four drawn */
    if (obj && flags && (r & 0x30) == 0x30) flags |= PX_SEMI;
    put_px(p, (uint16_t)(r >> 16) & 0x7FFF, (uint8_t)((r >> 8) & 3), flags);
}

TEST(speedup_compose_matches_the_code_before) {
    gba_t *g = gba_create(NULL);
    static line_buffers_t lb;
    uint16_t fast[PPU_WIDTH], ref[PPU_WIDTH];
    for (int trial = 0; trial < 600; trial++) {
        bool plain = trial % 3 == 0; /* no windows, no effects, no semi-transparent sprites: the quick path */
        for (int x = 0; x < PPU_WIDTH; x++) {
            for (int bg = 0; bg < 4; bg++) random_px(&lb.bg[bg][x], false);
            random_px(&lb.obj[x], !plain);
            lb.objwin[x] = (rnd() & 1) != 0;
        }
        g->ppu.dispcnt = plain ? 0 : (uint16_t)(rnd() & 0xE000);
        for (uint32_t off = 0x040; off <= 0x04A; off += 2) tu_io16(g, off, (uint16_t)rnd());
        tu_io16(g, 0x050, plain ? (uint16_t)(rnd() & 0x3F3F) : (uint16_t)rnd());
        tu_io16(g, 0x052, (uint16_t)rnd());
        tu_io16(g, 0x054, (uint16_t)rnd());
        tu_pal(g, 0, (uint16_t)rnd() & 0x7FFF);
        int y = (int)(rnd() % 160);
        ppu_compose(g, y, &lb, fast);
        ref_compose(g, y, &lb, ref);
        CHECK(memcmp(fast, ref, sizeof fast) == 0);
    }
    gba_destroy(g);
}

/* ---- bitmap backgrounds (modes 3-5): the code before 7a ---- */

static void ref_bitmap_bg(gba_t *g, int mode, int y, layer_px_t *out) {
    uint16_t cnt = ppu_reg(g, 0x00C);
    uint8_t prio = cnt & 3;
    uint32_t page = (mode != 3 && (g->ppu.dispcnt & 0x10)) ? 0xA000 : 0;
    int32_t w = mode == 5 ? 160 : 240, h = mode == 5 ? 128 : 160;
    uint16_t m = ppu_reg(g, 0x04C);
    int mh = (cnt & 0x40) ? (m & 0xF) + 1 : 1, mv = (cnt & 0x40) ? ((m >> 4) & 0xF) + 1 : 1;
    int back = y % mv;
    int32_t pa = (int16_t)ppu_reg(g, 0x020), pc = (int16_t)ppu_reg(g, 0x024);
    int32_t x0 = g->ppu.ref_x[0] - back * (int16_t)ppu_reg(g, 0x022);
    int32_t y0 = g->ppu.ref_y[0] - back * (int16_t)ppu_reg(g, 0x026);
    for (int x = 0; x < PPU_WIDTH; x++) {
        int32_t sx = x - x % mh;
        int32_t tx = (x0 + pa * sx) >> 8, ty = (y0 + pc * sx) >> 8;
        if (tx < 0 || ty < 0 || tx >= w || ty >= h) continue;
        uint32_t pos = (uint32_t)(ty * w + tx);
        uint16_t color;
        if (mode == 4) {
            uint32_t index = g->bus.vram[page + pos];
            if (!index) continue;
            color = pal_color(g, index);
        } else {
            color = vram16(g, page + pos * 2) & 0x7FFF;
        }
        put_px(&out[x], color, prio, PX_OPAQUE);
    }
}

TEST(speedup_bitmap_bg_matches_the_code_before) {
    gba_t *g = gba_create(NULL);
    static layer_px_t fast[PPU_WIDTH], ref[PPU_WIDTH];
    random_vram(g);
    for (int trial = 0; trial < 400; trial++) {
        int mode = 3 + (int)(rnd() % 3);
        g->ppu.dispcnt = (uint16_t)(mode | (rnd() & 0x10));
        uint16_t cnt = (uint16_t)(rnd() & 3);
        if (trial % 4 == 0) cnt |= 0x40; /* some with mosaic */
        tu_io16(g, 0x00C, cnt);
        tu_io16(g, 0x04C, (uint16_t)rnd());
        bool unrotated = trial % 2 == 0; /* half the lines take the one-row path */
        tu_io16(g, 0x020, unrotated ? (uint16_t)(64 + (rnd() & 0x1FF)) : (uint16_t)rnd());
        tu_io16(g, 0x022, (uint16_t)rnd());
        tu_io16(g, 0x024, unrotated ? 0 : (uint16_t)rnd());
        tu_io16(g, 0x026, (uint16_t)rnd());
        g->ppu.ref_x[0] = (int32_t)(rnd() % (400u << 8)) - (80 << 8);
        g->ppu.ref_y[0] = (int32_t)(rnd() % (260u << 8)) - (50 << 8);
        int y = (int)(rnd() % 160);
        memset(fast, 0, sizeof fast);
        memset(ref, 0, sizeof ref);
        ppu_render_bitmap_bg(g, mode, y, fast);
        ref_bitmap_bg(g, mode, y, ref);
        CHECK(memcmp(fast, ref, sizeof fast) == 0);
    }
    gba_destroy(g);
}

/* ---- the CPU shortcuts ---- */

TEST(speedup_every_condition_with_every_flag_combination) {
    const uint32_t nop = 0xE1A00000; /* mov r0, r0 */
    for (uint32_t cond = 0; cond < 16; cond++) {
        gba_t *g = tu_arm(&nop, 1);
        for (uint32_t nzcv = 0; nzcv < 16; nzcv++) {
            bus_poke(g, 0x03000000, cond << 28 | 0x03A00001, 4); /* mov<cond> r0, #1 */
            g->cpu.r[0] = 0;
            g->cpu.r[15] = 0x03000000;
            g->cpu.cpsr = (g->cpu.cpsr & 0x0FFFFFFFu) | nzcv << 28;
            tu_steps(g, 1);
            CHECK_EQ(g->cpu.r[0], cpu_cond_passed(nzcv << 28, cond) ? 1u : 0u);
        }
        gba_destroy(g);
    }
}

TEST(speedup_code_rewritten_in_ram_runs_the_new_code) {
    const uint32_t nop = 0xE1A00000;
    gba_t *g = tu_arm(&nop, 1);
    const uint32_t places[] = {0x03000100, 0x02000100, 0x03FF8100 /* an IWRAM mirror */};
    for (size_t i = 0; i < TU_LEN(places); i++) {
        bus_poke(g, places[i], 0xE3A00011, 4); /* mov r0, #0x11 */
        g->cpu.r[15] = places[i];
        tu_steps(g, 1);
        CHECK_EQ(g->cpu.r[0], 0x11);
        bus_write32(g, places[i], 0xE3A00022, false); /* the program rewrites it: mov r0, #0x22 */
        g->cpu.r[15] = places[i];
        tu_steps(g, 1);
        CHECK_EQ(g->cpu.r[0], 0x22);
    }
    gba_destroy(g);
}

TEST(speedup_rom_reads_at_the_end_and_past_it) {
    const uint32_t ops[] = {0xE1A00000, 0x11223344};
    gba_t *g = tu_arm(ops, TU_LEN(ops)); /* a 0x200-byte ROM */
    CHECK_EQ(bus_read32(g, 0x08000004, false), 0x11223344);
    CHECK_EQ(bus_read32(g, 0x080001FC, false), 0); /* the last word */
    CHECK_EQ(bus_read16(g, 0x08000200, false), 0x0100); /* past the end: the address pattern */
    CHECK_EQ(bus_read32(g, 0x08000200, false), 0x01010100);
    CHECK_EQ(bus_read8(g, 0x08000201, false), 0x01);
    CHECK_EQ(bus_read32(g, 0x0A000004, false), 0x11223344); /* a mirror */
    gba_destroy(g);
}
```

- [x] **Step 2: Run them on today's code**

```bash
bash tools/dev.sh unit speedup
bash tools/dev.sh unit 2>&1 | tail -1
```

Expected: `6 tests, 0 failed` (the copies of today's code agree with today's code), then `410 tests, 0 failed`. (In the prototype a horizontal-flip bug planted in the new text BG path and an OBJ-priority bug planted in the new compositor each made one of these fail.)

- [x] **Step 3: Commit**

```bash
git add tests/unit/test_speedups.c docs/PROGRESS.md docs/superpowers/plans/2026-10-03-milestone-7a-speed.md
git commit -m "test: pin the drawing and CPU results the speed-ups must keep" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 3: Drawing speed-ups

**Files:**
- Modify: `core/src/ppu/ppu_bg.c` (text BGs a tile row at a time without horizontal mosaic; unrotated bitmap lines read one row), `core/src/ppu/ppu_compose.c` (each layer looked at once; a plain path when nothing blends or windows)

- [x] **Step 1: Write the code**

Replace `core/src/ppu/ppu_bg.c`:

```c
#include "ppu/ppu_internal.h"

/* BG mosaic block size (1 = off) from MOSAIC when the BG's mosaic bit is set. */
static void bg_mosaic(const gba_t *g, uint16_t cnt, int *mh, int *mv) {
    uint16_t m = ppu_reg(g, 0x04C);
    bool on = (cnt & 0x40) != 0;
    *mh = on ? (m & 0xF) + 1 : 1;
    *mv = on ? ((m >> 4) & 0xF) + 1 : 1;
}

/* Line y of a text BG with horizontal mosaic: pixel by pixel (each block repeats its first pixel). */
static void text_bg_mosaic(gba_t *g, uint16_t cnt, uint32_t hofs, uint32_t py, int mh, layer_px_t *out) {
    uint8_t prio = cnt & 3;
    uint32_t char_base = ((cnt >> 2) & 3) * 0x4000u;
    bool bpp8 = (cnt & 0x80) != 0;
    uint32_t screen_base = ((cnt >> 8) & 0x1F) * 0x800u;
    uint32_t width = (cnt & 0x4000) ? 512 : 256;
    for (int x = 0; x < PPU_WIDTH; x++) {
        uint32_t px = ((uint32_t)(x - x % mh) + hofs) & (width - 1);
        /* Maps wider or taller than 256 are 32x32-entry screen blocks: left to right, then top to bottom. */
        uint32_t block = (px >> 8) + (py >> 8) * (width >> 8);
        uint16_t entry = vram16(g, screen_base + block * 0x800 + ((py >> 3) & 31) * 64 + ((px >> 3) & 31) * 2);
        uint32_t tile = entry & 0x3FF;
        uint32_t tx = (entry & 0x400) ? 7 - (px & 7) : px & 7;
        uint32_t ty = (entry & 0x800) ? 7 - (py & 7) : py & 7;
        uint32_t index;
        if (bpp8) {
            uint32_t addr = char_base + tile * 64 + ty * 8 + tx;
            if (addr >= 0x10000) continue; /* backgrounds cannot read sprite VRAM */
            index = g->bus.vram[addr];
        } else {
            uint32_t addr = char_base + tile * 32 + ty * 4 + tx / 2;
            if (addr >= 0x10000) continue;
            index = (tx & 1) ? g->bus.vram[addr] >> 4 : g->bus.vram[addr] & 0xFu;
            if (index) index |= (uint32_t)(entry >> 12) << 4;
        }
        if (!index) continue;
        put_px(&out[x], pal_color(g, index), prio, PX_OPAQUE);
    }
}

void ppu_render_text_bg(gba_t *g, int bg, int y, layer_px_t *out) {
    uint16_t cnt = ppu_reg(g, 0x008 + (uint32_t)bg * 2);
    uint32_t hofs = ppu_reg(g, 0x010 + (uint32_t)bg * 4) & 0x1FF;
    uint32_t vofs = ppu_reg(g, 0x012 + (uint32_t)bg * 4) & 0x1FF;
    uint32_t height = (cnt & 0x8000) ? 512 : 256;
    int mh, mv;
    bg_mosaic(g, cnt, &mh, &mv);
    uint32_t py = ((uint32_t)(y - y % mv) + vofs) & (height - 1);
    if (mh > 1) {
        text_bg_mosaic(g, cnt, hofs, py, mh, out);
        return;
    }

    /* A tile row at a time: its map entry and its row of pixel data are read once. */
    uint8_t prio = cnt & 3;
    uint32_t char_base = ((cnt >> 2) & 3) * 0x4000u;
    bool bpp8 = (cnt & 0x80) != 0;
    uint32_t screen_base = ((cnt >> 8) & 0x1F) * 0x800u;
    uint32_t width = (cnt & 0x4000) ? 512 : 256;
    uint32_t row_entries = screen_base + (py >> 8) * (width >> 8) * 0x800 + ((py >> 3) & 31) * 64;
    for (int x = 0; x < PPU_WIDTH;) {
        uint32_t px = ((uint32_t)x + hofs) & (width - 1);
        uint32_t first = px & 7;
        int n = 8 - (int)first;
        if (n > PPU_WIDTH - x) n = PPU_WIDTH - x;
        uint16_t entry = vram16(g, row_entries + (px >> 8) * 0x800 + ((px >> 3) & 31) * 2);
        uint32_t tile = entry & 0x3FF;
        bool hflip = (entry & 0x400) != 0;
        uint32_t ty = (entry & 0x800) ? 7 - (py & 7) : py & 7;
        layer_px_t *o = &out[x];
        x += n;
        if (bpp8) {
            uint32_t addr = char_base + tile * 64 + ty * 8;
            if (addr >= 0x10000) continue; /* backgrounds cannot read sprite VRAM (a row never straddles it) */
            const uint8_t *row = &g->bus.vram[addr];
            for (int i = 0; i < n; i++) {
                uint32_t tx = first + (uint32_t)i;
                uint32_t index = row[hflip ? 7 - tx : tx];
                if (index) put_px(&o[i], pal_color(g, index), prio, PX_OPAQUE);
            }
        } else {
            uint32_t addr = char_base + tile * 32 + ty * 4;
            if (addr >= 0x10000) continue;
            const uint8_t *b = &g->bus.vram[addr];
            uint32_t row = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
            if (!row) continue; /* a transparent tile row */
            uint32_t bank = (uint32_t)(entry >> 12) << 4;
            for (int i = 0; i < n; i++) {
                uint32_t tx = first + (uint32_t)i;
                uint32_t index = (row >> ((hflip ? 7 - tx : tx) * 4)) & 0xF;
                if (index) put_px(&o[i], pal_color(g, bank | index), prio, PX_OPAQUE);
            }
        }
    }
}

typedef struct {
    int32_t pa, pc; /* texture step per screen pixel (8.8) */
    int32_t x0, y0; /* texture position of screen pixel 0 (20.8) */
    int mh;         /* horizontal mosaic block */
} affine_line_t;

/* Texture walk for line y of BG2/BG3. Vertical mosaic reuses the first line of the block by
 * stepping the reference back (exact while the parameters stay constant over the block). */
static affine_line_t affine_line(const gba_t *g, int bg, int y, uint16_t cnt) {
    uint32_t params = bg == 2 ? 0x020 : 0x030;
    affine_line_t a;
    int mv;
    bg_mosaic(g, cnt, &a.mh, &mv);
    int back = y % mv;
    a.pa = (int16_t)ppu_reg(g, params);
    a.pc = (int16_t)ppu_reg(g, params + 4);
    a.x0 = g->ppu.ref_x[bg - 2] - back * (int16_t)ppu_reg(g, params + 2);
    a.y0 = g->ppu.ref_y[bg - 2] - back * (int16_t)ppu_reg(g, params + 6);
    return a;
}

void ppu_render_affine_bg(gba_t *g, int bg, int y, layer_px_t *out) {
    uint16_t cnt = ppu_reg(g, 0x008 + (uint32_t)bg * 2);
    uint8_t prio = cnt & 3;
    uint32_t char_base = ((cnt >> 2) & 3) * 0x4000u;
    uint32_t screen_base = ((cnt >> 8) & 0x1F) * 0x800u;
    int32_t size = 128 << ((cnt >> 14) & 3);
    bool wrap = (cnt & 0x2000) != 0;
    affine_line_t a = affine_line(g, bg, y, cnt);

    for (int x = 0; x < PPU_WIDTH; x++) {
        int32_t sx = x - x % a.mh;
        int32_t tx = (a.x0 + a.pa * sx) >> 8, ty = (a.y0 + a.pc * sx) >> 8;
        if (wrap) {
            tx &= size - 1;
            ty &= size - 1;
        } else if (tx < 0 || ty < 0 || tx >= size || ty >= size) {
            continue;
        }
        uint32_t map = screen_base + (uint32_t)(ty >> 3) * (uint32_t)(size >> 3) + (uint32_t)(tx >> 3);
        uint32_t tile = g->bus.vram[map & 0xFFFF];
        uint32_t addr = char_base + tile * 64 + (uint32_t)(ty & 7) * 8 + (uint32_t)(tx & 7);
        if (addr >= 0x10000) continue;
        uint32_t index = g->bus.vram[addr];
        if (!index) continue;
        put_px(&out[x], pal_color(g, index), prio, PX_OPAQUE);
    }
}

/* Modes 3-5 draw BG2 as a bitmap sampled through the BG2 affine transform. */
void ppu_render_bitmap_bg(gba_t *g, int mode, int y, layer_px_t *out) {
    uint16_t cnt = ppu_reg(g, 0x00C);
    uint8_t prio = cnt & 3;
    uint32_t page = (mode != 3 && (g->ppu.dispcnt & 0x10)) ? 0xA000 : 0;
    int32_t w = mode == 5 ? 160 : 240, h = mode == 5 ? 128 : 160;
    affine_line_t a = affine_line(g, 2, y, cnt);

    if (a.mh == 1 && a.pc == 0) { /* not rotated: the whole line is one row of the bitmap */
        int32_t ty = a.y0 >> 8;
        if (ty < 0 || ty >= h) return;
        for (int x = 0; x < PPU_WIDTH; x++) {
            int32_t tx = (a.x0 + a.pa * x) >> 8;
            if (tx < 0 || tx >= w) continue;
            uint32_t pos = (uint32_t)(ty * w + tx);
            if (mode == 4) {
                uint32_t index = g->bus.vram[page + pos];
                if (index) put_px(&out[x], pal_color(g, index), prio, PX_OPAQUE);
            } else {
                put_px(&out[x], vram16(g, page + pos * 2) & 0x7FFF, prio, PX_OPAQUE);
            }
        }
        return;
    }

    for (int x = 0; x < PPU_WIDTH; x++) {
        int32_t sx = x - x % a.mh;
        int32_t tx = (a.x0 + a.pa * sx) >> 8, ty = (a.y0 + a.pc * sx) >> 8;
        if (tx < 0 || ty < 0 || tx >= w || ty >= h) continue;
        uint32_t pos = (uint32_t)(ty * w + tx);
        uint16_t color;
        if (mode == 4) {
            uint32_t index = g->bus.vram[page + pos];
            if (!index) continue;
            color = pal_color(g, index);
        } else {
            color = vram16(g, page + pos * 2) & 0x7FFF;
        }
        put_px(&out[x], color, prio, PX_OPAQUE);
    }
}
```

Replace `core/src/ppu/ppu_compose.c`:

```c
#include "ppu/ppu_internal.h"

/*
 * The two front-most visible pixels at x. Front to back: lower priority value first;
 * at equal priority OBJ is in front of BG0, BG0 of BG1, and so on. The backdrop is last.
 * Each layer is looked at once; its place in that order is the key prio * 8 + (OBJ 0, BGn n + 1).
 */
static void find_top_two(const line_buffers_t *lb, int x, uint32_t visible, uint16_t backdrop, int layer[2],
                         uint16_t color[2], bool *semi) {
    unsigned key0 = 64, key1 = 64;
    layer[0] = layer[1] = LAYER_BD;
    color[0] = color[1] = backdrop;
    const layer_px_t *o = &lb->obj[x];
    bool obj_first = false;
    if ((visible & (1u << LAYER_OBJ)) && (o->flags & PX_OPAQUE)) {
        key0 = (unsigned)o->prio * 8;
        layer[0] = LAYER_OBJ;
        color[0] = o->color;
        obj_first = true;
    }
    for (int bg = 0; bg < 4; bg++) {
        const layer_px_t *p = &lb->bg[bg][x];
        if (!(visible & (1u << bg)) || !(p->flags & PX_OPAQUE)) continue;
        unsigned key = (unsigned)p->prio * 8 + (unsigned)bg + 1;
        if (key < key0) {
            key1 = key0;
            layer[1] = layer[0];
            color[1] = color[0];
            key0 = key;
            layer[0] = bg;
            color[0] = p->color;
            obj_first = false;
        } else if (key < key1) {
            key1 = key;
            layer[1] = bg;
            color[1] = p->color;
        }
    }
    *semi = obj_first && (o->flags & PX_SEMI) != 0;
}

/* The front-most pixel at x when nothing blends or windows: every layer is visible. */
static uint16_t find_top(const line_buffers_t *lb, int x, uint16_t backdrop) {
    unsigned best = 64;
    uint16_t color = backdrop;
    const layer_px_t *o = &lb->obj[x];
    if (o->flags & PX_OPAQUE) {
        best = (unsigned)o->prio * 8;
        color = o->color;
    }
    for (int bg = 0; bg < 4; bg++) {
        const layer_px_t *p = &lb->bg[bg][x];
        unsigned key = (unsigned)p->prio * 8 + (unsigned)bg + 1;
        if ((p->flags & PX_OPAQUE) && key < best) {
            best = key;
            color = p->color;
        }
    }
    return color;
}

/* WINxH / WINxV hold start << 8 | end (end exclusive); start > end wraps around. */
static bool in_span(uint16_t reg, int v) {
    int start = reg >> 8, end = reg & 0xFF;
    if (start <= end) return v >= start && v < end;
    return v >= start || v < end;
}

static uint32_t cap16(uint32_t v) {
    return v > 16 ? 16 : v;
}

static uint16_t blend(uint16_t a, uint16_t b, uint32_t eva, uint32_t evb) {
    uint16_t out = 0;
    for (int s = 0; s < 15; s += 5) {
        uint32_t c = (((a >> s) & 31u) * eva + ((b >> s) & 31u) * evb) >> 4;
        out |= (uint16_t)((c > 31 ? 31 : c) << s);
    }
    return out;
}

static uint16_t brighten(uint16_t a, uint32_t evy) {
    uint16_t out = 0;
    for (int s = 0; s < 15; s += 5) {
        uint32_t c = (a >> s) & 31u;
        out |= (uint16_t)((c + (((31 - c) * evy) >> 4)) << s);
    }
    return out;
}

static uint16_t darken(uint16_t a, uint32_t evy) {
    uint16_t out = 0;
    for (int s = 0; s < 15; s += 5) {
        uint32_t c = (a >> s) & 31u;
        out |= (uint16_t)((c - ((c * evy) >> 4)) << s);
    }
    return out;
}

void ppu_compose(gba_t *g, int y, const line_buffers_t *lb, uint16_t *dst) {
    uint16_t dispcnt = g->ppu.dispcnt;
    uint16_t winin = ppu_reg(g, 0x048), winout = ppu_reg(g, 0x04A);
    uint16_t win0h = ppu_reg(g, 0x040), win1h = ppu_reg(g, 0x042);
    bool win0 = (dispcnt & 0x2000) && in_span(ppu_reg(g, 0x044), y);
    bool win1 = (dispcnt & 0x4000) && in_span(ppu_reg(g, 0x046), y);
    bool objwin = (dispcnt & 0x8000) != 0;
    bool windowing = (dispcnt & 0xE000) != 0;
    uint16_t bldcnt = ppu_reg(g, 0x050), bldalpha = ppu_reg(g, 0x052);
    uint32_t mode = (bldcnt >> 6) & 3;
    uint32_t eva = cap16(bldalpha & 0x1F), evb = cap16((bldalpha >> 8) & 0x1F);
    uint32_t evy = cap16(ppu_reg(g, 0x054) & 0x1F);
    uint16_t backdrop = pal_color(g, 0);

    /* No windows and no colour effects: only semi-transparent sprites could still blend. */
    if (!windowing && mode == 0) {
        bool semi_obj = false;
        for (int x = 0; x < PPU_WIDTH && !semi_obj; x++) semi_obj = (lb->obj[x].flags & PX_SEMI) != 0;
        if (!semi_obj) {
            for (int x = 0; x < PPU_WIDTH; x++) dst[x] = find_top(lb, x, backdrop);
            return;
        }
    }

    for (int x = 0; x < PPU_WIDTH; x++) {
        uint32_t mask = 0x3F; /* bits 0-3 BG0-BG3, bit 4 OBJ, bit 5 colour effects */
        if (windowing) {
            if (win0 && in_span(win0h, x)) mask = winin & 0x3F;
            else if (win1 && in_span(win1h, x)) mask = (winin >> 8) & 0x3F;
            else if (objwin && lb->objwin[x]) mask = (winout >> 8) & 0x3F;
            else mask = winout & 0x3F;
        }

        int layer[2];
        uint16_t color[2];
        bool semi;
        find_top_two(lb, x, mask, backdrop, layer, color, &semi);

        uint16_t out = color[0];
        if (mask & 0x20) {
            bool first = (bldcnt >> layer[0]) & 1;
            bool second = (bldcnt >> (8 + layer[1])) & 1;
            if (semi && second) {
                out = blend(color[0], color[1], eva, evb); /* semi-transparent sprites always blend */
            } else if (first) {
                if (mode == 1 && second) out = blend(color[0], color[1], eva, evb);
                else if (mode == 2) out = brighten(color[0], evy);
                else if (mode == 3) out = darken(color[0], evy);
            }
        }
        dst[x] = out;
    }
}
```

- [x] **Step 2: Run every PC test and the scenes**

```bash
bash tools/dev.sh unit 2>&1 | tail -1
bash tools/dev.sh test 2>&1 | grep -E "tests passed|tests failed"
bash tools/dev.sh bench --runs 2 --check
```

Expected: `410 tests, 0 failed`; `100% tests passed, 0 tests failed out of 120`; 6 scenes `ok ok` (on the PC game A and game C about 1.4-1.6× faster than in Task 1).

- [x] **Step 3: Commit**

```bash
git add core/src/ppu/ppu_bg.c core/src/ppu/ppu_compose.c docs/PROGRESS.md docs/superpowers/plans/2026-10-03-milestone-7a-speed.md
git commit -m "perf(core): text backgrounds by tile row, the compositor looks at each layer once, bitmap rows" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 4: CPU speed-ups

**Files:**
- Modify: `core/src/mem/bus.c` (instruction fetches and EWRAM/IWRAM/ROM data accesses straight from memory), `core/src/cpu/cpu.c` (ARM conditions from a table; the IRQ check only when a request is pending)

- [x] **Step 1: Write the code**

Replace `core/src/mem/bus.c`:

```c
#include "mem/bus.h"

#include <string.h>

#include "gba_internal.h"

static inline uint32_t load_n(const uint8_t *p, unsigned size) {
    switch (size) {
    case 1: return p[0];
    case 2: return (uint32_t)p[0] | (uint32_t)p[1] << 8;
    default: return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
    }
}

static inline void store_n(uint8_t *p, uint32_t v, unsigned size) {
    p[0] = (uint8_t)v;
    if (size >= 2) p[1] = (uint8_t)(v >> 8);
    if (size == 4) {
        p[2] = (uint8_t)(v >> 16);
        p[3] = (uint8_t)(v >> 24);
    }
}

/* The size-byte lane of a 32-bit bus value that an address selects. */
static inline uint32_t lane(uint32_t word, uint32_t addr, unsigned size) {
    switch (size) {
    case 1: return (word >> ((addr & 3) * 8)) & 0xFF;
    case 2: return (word >> ((addr & 2) * 8)) & 0xFFFF;
    default: return word;
    }
}

/* 96 KiB of VRAM in a 128 KiB window: 0x18000-0x1FFFF mirrors 0x10000-0x17FFF. */
static inline uint32_t vram_offset(uint32_t addr) {
    uint32_t off = addr & 0x1FFFF;
    return off >= 0x18000 ? off - 0x8000 : off;
}

/* The save area has an 8-bit bus: accesses of every size keep their exact address. */
static inline bool save_area(uint32_t addr) {
    return (addr >> 24) == 0xE || (addr >> 24) == 0xF;
}

/* A halfword of the prefetch pipeline: real memory only, no side effects or cycles. Anything else
 * (unmapped space, I/O, the save area) falls back to the last fetched opcode, so a PC in unmapped
 * space cannot recurse into open bus. */
static uint32_t pipeline16(gba_t *g, uint32_t addr) {
    bus_t *b = &g->bus;
    addr &= ~1u;
    switch (addr >> 24) {
    case 0x0:
        if (addr < BIOS_SIZE && b->in_bios) return load_n(&b->bios[addr], 2);
        break;
    case 0x2: return load_n(&b->ewram[addr & (EWRAM_SIZE - 1)], 2);
    case 0x3: return load_n(&b->iwram[addr & (IWRAM_SIZE - 1)], 2);
    case 0x5: return load_n(&b->pal[addr & (PAL_SIZE - 1)], 2);
    case 0x6: return load_n(&b->vram[vram_offset(addr)], 2);
    case 0x7: return load_n(&b->oam[addr & (OAM_SIZE - 1)], 2);
    case 0x8:
    case 0x9:
    case 0xA:
    case 0xB:
    case 0xC:
    case 0xD: return cart_rom_read(&g->cart, addr, 2);
    default: break;
    }
    return lane(b->open_bus, addr, 2);
}

uint32_t bus_open_bus(gba_t *g) {
    const arm_cpu_t *c = &g->cpu;
    uint32_t next = c->r[15]; /* while an instruction runs: its address + 8 (ARM) or + 4 (Thumb) */
    if (!(c->cpsr & CPSR_T)) return pipeline16(g, next) | pipeline16(g, next + 2) << 16;
    uint32_t pc = next - 4; /* $, the Thumb instruction being executed */
    switch (pc >> 24) {
    case 0x0: /* BIOS and OAM: 32-bit bus */
    case 0x7:
        return (pc & 2) ? pipeline16(g, pc + 2) | pipeline16(g, pc + 4) << 16
                        : pipeline16(g, pc + 4) | pipeline16(g, pc + 6) << 16;
    case 0x3: /* IWRAM */
        return (pc & 2) ? pipeline16(g, pc + 2) | pipeline16(g, pc + 4) << 16
                        : pipeline16(g, pc + 4) | pipeline16(g, pc + 2) << 16;
    default: { /* EWRAM, palette, VRAM, ROM: 16-bit bus */
        uint32_t half = pipeline16(g, pc + 4);
        return half | half << 16;
    }
    }
}

static uint32_t mem_read(gba_t *g, uint32_t addr, unsigned size) {
    bus_t *b = &g->bus;
    switch (addr >> 24) {
    case 0x0:
        if (addr < BIOS_SIZE) {
            uint32_t word = b->in_bios ? load_n(&b->bios[addr & 0x3FFC], 4) : b->bios_latch;
            return lane(word, addr, size);
        }
        return lane(bus_open_bus(g), addr, size);
    case 0x2: return load_n(&b->ewram[addr & (EWRAM_SIZE - 1)], size);
    case 0x3: return load_n(&b->iwram[addr & (IWRAM_SIZE - 1)], size);
    case 0x4: return io_read(g, addr, size);
    case 0x5: return load_n(&b->pal[addr & (PAL_SIZE - 1)], size);
    case 0x6: return load_n(&b->vram[vram_offset(addr)], size);
    case 0x7: return load_n(&b->oam[addr & (OAM_SIZE - 1)], size);
    case 0x8:
    case 0x9:
    case 0xA:
    case 0xB:
    case 0xC:
    case 0xD:
        if ((addr >> 24) == 0xD && save_eeprom_mapped(g, addr)) return save_eeprom_read(g);
        if (gpio_mapped(g, addr)) return gpio_read(g, addr, size);
        return cart_rom_read(&g->cart, addr, size);
    case 0xE:
    case 0xF: {
        uint32_t byte = save_read8(g, addr); /* wider reads repeat the byte */
        return size == 1 ? byte : size == 2 ? byte * 0x0101u : byte * 0x01010101u;
    }
    default: return lane(bus_open_bus(g), addr, size);
    }
}

static void mem_write(gba_t *g, uint32_t addr, uint32_t v, unsigned size) {
    bus_t *b = &g->bus;
    switch (addr >> 24) {
    case 0x2: store_n(&b->ewram[addr & (EWRAM_SIZE - 1)], v, size); break;
    case 0x3: store_n(&b->iwram[addr & (IWRAM_SIZE - 1)], v, size); break;
    case 0x4: io_write(g, addr, v, size); break;
    case 0x5:
        if (size == 1) store_n(&b->pal[addr & (PAL_SIZE - 2)], (v & 0xFF) * 0x0101u, 2);
        else store_n(&b->pal[addr & (PAL_SIZE - 1)], v, size);
        break;
    case 0x6: {
        uint32_t off = vram_offset(addr);
        if (size == 1) {
            /* Byte writes land as a doubled halfword in BG VRAM and are dropped in OBJ VRAM. */
            uint32_t bg_end = (g->ppu.dispcnt & 7) >= 3 ? 0x14000 : 0x10000;
            if (off < bg_end) store_n(&b->vram[off & ~1u], (v & 0xFF) * 0x0101u, 2);
        } else {
            store_n(&b->vram[off], v, size);
        }
        break;
    }
    case 0x7:
        if (size != 1) store_n(&b->oam[addr & (OAM_SIZE - 1)], v, size); /* byte writes are ignored */
        break;
    case 0x8:
    case 0x9:
    case 0xA:
    case 0xB:
    case 0xC:
    case 0xD:
        if ((addr >> 24) == 0xD && save_eeprom_mapped(g, addr)) save_eeprom_write(g, v); /* bit 0 */
        else if (gpio_mapped(g, addr)) gpio_write(g, addr, v, size);
        break;
    case 0xE:
    case 0xF: save_write8(g, addr, (uint8_t)(v >> (8 * (addr & (size - 1))))); break; /* one byte, by address */
    default: break; /* BIOS, ROM and unmapped space ignore writes */
    }
}

/* Data reads: EWRAM, IWRAM and plain ROM straight from memory, the rest through mem_read. */
static inline uint32_t data_read(gba_t *g, uint32_t addr, unsigned size) {
    switch (addr >> 24) {
    case 0x2: return load_n(&g->bus.ewram[addr & (EWRAM_SIZE - 1)], size);
    case 0x3: return load_n(&g->bus.iwram[addr & (IWRAM_SIZE - 1)], size);
    case 0x8:
    case 0x9:
    case 0xA:
    case 0xB:
    case 0xC: {
        uint32_t off = addr & 0x01FFFFFFu;
        if (off >= 0x100 && off + size <= g->cart.rom_size) return load_n(g->cart.rom + off, size);
        return mem_read(g, addr, size);
    }
    default: return mem_read(g, addr, size);
    }
}

/* Data writes: EWRAM and IWRAM straight to memory, the rest through mem_write. */
static inline void data_write(gba_t *g, uint32_t addr, uint32_t v, unsigned size) {
    switch (addr >> 24) {
    case 0x2: store_n(&g->bus.ewram[addr & (EWRAM_SIZE - 1)], v, size); break;
    case 0x3: store_n(&g->bus.iwram[addr & (IWRAM_SIZE - 1)], v, size); break;
    default: mem_write(g, addr, v, size); break;
    }
}

static inline uint32_t access_cycles(const gba_t *g, uint32_t addr, unsigned size, bool seq) {
    uint32_t region = addr >> 24;
    if (region > 0xF) return 1;
    return g->bus.cycles[region][size == 4][seq];
}

uint32_t bus_read8(gba_t *g, uint32_t addr, bool seq) {
    if (dma_bus_due(&g->dma, g->now)) dma_run_immediate(g);
    g->bus.data_access = true;
    uint32_t cyc = access_cycles(g, addr, 1, seq);
    g->now += cyc;
    if (addr < 0x08000000u) { /* not the cartridge: the prefetch buffer may use the bus meanwhile */
        g->bus.off_rom += cyc;
        g->bus.prefetch_hint = true;
    }
    return data_read(g, addr, 1);
}

uint32_t bus_read16(gba_t *g, uint32_t addr, bool seq) {
    if (dma_bus_due(&g->dma, g->now)) dma_run_immediate(g);
    g->bus.data_access = true;
    if (!save_area(addr)) addr &= ~1u; /* the save area keeps exact addresses */
    uint32_t cyc = access_cycles(g, addr, 2, seq);
    g->now += cyc;
    if (addr < 0x08000000u) { /* not the cartridge: the prefetch buffer may use the bus meanwhile */
        g->bus.off_rom += cyc;
        g->bus.prefetch_hint = true;
    }
    return data_read(g, addr, 2);
}

uint32_t bus_read32(gba_t *g, uint32_t addr, bool seq) {
    if (dma_bus_due(&g->dma, g->now)) dma_run_immediate(g);
    g->bus.data_access = true;
    if (!save_area(addr)) addr &= ~3u; /* the save area keeps exact addresses */
    uint32_t cyc = access_cycles(g, addr, 4, seq);
    g->now += cyc;
    if (addr < 0x08000000u) { /* not the cartridge: the prefetch buffer may use the bus meanwhile */
        g->bus.off_rom += cyc;
        g->bus.prefetch_hint = true;
    }
    return data_read(g, addr, 4);
}

void bus_write8(gba_t *g, uint32_t addr, uint8_t value, bool seq) {
    if (dma_bus_due(&g->dma, g->now)) dma_run_immediate(g);
    g->bus.data_access = true;
    uint32_t cyc = access_cycles(g, addr, 1, seq);
    g->now += cyc;
    if (addr < 0x08000000u) { /* not the cartridge: the prefetch buffer may use the bus meanwhile */
        g->bus.off_rom += cyc;
        g->bus.prefetch_hint = true;
    }
    data_write(g, addr, value, 1);
}

void bus_write16(gba_t *g, uint32_t addr, uint16_t value, bool seq) {
    if (dma_bus_due(&g->dma, g->now)) dma_run_immediate(g);
    g->bus.data_access = true;
    if (!save_area(addr)) addr &= ~1u; /* the save area keeps exact addresses */
    uint32_t cyc = access_cycles(g, addr, 2, seq);
    g->now += cyc;
    if (addr < 0x08000000u) { /* not the cartridge: the prefetch buffer may use the bus meanwhile */
        g->bus.off_rom += cyc;
        g->bus.prefetch_hint = true;
    }
    data_write(g, addr, value, 2);
}

void bus_write32(gba_t *g, uint32_t addr, uint32_t value, bool seq) {
    if (dma_bus_due(&g->dma, g->now)) dma_run_immediate(g);
    g->bus.data_access = true;
    if (!save_area(addr)) addr &= ~3u; /* the save area keeps exact addresses */
    uint32_t cyc = access_cycles(g, addr, 4, seq);
    g->now += cyc;
    if (addr < 0x08000000u) { /* not the cartridge: the prefetch buffer may use the bus meanwhile */
        g->bus.off_rom += cyc;
        g->bus.prefetch_hint = true;
    }
    data_write(g, addr, value, 4);
}

/* Where an instruction at addr can be read straight from memory: EWRAM, IWRAM and the ROM itself (not past
 * its end, not the GPIO port at 0x080000C4, not region 0xD where an EEPROM may answer); NULL for the rest. */
static inline const uint8_t *code_ptr(const gba_t *g, uint32_t addr) {
    switch (addr >> 24) {
    case 0x2: return &g->bus.ewram[addr & (EWRAM_SIZE - 1)];
    case 0x3: return &g->bus.iwram[addr & (IWRAM_SIZE - 1)];
    case 0x8:
    case 0x9:
    case 0xA:
    case 0xB:
    case 0xC: {
        uint32_t off = addr & 0x01FFFFFFu;
        return off >= 0x100 && off + 4 <= g->cart.rom_size ? g->cart.rom + off : NULL;
    }
    default: return NULL;
    }
}

uint32_t bus_fetch32(gba_t *g, uint32_t addr, bool seq) {
    if (dma_bus_due(&g->dma, g->now)) dma_run_immediate(g);
    bus_t *b = &g->bus;
    const uint8_t *p = code_ptr(g, addr & ~3u);
    if (p) {
        b->in_bios = false;
        g->now += access_cycles(g, addr & ~3u, 4, seq);
        return b->open_bus = load_n(p, 4);
    }
    b->in_bios = addr < BIOS_SIZE;
    addr &= ~3u;
    g->now += access_cycles(g, addr, 4, seq);
    uint32_t op = mem_read(g, addr, 4);
    /* The BIOS latch holds the last BIOS word fetched: two instructions ahead in the pipeline. */
    if (b->in_bios) b->bios_latch = load_n(&b->bios[(addr + 8) & 0x3FFC], 4);
    b->open_bus = op;
    return op;
}

uint32_t bus_fetch16(gba_t *g, uint32_t addr, bool seq) {
    if (dma_bus_due(&g->dma, g->now)) dma_run_immediate(g);
    bus_t *b = &g->bus;
    const uint8_t *p = code_ptr(g, addr & ~1u);
    if (p) {
        b->in_bios = false;
        g->now += access_cycles(g, addr & ~1u, 2, seq);
        uint32_t op = load_n(p, 2);
        b->open_bus = op | op << 16;
        return op;
    }
    b->in_bios = addr < BIOS_SIZE;
    addr &= ~1u;
    g->now += access_cycles(g, addr, 2, seq);
    uint32_t op = mem_read(g, addr, 2);
    if (b->in_bios) b->bios_latch = load_n(&b->bios[(addr + 4) & 0x3FFC], 4);
    b->open_bus = op | op << 16;
    return op;
}

void bus_prefetch_stall(gba_t *g) {
    bus_t *b = &g->bus;
    uint32_t pc = g->cpu.r[15];
    uint32_t region = pc >> 24;
    if (!(b->waitcnt & 0x4000) || region < 0x8 || region > 0xD) return;
    int32_t s = (int32_t)b->cycles[region][0][1] - 1; /* the cartridge's sequential wait states (16-bit) */
    int32_t wait = (int32_t)b->off_rom;
    int32_t previous = 0, max_loads = 8;
    uint32_t dist = b->prefetch_end - pc; /* halfwords still buffered from an earlier stall */
    if (dist < 16) {
        previous = (int32_t)(dist >> 1);
        max_loads -= previous;
    }
    int32_t stall = s + 1, loads = 1;
    while (stall < wait && loads < max_loads) {
        stall += s;
        loads++;
    }
    b->prefetch_end = pc + 2u * (uint32_t)(loads + previous - 1);
    int32_t used = stall > wait ? stall : wait; /* the wait cannot be shorter than the prefetch */
    g->now = (uint64_t)((int64_t)g->now + (used - wait) - stall);
    g->cpu.next_fetch_seq = true; /* the next fetch comes out of the buffer: sequential */
}

uint32_t bus_code_cycles(const gba_t *g, uint32_t addr, unsigned size, bool seq) {
    return access_cycles(g, addr, size, seq);
}

void bus_idle(gba_t *g, uint32_t cycles) {
    g->now += cycles;
    g->bus.off_rom += cycles;
}

uint32_t bus_peek(gba_t *g, uint32_t addr, unsigned size) {
    return mem_read(g, addr & ~(uint32_t)(size - 1), size);
}

void bus_poke(gba_t *g, uint32_t addr, uint32_t value, unsigned size) {
    uint32_t region = addr >> 24;
    if (region >= 0x2 && region <= 0x7) mem_write(g, addr & ~(uint32_t)(size - 1), value, size);
}

static void set_region(bus_t *b, int region, uint8_t n16, uint8_t s16, uint8_t n32, uint8_t s32) {
    b->cycles[region][0][0] = n16;
    b->cycles[region][0][1] = s16;
    b->cycles[region][1][0] = n32;
    b->cycles[region][1][1] = s32;
}

void bus_set_waitcnt(gba_t *g, uint16_t value) {
    static const uint8_t nonseq[4] = {4, 3, 2, 8};
    static const uint8_t seq[3][2] = {{2, 1}, {4, 1}, {8, 1}};
    bus_t *b = &g->bus;
    b->waitcnt = value & 0x7FFF;
    for (int ws = 0; ws < 3; ws++) {
        uint8_t n = (uint8_t)(1 + nonseq[(value >> (2 + ws * 3)) & 3]);
        uint8_t s = (uint8_t)(1 + seq[ws][(value >> (4 + ws * 3)) & 1]);
        /* The cartridge bus is 16 bits wide: a 32-bit access is one N plus one S access. */
        set_region(b, 0x8 + ws * 2, n, s, (uint8_t)(n + s), (uint8_t)(2 * s));
        set_region(b, 0x9 + ws * 2, n, s, (uint8_t)(n + s), (uint8_t)(2 * s));
    }
    uint8_t sram = (uint8_t)(1 + nonseq[value & 3]);
    set_region(b, 0xE, sram, sram, sram, sram);
    set_region(b, 0xF, sram, sram, sram, sram);
}

void bus_reset(gba_t *g) {
    bus_t *b = &g->bus;
    memset(b->ewram, 0, sizeof b->ewram);
    memset(b->iwram, 0, sizeof b->iwram);
    memset(b->pal, 0, sizeof b->pal);
    memset(b->vram, 0, sizeof b->vram);
    memset(b->oam, 0, sizeof b->oam);
    set_region(b, 0x0, 1, 1, 1, 1); /* BIOS */
    set_region(b, 0x1, 1, 1, 1, 1);
    set_region(b, 0x2, 3, 3, 6, 6); /* EWRAM: 16-bit bus, 2 wait states */
    set_region(b, 0x3, 1, 1, 1, 1); /* IWRAM */
    set_region(b, 0x4, 1, 1, 1, 1); /* I/O */
    set_region(b, 0x5, 1, 1, 2, 2); /* palette: 16-bit bus */
    set_region(b, 0x6, 1, 1, 2, 2); /* VRAM: 16-bit bus */
    set_region(b, 0x7, 1, 1, 1, 1); /* OAM */
    bus_set_waitcnt(g, 0);
    b->in_bios = false;
    b->bios_latch = 0;
    b->open_bus = 0;
}
```

Replace `core/src/cpu/cpu.c`:

```c
#include "cpu/cpu.h"

#include <string.h>

#include "gba_internal.h"

/* cond_table[cond] bit n: the condition passes when the flags NZCV are n (built by cpu_init_tables). */
static uint16_t cond_table[16];

int cpu_mode_bank(uint32_t mode) {
    switch (mode & CPSR_MODE) {
    case MODE_FIQ: return BANK_FIQ;
    case MODE_IRQ: return BANK_IRQ;
    case MODE_SVC: return BANK_SVC;
    case MODE_ABT: return BANK_ABT;
    case MODE_UND: return BANK_UND;
    default: return BANK_USR; /* USR, SYS and invalid mode values */
    }
}

bool cpu_has_spsr(const arm_cpu_t *c) {
    return cpu_mode_bank(c->cpsr) != BANK_USR;
}

static void switch_bank(arm_cpu_t *c, int from, int to) {
    if (from == to) return;
    if (from == BANK_FIQ || to == BANK_FIQ) {
        memcpy(from == BANK_FIQ ? c->bank_fiq_r8_12 : c->bank_usr_r8_12, &c->r[8], sizeof c->bank_usr_r8_12);
        memcpy(&c->r[8], to == BANK_FIQ ? c->bank_fiq_r8_12 : c->bank_usr_r8_12, sizeof c->bank_usr_r8_12);
    }
    c->bank_r13[from] = c->r[13];
    c->bank_r14[from] = c->r[14];
    c->r[13] = c->bank_r13[to];
    c->r[14] = c->bank_r14[to];
}

void cpu_write_cpsr(arm_cpu_t *c, uint32_t value) {
    switch_bank(c, cpu_mode_bank(c->cpsr), cpu_mode_bank(value));
    c->cpsr = value;
}

void cpu_branch(arm_cpu_t *c, uint32_t target) {
    c->r[15] = target;
    c->branched = true;
    c->next_fetch_seq = false;
}

void cpu_exception(gba_t *g, uint32_t vector, uint32_t mode, uint32_t return_addr) {
    arm_cpu_t *c = &g->cpu;
    uint32_t old = c->cpsr;
    cpu_write_cpsr(c, (old & ~(CPSR_MODE | CPSR_T)) | mode | CPSR_I);
    c->spsr[cpu_mode_bank(mode)] = old;
    c->r[14] = return_addr;
    cpu_branch(c, vector);
}

bool cpu_cond_passed(uint32_t cpsr, uint32_t cond) {
    bool n = (cpsr & CPSR_N) != 0, z = (cpsr & CPSR_Z) != 0;
    bool cf = (cpsr & CPSR_C) != 0, v = (cpsr & CPSR_V) != 0;
    switch (cond) {
    case 0x0: return z;
    case 0x1: return !z;
    case 0x2: return cf;
    case 0x3: return !cf;
    case 0x4: return n;
    case 0x5: return !n;
    case 0x6: return v;
    case 0x7: return !v;
    case 0x8: return cf && !z;
    case 0x9: return !cf || z;
    case 0xA: return n == v;
    case 0xB: return n != v;
    case 0xC: return !z && n == v;
    case 0xD: return z || n != v;
    case 0xE: return true;
    default: return false; /* NV: never executes on ARMv4 */
    }
}

void cpu_init_tables(void) {
    for (uint32_t cond = 0; cond < 16; cond++) {
        cond_table[cond] = 0;
        for (uint32_t nzcv = 0; nzcv < 16; nzcv++)
            if (cpu_cond_passed(nzcv << 28, cond)) cond_table[cond] |= (uint16_t)(1u << nzcv);
    }
    arm_init_table();
    thumb_init_table();
}

void cpu_reset(gba_t *g) {
    memset(&g->cpu, 0, sizeof g->cpu);
    g->cpu.cpsr = MODE_SVC | CPSR_I | CPSR_F;
}

void cpu_step(gba_t *g) {
    arm_cpu_t *c = &g->cpu;
    /* LR_irq = next instruction + 4, so the handler's "subs pc, lr, #4" resumes it. */
    if (g->irq.pending && irq_line(g) && !(c->cpsr & CPSR_I)) {
        cpu_exception(g, 0x18, MODE_IRQ, c->r[15] + 4);
        g->now += bus_code_cycles(g, 0x18, 4, false) + bus_code_cycles(g, 0x1C, 4, true); /* pipeline refill */
        c->next_fetch_seq = true;
    }

    uint32_t pc = c->r[15];
    c->branched = false;
    g->bus.data_access = false;
    g->bus.prefetch_hint = false;
    g->bus.off_rom = 0;
    if (c->cpsr & CPSR_T) {
        uint16_t op = (uint16_t)bus_fetch16(g, pc, c->next_fetch_seq);
        c->next_fetch_seq = true;
        c->r[15] = pc + 4;
        thumb_execute(g, op);
        if (!c->branched) c->r[15] = pc + 2;
    } else {
        uint32_t op = bus_fetch32(g, pc, c->next_fetch_seq);
        c->next_fetch_seq = true;
        c->r[15] = pc + 8;
        if ((cond_table[op >> 28] >> (c->cpsr >> 28)) & 1) arm_execute(g, op);
        if (!c->branched) c->r[15] = pc + 4;
    }
    if (c->branched) { /* pipeline refill: N at the target, S after it; the next fetch is then sequential */
        unsigned size = (c->cpsr & CPSR_T) ? 2 : 4;
        g->now += bus_code_cycles(g, c->r[15], size, false) + bus_code_cycles(g, c->r[15] + size, size, true);
        c->next_fetch_seq = true;
        g->bus.prefetch_end = 0; /* a branch empties the prefetch buffer */
    } else if (g->bus.data_access) {
        c->next_fetch_seq = false; /* the bus was used for data (or the multiplier): the next fetch is non-sequential */
        if (g->bus.prefetch_hint) bus_prefetch_stall(g);
    }
}
```

- [x] **Step 2: Run every PC test and the scenes**

```bash
bash tools/dev.sh unit 2>&1 | tail -1
bash tools/dev.sh test 2>&1 | grep -E "tests passed|tests failed"
bash tools/dev.sh bench --runs 2 --check
```

Expected: `410 tests, 0 failed`; CTest 120/120; 6 scenes `ok ok` (game B and game D about 1.3× faster on the PC than after Task 3).

- [x] **Step 3: Commit**

```bash
git add core/src/mem/bus.c core/src/cpu/cpu.c docs/PROGRESS.md docs/superpowers/plans/2026-10-03-milestone-7a-speed.md
git commit -m "perf(core): instruction fetch and RAM/ROM data straight from memory, ARM condition table" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

**Batch 1 (Tasks 1-4) done** — report briefly and carry on (no stop).

---
### Task 5: Link-time optimisation on Android

**Files:**
- Modify: `platform/android/CMakeLists.txt`

(`phone-bench` builds with link-time optimisation since Task 1; this gives the app the same.)

- [x] **Step 1: Write the code**

Replace `platform/android/CMakeLists.txt`:

```cmake
# Native library for the Android app (built by Gradle's externalNativeBuild from app/build.gradle.kts).
cmake_minimum_required(VERSION 3.22)
project(gbaemu LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# The emulator must run at full speed in the debug APK too: optimise, keep debug symbols.
set(CMAKE_C_FLAGS_DEBUG "-O2 -g")
# Link-time optimisation: the CPU loop, the memory bus and the instruction handlers live in different files;
# letting the compiler inline across them made games 15-24 % faster on a phone's small core (milestone 7a).
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)

set(REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
add_subdirectory(${REPO_ROOT}/core ${CMAKE_BINARY_DIR}/core)

file(GLOB PLATFORM_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/*.c)
add_library(gbaemu SHARED ${PLATFORM_SOURCES})
target_include_directories(gbaemu PRIVATE ${CMAKE_CURRENT_LIST_DIR} ${REPO_ROOT}/tools/common)
target_compile_options(gbaemu PRIVATE -Wall -Wextra -Wno-unused-parameter -Werror=unguarded-availability)
# Newer-API calls (e.g. AChoreographer_postFrameCallback64, Android 10+) link weakly and must sit
# behind __builtin_available checks; minSdk is 26.
target_compile_definitions(gbaemu PRIVATE __ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__)
target_link_libraries(gbaemu PRIVATE gbacore android log EGL GLESv3 aaudio)
```

- [x] **Step 2: Build and run the on-device tests (emulator)**

```bash
rm -rf app/build/outputs/androidTest-results; bash tools/dev.sh androidtest 2>&1 | grep -E "^e: |Starting|BUILD FAILED"; grep -h -o 'tests="[0-9]*" failures="[0-9]*"' app/build/outputs/androidTest-results/connected/debug/*.xml
echo "C warnings: $(bash tools/dev.sh apk --rerun-tasks 2>&1 | grep -c 'warning:.*\.[ch]:')"
```
```bash
grep -o "\-flto=thin" -r app/.cxx --include=build.ninja | wc -l
```

Expected: `tests="53" failures="0"`; `C warnings: 0`; a count above 0 (the app's native code is compiled with `-flto=thin`).

- [x] **Step 3: Commit**

```bash
git add platform/android/CMakeLists.txt docs/PROGRESS.md docs/superpowers/plans/2026-10-03-milestone-7a-speed.md
git commit -m "perf(android): link-time optimisation for the native library" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 6: Show speed

**Files:**
- Create: `app/src/main/java/com/tengen/gbaemu/SpeedMeter.kt`, `app/src/test/java/com/tengen/gbaemu/SpeedMeterTest.kt`
- Modify: `app/src/main/java/com/tengen/gbaemu/Settings.kt` (`showSpeed`, key `show_speed`), `app/src/test/java/com/tengen/gbaemu/SettingsTest.kt`, `app/src/main/java/com/tengen/gbaemu/SettingsScreens.kt` (Picture → Show speed On/Off + a note), `app/src/main/java/com/tengen/gbaemu/Screens.kt` (`GameScreen`'s `showSpeed`: a label at the top middle), `app/src/main/java/com/tengen/gbaemu/MainActivity.kt` (passes the setting)

- [x] **Step 1: Write the failing tests**

Create `app/src/test/java/com/tengen/gbaemu/SpeedMeterTest.kt`:

```kotlin
package com.tengen.gbaemu

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class SpeedMeterTest {
    private val second = 1_000_000_000L

    @Test
    fun nothingUntilASecondHasBeenMeasured() {
        val m = SpeedMeter()
        assertNull(m.sample(0, 100))
        assertNull(m.sample(second / 2, 130))
        assertEquals("60 fps · 1.0×", m.sample(second, 160))
    }

    @Test
    fun fastForwardShowsMoreFramesAndASpeedUp() {
        val m = SpeedMeter()
        m.sample(0, 0)
        assertEquals("239 fps · 4.0×", m.sample(second, 239))
        assertEquals("239 fps · 4.0×", m.sample(second + second / 3, 300)) // under a second since: unchanged
        assertEquals("30 fps · 0.5×", m.sample(2 * second, 269))
    }

    @Test
    fun aNewGameStartsTheMeasureAgain() {
        val m = SpeedMeter()
        m.sample(0, 5000)
        assertEquals("60 fps · 1.0×", m.sample(second, 5060))
        assertEquals("60 fps · 1.0×", m.sample(2 * second, 10)) // the count restarted: the old label stays
        assertEquals("120 fps · 2.0×", m.sample(3 * second, 130))
    }
}
```

Replace `app/src/test/java/com/tengen/gbaemu/SettingsTest.kt`:

```kotlin
package com.tengen.gbaemu

import org.junit.Assert.assertEquals
import org.junit.Test

class SettingsTest {
    @Test
    fun nothingStoredMeansTheDefaultsWhichAreTodaysBehaviour() {
        val s = Settings.load(MemoryStore())
        assertEquals(Settings(), s)
        assertEquals(false, s.smooth)
        assertEquals(Settings.SIZE_FIT, s.size)
        assertEquals(true, s.sound)
        assertEquals(2, s.soundBursts)
        assertEquals(4, s.fastSpeed)
        assertEquals(64L * 1024 * 1024, s.rewindBudget)
        assertEquals(Settings.LOOK_NORMAL, s.touchLook)
        assertEquals(true, s.buzz)
        assertEquals(false, s.biosFile)
        assertEquals(false, s.showSpeed)
    }

    @Test
    fun everySettingIsSavedAndReadBack() {
        val store = MemoryStore()
        val s = Settings(smooth = true, size = Settings.SIZE_STRETCH, sound = false, delay = Settings.DELAY_HIGH,
            fastSpeed = NativeBridge.FF_MAX, rewind = false, rewindLength = Settings.LENGTH_LONG,
            touchLook = Settings.LOOK_STRONG, buzz = false, biosFile = true, showSpeed = true)
        s.save(store)
        assertEquals(s, Settings.load(store))
    }

    @Test
    fun theFastForwardSpeedStoredBefore6bIsKept() {
        val store = MemoryStore()
        store.put("fast_speed", 8) // written by the in-game menu row since 5b
        assertEquals(8, Settings.load(store).fastSpeed)
    }

    @Test
    fun aValueThatDoesNotReadIsItsDefault() {
        val store = MemoryStore()
        for ((key, value) in listOf("size" to 9, "delay" to -2, "fast_speed" to 3, "rewind_length" to 7, "touch_look" to 5,
            "smooth" to 4)) store.put(key, value)
        assertEquals(Settings(), Settings.load(store))
    }

    @Test
    fun theDelayAndTheRewindLengthMeanBurstsAndBytes() {
        assertEquals(listOf(2, 4, 8), listOf(Settings.DELAY_LOW, Settings.DELAY_MEDIUM, Settings.DELAY_HIGH)
            .map { Settings(delay = it).soundBursts })
        assertEquals(listOf(24L, 64L, 128L).map { it * 1024 * 1024 },
            listOf(Settings.LENGTH_SHORT, Settings.LENGTH_NORMAL, Settings.LENGTH_LONG).map { Settings(rewindLength = it).rewindBudget })
        assertEquals(0L, Settings(rewind = false).rewindBudget)
    }
}
```

- [x] **Step 2: Run to see them fail**

```bash
bash tools/dev.sh jvmtest
```

Expected: FAIL — `Unresolved reference 'SpeedMeter'`, `'showSpeed'`.

- [x] **Step 3: Write the code**

Create `app/src/main/java/com/tengen/gbaemu/SpeedMeter.kt`:

```kotlin
package com.tengen.gbaemu

import java.util.Locale

/**
 * The Show speed label (Settings → Picture): "60 fps · 1.0×" from the frames the emulator ran in about the last
 * second — fast-forward shows more, rewinding (no frames run) less. [sample] it every so often with the time and
 * the frame count (NativeBridge.debugFrameCount); the label changes once a second.
 */
class SpeedMeter {
    private var lastNanos = 0L
    private var lastFrames = -1L

    /** null until a second has been measured. */
    var label: String? = null
        private set

    fun sample(nanos: Long, frames: Long): String? {
        if (lastFrames < 0 || frames < lastFrames) { // the first sample, or a new game (the count restarted)
            lastNanos = nanos
            lastFrames = frames
            return label
        }
        val elapsed = nanos - lastNanos
        if (elapsed < SECOND) return label
        val fps = (frames - lastFrames) * SECOND.toDouble() / elapsed
        label = String.format(Locale.US, "%.0f fps · %.1f×", fps, fps / GBA_FPS)
        lastNanos = nanos
        lastFrames = frames
        return label
    }

    companion object {
        const val GBA_FPS = 59.7275
        private const val SECOND = 1_000_000_000L
    }
}
```

Replace `app/src/main/java/com/tengen/gbaemu/Settings.kt`:

```kotlin
package com.tengen.gbaemu

/**
 * The app-wide settings (Settings screen). The defaults are the app's behaviour before 6b. Stored in an [IntStore]
 * (the "settings" preferences); `fast_speed` is the key the in-game menu row has used since 5b, so it is kept.
 */
data class Settings(
    val smooth: Boolean = false,
    val size: Int = SIZE_FIT,
    val sound: Boolean = true,
    val delay: Int = DELAY_LOW,
    val fastSpeed: Int = 4,
    val rewind: Boolean = true,
    val rewindLength: Int = LENGTH_NORMAL,
    val touchLook: Int = LOOK_NORMAL,
    val buzz: Boolean = true,
    val biosFile: Boolean = false, // a BIOS file was chosen (Bios.file); the next game starts through it
    val showSpeed: Boolean = false, // the frames per second label on the game screen (7a)
) {
    /** AAudio's own buffer, in bursts. */
    val soundBursts: Int get() = when (delay) {
        DELAY_MEDIUM -> 4
        DELAY_HIGH -> 8
        else -> 2
    }

    /** Bytes for rewind snapshots (0 = rewind off). */
    val rewindBudget: Long get() = if (!rewind) 0 else when (rewindLength) {
        LENGTH_SHORT -> 24L * MIB
        LENGTH_LONG -> 128L * MIB
        else -> 64L * MIB
    }

    fun save(store: IntStore) {
        store.put("smooth", flag(smooth))
        store.put("size", size)
        store.put("sound", flag(sound))
        store.put("delay", delay)
        store.put("fast_speed", fastSpeed)
        store.put("rewind", flag(rewind))
        store.put("rewind_length", rewindLength)
        store.put("touch_look", touchLook)
        store.put("buzz", flag(buzz))
        store.put("bios_file", flag(biosFile))
        store.put("show_speed", flag(showSpeed))
    }

    companion object {
        private const val MIB = 1024 * 1024

        const val SIZE_FIT = 0 // the numbers of fit_mode_t (platform/android/fit_rect.h)
        const val SIZE_INTEGER = 1
        const val SIZE_STRETCH = 2
        const val DELAY_LOW = 0
        const val DELAY_MEDIUM = 1
        const val DELAY_HIGH = 2
        const val LENGTH_SHORT = 0
        const val LENGTH_NORMAL = 1
        const val LENGTH_LONG = 2
        const val LOOK_NORMAL = 0
        const val LOOK_FAINT = 1
        const val LOOK_STRONG = 2

        /** The choices' names, by value. */
        val SIZE_NAMES = listOf("Fit screen", "Whole-number steps", "Stretch to fill")
        val DELAY_NAMES = listOf("Low", "Medium", "High")
        val LENGTH_NAMES = listOf("About 1 minute", "About 2½ minutes", "About 5 minutes")
        val LOOK_NAMES = listOf("Normal", "Faint", "Strong")
        val SPEEDS = listOf(2, 4, 8, NativeBridge.FF_MAX)

        fun speedName(speed: Int) = if (speed == NativeBridge.FF_MAX) "Max" else "$speed×"

        fun load(store: IntStore): Settings {
            val d = Settings()
            return Settings(
                smooth = bool(store, "smooth", d.smooth),
                size = choice(store, "size", 0..2, d.size),
                sound = bool(store, "sound", d.sound),
                delay = choice(store, "delay", 0..2, d.delay),
                fastSpeed = store.get("fast_speed")?.takeIf { it in SPEEDS } ?: d.fastSpeed,
                rewind = bool(store, "rewind", d.rewind),
                rewindLength = choice(store, "rewind_length", 0..2, d.rewindLength),
                touchLook = choice(store, "touch_look", 0..2, d.touchLook),
                buzz = bool(store, "buzz", d.buzz),
                biosFile = bool(store, "bios_file", d.biosFile),
                showSpeed = bool(store, "show_speed", d.showSpeed),
            )
        }

        private fun flag(b: Boolean) = if (b) 1 else 0

        private fun bool(store: IntStore, key: String, default: Boolean): Boolean = when (store.get(key)) {
            0 -> false
            1 -> true
            else -> default
        }

        private fun choice(store: IntStore, key: String, valid: IntRange, default: Int): Int =
            store.get(key)?.takeIf { it in valid } ?: default
    }
}
```

Replace `app/src/main/java/com/tengen/gbaemu/SettingsScreens.kt`:

```kotlin
package com.tengen.gbaemu

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.systemBarsPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/** The Settings screen's pages: the categories, then one page each. */
enum class SettingsPage(val title: String) {
    MAIN("Settings"), PICTURE("Picture"), SOUND("Sound"), SPEED("Fast-forward & rewind"), KEYS("Keyboard & controller"),
    TOUCH("Touch buttons"), BIOS("BIOS"),
}

/** A pick list waiting to be shown: its title, the choices, the one chosen now, and what picking does. */
private class Choice(val title: String, val options: List<String>, val selected: Int, val onPick: (Int) -> Unit)

/**
 * Settings (from the library's ⚙ / ⋮ and the in-game menu). [keysChanged] changes whenever the key bindings do, so
 * the key table is read again. Every change goes to [onChange] at once (saved and applied by the caller).
 */
@Composable
fun SettingsScreen(page: SettingsPage, settings: Settings, keys: KeyBindings, keysChanged: Int, biosName: String,
                   onPage: (SettingsPage) -> Unit, onBack: () -> Unit, onChange: (Settings) -> Unit,
                   onPickKey: (bit: Int, device: Int) -> Unit, onResetKeys: () -> Unit, onChooseBios: () -> Unit,
                   onBuiltInBios: () -> Unit, onEditLayout: (sideways: Boolean) -> Unit = {}) {
    var choosing by remember { mutableStateOf<Choice?>(null) }
    val s = settings
    val onOff = listOf("On", "Off")
    Surface(Modifier.fillMaxSize()) {
        Column(Modifier.fillMaxSize().systemBarsPadding()) {
            Row(Modifier.fillMaxWidth().padding(4.dp), verticalAlignment = Alignment.CenterVertically) {
                TextButton(onClick = onBack) { Text("←", fontSize = 22.sp) }
                Text(page.title, style = MaterialTheme.typography.titleLarge)
            }
            Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 16.dp)) {
                when (page) {
                    SettingsPage.MAIN -> {
                        Category("Picture", "${if (s.smooth) "Smooth" else "Sharp"} · ${Settings.SIZE_NAMES[s.size]}") { onPage(SettingsPage.PICTURE) }
                        Category("Sound", if (s.sound) "On · ${Settings.DELAY_NAMES[s.delay]} delay" else "Off") { onPage(SettingsPage.SOUND) }
                        Category("Fast-forward & rewind", Settings.speedName(s.fastSpeed) + " · " +
                            if (s.rewind) "Rewind ${Settings.LENGTH_NAMES[s.rewindLength].lowercase()}" else "Rewind off") { onPage(SettingsPage.SPEED) }
                        Category("Keyboard & controller", "Which key does what") { onPage(SettingsPage.KEYS) }
                        Category("Touch buttons", "${Settings.LOOK_NAMES[s.touchLook]} · Buzz ${if (s.buzz) "on" else "off"}") { onPage(SettingsPage.TOUCH) }
                        Category("BIOS", biosName) { onPage(SettingsPage.BIOS) }
                    }
                    SettingsPage.PICTURE -> {
                        val pixels = listOf("Sharp", "Smooth")
                        Setting("Pixels", pixels[if (s.smooth) 1 else 0]) {
                            choosing = Choice("Pixels", pixels, if (s.smooth) 1 else 0) { onChange(s.copy(smooth = it == 1)) }
                        }
                        Setting("Size", Settings.SIZE_NAMES[s.size]) {
                            choosing = Choice("Size", Settings.SIZE_NAMES, s.size) { onChange(s.copy(size = it)) }
                        }
                        Setting("Show speed", onOff[if (s.showSpeed) 0 else 1]) {
                            choosing = Choice("Show speed", onOff, if (s.showSpeed) 0 else 1) { onChange(s.copy(showSpeed = it == 0)) }
                        }
                        Note("Whole-number steps: every GBA pixel the same size (crisper, a little smaller). Sideways, the size matters most.")
                        Note("Show speed: frames per second at the top of the game, and how many times real speed (1.0× = normal).")
                    }
                    SettingsPage.SOUND -> {
                        Setting("Sound", onOff[if (s.sound) 0 else 1]) {
                            choosing = Choice("Sound", onOff, if (s.sound) 0 else 1) { onChange(s.copy(sound = it == 0)) }
                        }
                        Setting("Delay", Settings.DELAY_NAMES[s.delay]) {
                            choosing = Choice("Delay", Settings.DELAY_NAMES, s.delay) { onChange(s.copy(delay = it)) }
                        }
                        Note("Low is the quickest. If the sound crackles on a busy phone, try Medium or High.")
                    }
                    SettingsPage.SPEED -> {
                        val speeds = Settings.SPEEDS.map(Settings::speedName)
                        Setting("Fast-forward speed", Settings.speedName(s.fastSpeed)) {
                            choosing = Choice("Fast-forward speed", speeds, Settings.SPEEDS.indexOf(s.fastSpeed)) {
                                onChange(s.copy(fastSpeed = Settings.SPEEDS[it]))
                            }
                        }
                        Setting("Rewind", onOff[if (s.rewind) 0 else 1]) {
                            choosing = Choice("Rewind", onOff, if (s.rewind) 0 else 1) { onChange(s.copy(rewind = it == 0)) }
                        }
                        Setting("Rewind length", Settings.LENGTH_NAMES[s.rewindLength]) {
                            choosing = Choice("Rewind length", Settings.LENGTH_NAMES, s.rewindLength) { onChange(s.copy(rewindLength = it)) }
                        }
                        Note("Rewind uses up to 24, 64 or 128 MB. Rewind changes are used the next time a game opens.")
                    }
                    SettingsPage.KEYS -> KeyTable(keys, keysChanged, onPickKey, onResetKeys)
                    SettingsPage.TOUCH -> {
                        Setting("Sideways, over the picture", Settings.LOOK_NAMES[s.touchLook]) {
                            choosing = Choice("Sideways, over the picture", Settings.LOOK_NAMES, s.touchLook) { onChange(s.copy(touchLook = it)) }
                        }
                        Setting("Buzz", onOff[if (s.buzz) 0 else 1]) {
                            choosing = Choice("Buzz", onOff, if (s.buzz) 0 else 1) { onChange(s.copy(buzz = it == 0)) }
                        }
                        Setting("Edit upright layout…", "") { onEditLayout(false) }
                        Setting("Edit sideways layout…", "") { onEditLayout(true) }
                        Note("Buzz follows the phone's own touch-vibration setting. The layouts: move and size every button and the game picture.")
                    }
                    SettingsPage.BIOS -> {
                        Setting("BIOS", biosName) {}
                        Spacer(Modifier.height(12.dp))
                        OutlinedButton(onClick = onChooseBios, modifier = Modifier.fillMaxWidth()) { Text("Choose a BIOS file…") }
                        if (s.biosFile) {
                            OutlinedButton(onClick = onBuiltInBios, modifier = Modifier.fillMaxWidth()) { Text("Use the built-in BIOS") }
                        }
                        Note("A dump of your own GBA's BIOS (16 KB). Games then start with the Nintendo logo. Used the next time a game opens; save states made with the other BIOS can't be loaded.")
                    }
                }
            }
        }
    }
    choosing?.let { c -> ChoiceDialog(c.title, c.options, c.selected, onPick = { c.onPick(it); choosing = null }, onCancel = { choosing = null }) }
}

@Composable
private fun Category(title: String, summary: String, onClick: () -> Unit) {
    Column(Modifier.fillMaxWidth().clickable(onClick = onClick).padding(vertical = 12.dp)) {
        Text(title, style = MaterialTheme.typography.bodyLarge)
        Text(summary, fontSize = 13.sp, color = Color.Gray)
    }
    HorizontalDivider()
}

@Composable
private fun Setting(label: String, value: String, onClick: () -> Unit) {
    Row(Modifier.fillMaxWidth().clickable(onClick = onClick).padding(vertical = 14.dp),
        horizontalArrangement = Arrangement.SpaceBetween) {
        Text(label, style = MaterialTheme.typography.bodyLarge)
        Text(value, color = Color.Gray)
    }
    HorizontalDivider()
}

@Composable
private fun Note(text: String) {
    Text(text, fontSize = 13.sp, color = Color.Gray, modifier = Modifier.padding(vertical = 12.dp))
}

/** A pick list: tapping a choice picks it. */
@Composable
fun ChoiceDialog(title: String, options: List<String>, selected: Int, onPick: (Int) -> Unit, onCancel: () -> Unit) {
    AlertDialog(
        onDismissRequest = onCancel,
        title = { Text(title) },
        text = {
            Column {
                options.forEachIndexed { i, label ->
                    Row(Modifier.fillMaxWidth().selectable(selected = i == selected, onClick = { onPick(i) }).padding(vertical = 6.dp),
                        verticalAlignment = Alignment.CenterVertically) {
                        RadioButton(selected = i == selected, onClick = null)
                        Text(label, modifier = Modifier.padding(start = 8.dp))
                    }
                }
            }
        },
        confirmButton = { TextButton(onClick = onCancel) { Text("Cancel") } },
    )
}

/** Each GBA button (and ⏩ ⟲) with its keyboard key and controller button; a tap waits for the new one. */
@Composable
private fun KeyTable(keys: KeyBindings, keysChanged: Int, onPickKey: (Int, Int) -> Unit, onReset: () -> Unit) {
    val rows = remember(keysChanged) {
        KeyMap.bindings.map { b ->
            Triple(b, KeyBindings.keyName(keys.key(b.bit, KeyBindings.KEYBOARD)), KeyBindings.keyName(keys.key(b.bit, KeyBindings.CONTROLLER)))
        }
    }
    Row(Modifier.fillMaxWidth().padding(vertical = 8.dp)) {
        for (h in listOf("GBA", "Keyboard", "Controller")) Text(h, Modifier.weight(1f), fontWeight = FontWeight.Bold)
    }
    for ((b, kbd, pad) in rows) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Text(b.button, Modifier.weight(1f))
            TextButton(onClick = { onPickKey(b.bit, KeyBindings.KEYBOARD) }, Modifier.weight(1f)) { Text(kbd) }
            TextButton(onClick = { onPickKey(b.bit, KeyBindings.CONTROLLER) }, Modifier.weight(1f)) { Text(pad) }
        }
        HorizontalDivider()
    }
    Spacer(Modifier.height(12.dp))
    OutlinedButton(onClick = onReset, modifier = Modifier.fillMaxWidth()) { Text("Reset to defaults") }
    Note("Tap a key, then press the new one. A key already used moves here; its old button shows \"—\". The phone's touch buttons stay as they are.")
}

/**
 * Waiting for the key for one GBA button; Back cancels (Back itself can't be a game key). A layer on the same window,
 * not a dialog: a dialog's window would get the key instead of MainActivity.dispatchKeyEvent.
 */
@Composable
fun KeyWaitLayer(button: String, device: Int, onCancel: () -> Unit) {
    Box(Modifier.fillMaxSize().background(Color(0xB0000000)).pointerInput(Unit) { detectTapGestures { } },
        contentAlignment = Alignment.Center) {
        Surface(shape = RoundedCornerShape(16.dp), modifier = Modifier.padding(24.dp)) {
            Column(Modifier.padding(20.dp)) {
                Text("$button — ${if (device == KeyBindings.KEYBOARD) "keyboard" else "controller"}",
                    style = MaterialTheme.typography.titleMedium)
                Spacer(Modifier.height(12.dp))
                Text("Press the ${if (device == KeyBindings.KEYBOARD) "key" else "controller button"} for $button…")
                TextButton(onClick = onCancel, modifier = Modifier.align(Alignment.End)) { Text("Cancel") }
            }
        }
    }
}

/** A 16 KB file that isn't the usual GBA BIOS. */
@Composable
fun UnusualBiosDialog(onUse: () -> Unit, onCancel: () -> Unit) {
    AlertDialog(
        onDismissRequest = onCancel,
        title = { Text("Not the usual GBA BIOS") },
        text = { Text("This isn't the usual GBA BIOS; games may not start. Use it anyway?") },
        confirmButton = { TextButton(onClick = onUse) { Text("Use it") } },
        dismissButton = { TextButton(onClick = onCancel) { Text("Cancel") } },
    )
}

/** Game settings (hold a game → Game settings…): save type and clock, Auto showing what was detected. */
@Composable
fun GameSettingsDialog(name: String, game: GameSettings.Game, detected: Pair<Int, Boolean>?, onChange: (GameSettings.Game) -> Unit,
                       onDone: () -> Unit) {
    var choosing by remember { mutableStateOf<Choice?>(null) }
    val autoSave = "Auto" + (detected?.let { " (${GameSettings.saveName(it.first)})" } ?: "")
    val autoClock = "Auto" + (detected?.let { if (it.second) " (has one)" else " (none)" } ?: "")
    val saveOptions = listOf(autoSave) + GameSettings.SAVE_CHOICES.map(GameSettings::saveName)
    val saveIndex = if (game.saveType == GameSettings.AUTO) 0 else GameSettings.SAVE_CHOICES.indexOf(game.saveType) + 1
    val clockOptions = listOf(autoClock, "On", "Off")
    val clockIndex = when (game.clock) {
        GameSettings.CLOCK_ON -> 1
        GameSettings.CLOCK_OFF -> 2
        else -> 0
    }
    AlertDialog(
        onDismissRequest = onDone,
        title = { Text("Game settings — $name") },
        text = {
            Column {
                Setting("Save type", saveOptions[saveIndex]) {
                    choosing = Choice("Save type", saveOptions, saveIndex) {
                        onChange(game.copy(saveType = if (it == 0) GameSettings.AUTO else GameSettings.SAVE_CHOICES[it - 1]))
                    }
                }
                Setting("Clock", clockOptions[clockIndex]) {
                    choosing = Choice("Clock", clockOptions, clockIndex) {
                        onChange(game.copy(clock = when (it) {
                            1 -> GameSettings.CLOCK_ON
                            2 -> GameSettings.CLOCK_OFF
                            else -> GameSettings.AUTO
                        }))
                    }
                }
                Note("Used the next time this game opens. The save file is kept either way; it only loads if it fits the save type.")
            }
        },
        confirmButton = { TextButton(onClick = onDone) { Text("Done") } },
    )
    choosing?.let { c -> ChoiceDialog(c.title, c.options, c.selected, onPick = { c.onPick(it); choosing = null }, onCancel = { choosing = null }) }
}
```

Replace `app/src/main/java/com/tengen/gbaemu/Screens.kt`:

```kotlin
package com.tengen.gbaemu

import android.content.Context
import android.graphics.Bitmap
import android.view.SurfaceHolder
import android.view.SurfaceView
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.requiredSize
import androidx.compose.foundation.layout.systemBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Slider
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import java.text.DateFormat
import java.util.Calendar
import java.util.Date
import kotlin.math.roundToInt
import kotlinx.coroutines.delay

/** The very first start (no games folder yet) or a folder that can't be opened any more. */
@Composable
fun FirstStartScreen(folderLost: Boolean, message: String?, onChooseFolder: () -> Unit, onOpenGame: () -> Unit,
                     onSettings: () -> Unit) {
    Surface(modifier = Modifier.fillMaxSize()) { /* the theme's background and text colours */
        Column(
            modifier = Modifier.fillMaxSize().systemBarsPadding().padding(24.dp),
            verticalArrangement = Arrangement.Center,
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Text("GBA Emulator", style = MaterialTheme.typography.headlineMedium)
            Spacer(Modifier.height(16.dp))
            Text(
                if (folderLost) "Can't open the games folder any more — choose it again."
                else "Pick the folder where your games are. New games you put there show up by themselves.\n\n" +
                    "Android doesn't allow the Download folder itself: make a folder such as GBA (the picker's " +
                    "Create new folder) and put your games in it.",
                textAlign = TextAlign.Center,
                color = if (folderLost) MaterialTheme.colorScheme.error else Color.Unspecified,
            )
            Spacer(Modifier.height(24.dp))
            Button(onClick = onChooseFolder) { Text("Choose games folder") }
            Spacer(Modifier.height(12.dp))
            OutlinedButton(onClick = onOpenGame) { Text("Open a single game…") }
            Spacer(Modifier.height(12.dp))
            OutlinedButton(onClick = onSettings) { Text("Settings") }
            if (message != null) {
                Spacer(Modifier.height(16.dp))
                Text(message, color = MaterialTheme.colorScheme.error, textAlign = TextAlign.Center)
            }
        }
    }
}

/** When a game was last played, as the list shows it: "Today 14:00", "Yesterday 09:12", a date, or "Never played". */
fun playedText(lastPlayed: Long, now: Long = System.currentTimeMillis()): String {
    if (lastPlayed <= 0) return "Never played"
    val day = { t: Long -> Calendar.getInstance().apply { timeInMillis = t }.let { it.get(Calendar.YEAR) * 1000 + it.get(Calendar.DAY_OF_YEAR) } }
    val time = DateFormat.getTimeInstance(DateFormat.SHORT).format(Date(lastPlayed))
    val yesterday = Calendar.getInstance().apply { timeInMillis = now; add(Calendar.DAY_OF_YEAR, -1) }.timeInMillis
    return when (day(lastPlayed)) {
        day(now) -> "Today $time"
        day(yesterday) -> "Yesterday $time"
        else -> DateFormat.getDateInstance(DateFormat.MEDIUM).format(Date(lastPlayed))
    }
}

/**
 * The game list: one row per game (the picture of where it was left, its name, when it was last played). A tap
 * plays it, holding opens [onHold]'s menu. [refresh] changes whenever the pictures may have changed.
 */
@Composable
fun LibraryScreen(rows: List<GameEntry>, folderName: String?, scanning: Boolean, message: String?, refresh: Int,
                  picture: (GameEntry) -> Bitmap?, onPlay: (GameEntry) -> Unit, onHold: (GameEntry) -> Unit,
                  onOpenGame: () -> Unit, onChangeFolder: () -> Unit, onSettings: () -> Unit) {
    var menuOpen by remember { mutableStateOf(false) }
    Surface(modifier = Modifier.fillMaxSize()) {
        Column(Modifier.fillMaxSize().systemBarsPadding()) {
            Row(Modifier.fillMaxWidth().padding(start = 16.dp, end = 4.dp, top = 8.dp, bottom = 8.dp),
                verticalAlignment = Alignment.CenterVertically) {
                Text("Games", style = MaterialTheme.typography.titleLarge, modifier = Modifier.weight(1f))
                Box {
                    TextButton(onClick = { menuOpen = true }) { Text("⋮", fontSize = 22.sp) }
                    DropdownMenu(expanded = menuOpen, onDismissRequest = { menuOpen = false }) {
                        DropdownMenuItem(text = { Text("Open a game…") }, onClick = { menuOpen = false; onOpenGame() })
                        DropdownMenuItem(text = { Text("Change folder…") }, onClick = { menuOpen = false; onChangeFolder() })
                        DropdownMenuItem(text = { Text("Settings") }, onClick = { menuOpen = false; onSettings() })
                    }
                }
            }
            if (message != null) {
                Text(message, color = MaterialTheme.colorScheme.error, modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp))
            }
            if (rows.isEmpty()) {
                Column(Modifier.fillMaxSize().padding(24.dp), verticalArrangement = Arrangement.Center,
                    horizontalAlignment = Alignment.CenterHorizontally) {
                    if (scanning) Text("Looking for games…")
                    else {
                        Text("No games found in ${folderName ?: "the folder"}.", textAlign = TextAlign.Center)
                        Spacer(Modifier.height(16.dp))
                        Button(onClick = onChangeFolder) { Text("Change folder") }
                    }
                }
            } else {
                LazyColumn(Modifier.fillMaxSize()) {
                    items(rows, key = { it.key }) { game -> GameRow(game, refresh, picture, onPlay, onHold) }
                }
            }
        }
    }
}

@Composable
private fun GameRow(game: GameEntry, refresh: Int, picture: (GameEntry) -> Bitmap?, onPlay: (GameEntry) -> Unit,
                    onHold: (GameEntry) -> Unit) {
    val bitmap = remember(game.id, refresh) { picture(game) }
    Row(
        Modifier.fillMaxWidth()
            .pointerInput(game) { detectTapGestures(onTap = { onPlay(game) }, onLongPress = { onHold(game) }) }
            .padding(horizontal = 16.dp, vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        val frame = Modifier.width(96.dp).aspectRatio(1.5f).clip(RoundedCornerShape(4.dp))
        if (bitmap != null) {
            Image(bitmap.asImageBitmap(), contentDescription = null, modifier = frame,
                contentScale = ContentScale.FillBounds, filterQuality = FilterQuality.None)
        } else {
            Box(frame.background(MaterialTheme.colorScheme.surfaceVariant))
        }
        Spacer(Modifier.width(12.dp))
        Column(Modifier.weight(1f)) {
            Text(game.name, maxLines = 2, overflow = TextOverflow.Ellipsis)
            Text(playedText(game.lastPlayed) + if (game.outside) " · outside the folder" else "",
                fontSize = 12.sp, color = Color.Gray)
        }
    }
}

/** Holding a game in the list. [onRemove] only for games from outside the folder. */
@Composable
fun GameHoldMenu(game: GameEntry, onStartFresh: () -> Unit, onImport: () -> Unit, onExport: () -> Unit,
                 onGameSettings: () -> Unit, onRemove: (() -> Unit)?, onClose: () -> Unit) {
    AlertDialog(
        onDismissRequest = onClose,
        title = { Text(game.name, maxLines = 2, overflow = TextOverflow.Ellipsis) },
        text = {
            Column {
                val items = listOf("Start fresh (from the title screen)" to onStartFresh, "Import save…" to onImport,
                    "Export save…" to onExport, "Game settings…" to onGameSettings) +
                    listOfNotNull(onRemove?.let { "Remove from list" to it })
                for ((label, action) in items) {
                    TextButton(onClick = action, modifier = Modifier.fillMaxWidth()) {
                        Text(label, modifier = Modifier.fillMaxWidth())
                    }
                }
            }
        },
        confirmButton = { TextButton(onClick = onClose) { Text("Cancel") } },
    )
}

/** Import over a save that exists: the old one is kept as .sav.bak. */
@Composable
fun ReplaceSaveDialog(name: String, onReplace: () -> Unit, onCancel: () -> Unit) {
    AlertDialog(
        onDismissRequest = onCancel,
        title = { Text("Replace this game's save?") },
        text = { Text("$name already has a save. It will be kept as a backup, and the game will start from the title screen with the imported save.") },
        confirmButton = { TextButton(onClick = onReplace) { Text("Replace") } },
        dismissButton = { TextButton(onClick = onCancel) { Text("Cancel") } },
    )
}

/** A game with a save chip and no save yet: start fresh, or bring in a .sav file. Dismissing cancels the opening. */
@Composable
fun ImportDialog(error: String?, onStartFresh: () -> Unit, onImport: () -> Unit, onCancel: () -> Unit) {
    AlertDialog(
        onDismissRequest = onCancel,
        title = { Text("No save for this game yet") },
        text = {
            Column {
                Text("Start a fresh game, or import a .sav file (for example from a PC emulator)?")
                if (error != null) {
                    Spacer(Modifier.height(12.dp))
                    Text(error, color = MaterialTheme.colorScheme.error)
                }
            }
        },
        confirmButton = { TextButton(onClick = onImport) { Text("Import a .sav") } },
        dismissButton = { TextButton(onClick = onStartFresh) { Text("Start fresh") } },
    )
}

/** Which page of the in-game menu is open. */
enum class MenuPage { MAIN, SAVE, LOAD, CHEATS }

/** A dimmed layer over the paused game (it takes every touch) with a card in the middle. */
@Composable
private fun MenuLayer(content: @Composable () -> Unit) {
    Box(
        Modifier.fillMaxSize().background(Color(0xB0000000)).pointerInput(Unit) { detectTapGestures { } },
        contentAlignment = Alignment.Center,
    ) {
        Surface(shape = RoundedCornerShape(16.dp), modifier = Modifier.padding(16.dp).widthIn(max = 440.dp)) {
            content()
        }
    }
}

/** Back while playing: the game is paused behind this menu. */
@Composable
fun GameMenu(title: String, fastSpeed: String, onFastSpeed: () -> Unit, onResume: () -> Unit, onSave: () -> Unit,
             onLoad: () -> Unit, onCheats: () -> Unit, onSettings: () -> Unit, onRestart: () -> Unit, onQuit: () -> Unit) {
    MenuLayer {
        Column(Modifier.padding(16.dp).width(260.dp)) {
            Text("Paused — $title", style = MaterialTheme.typography.titleMedium)
            Spacer(Modifier.height(12.dp))
            for ((label, action) in listOf("▶  Resume" to onResume, "Fast-forward: $fastSpeed" to onFastSpeed,
                "Save state…" to onSave, "Load state…" to onLoad, "Cheats…" to onCheats, "Settings…" to onSettings,
                "Restart game" to onRestart, "Quit" to onQuit)) {
                FilledTonalButton(onClick = action, modifier = Modifier.fillMaxWidth().padding(vertical = 3.dp)) {
                    Text(label)
                }
            }
        }
    }
}

/**
 * The 10 slots as a grid of pictures (two columns). Saving: every slot can be picked; loading: only used ones.
 * [refresh] changes whenever the slots may have changed, so the pictures are read again.
 */
@Composable
fun SlotGrid(saving: Boolean, states: States, refresh: Int, onPick: (Int) -> Unit, onBack: () -> Unit) {
    val pictures = remember(refresh) { (1..States.SLOTS).map { if (states.used(it)) states.loadPicture(it) else null } }
    val used = remember(refresh) { (1..States.SLOTS).map { states.used(it) } }
    val dates = remember(refresh) {
        val format = DateFormat.getDateTimeInstance(DateFormat.SHORT, DateFormat.SHORT)
        (1..States.SLOTS).map { if (states.used(it)) format.format(Date(states.savedAt(it))) else "empty" }
    }
    MenuLayer {
        Column(Modifier.padding(12.dp).verticalScroll(rememberScrollState())) {
            Text(if (saving) "Save state" else "Load state", style = MaterialTheme.typography.titleMedium,
                modifier = Modifier.padding(4.dp))
            for (row in 0 until States.SLOTS / 2) {
                Row {
                    for (col in 0..1) {
                        val slot = row * 2 + col + 1
                        val enabled = saving || used[slot - 1]
                        Column(
                            Modifier.weight(1f).padding(4.dp).clip(RoundedCornerShape(8.dp))
                                .background(MaterialTheme.colorScheme.surfaceVariant)
                                .clickable(enabled = enabled) { onPick(slot) }.padding(6.dp),
                        ) {
                            val picture = pictures[slot - 1]
                            val frame = Modifier.fillMaxWidth().aspectRatio(1.5f).clip(RoundedCornerShape(4.dp))
                            if (picture != null) {
                                Image(picture.asImageBitmap(), contentDescription = "slot $slot", modifier = frame,
                                    contentScale = ContentScale.FillBounds, filterQuality = FilterQuality.None)
                            } else {
                                Box(frame.border(1.dp, Color.Gray, RoundedCornerShape(4.dp)))
                            }
                            Text("$slot · ${dates[slot - 1]}", fontSize = 12.sp,
                                color = if (enabled) Color.Unspecified else Color.Gray,
                                modifier = Modifier.padding(top = 4.dp))
                        }
                    }
                }
            }
            TextButton(onClick = onBack, modifier = Modifier.align(Alignment.End)) { Text("Back") }
        }
    }
}

/** The type picker's names, by NativeBridge.CHEAT_* value. */
val CHEAT_TYPES = listOf("Auto", "Action Replay v3", "GameShark v1/v2", "CodeBreaker", "address:value")

/** The game's cheats: one row each (name, on/off switch); a tap on the name edits it. */
@Composable
fun CheatPanel(title: String, cheats: List<Cheat>, onToggle: (Int, Boolean) -> Unit, onEdit: (Int) -> Unit,
               onAdd: () -> Unit, onBack: () -> Unit) {
    MenuLayer {
        Column(Modifier.padding(16.dp).width(300.dp)) {
            Text("Cheats — $title", style = MaterialTheme.typography.titleMedium)
            Spacer(Modifier.height(8.dp))
            Column(Modifier.heightIn(max = 360.dp).verticalScroll(rememberScrollState())) {
                if (cheats.isEmpty()) Text("No cheats yet.", Modifier.padding(vertical = 8.dp))
                cheats.forEachIndexed { i, c ->
                    Row(
                        Modifier.fillMaxWidth().padding(vertical = 3.dp).clip(RoundedCornerShape(8.dp))
                            .background(MaterialTheme.colorScheme.surfaceVariant),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Text(c.name, Modifier.weight(1f).clickable { onEdit(i) }.padding(12.dp))
                        Switch(checked = c.on, onCheckedChange = { onToggle(i, it) }, modifier = Modifier.padding(end = 8.dp))
                    }
                }
            }
            Spacer(Modifier.height(8.dp))
            Button(onClick = onAdd, modifier = Modifier.fillMaxWidth()) { Text("+ Add cheat") }
            TextButton(onClick = onBack, modifier = Modifier.align(Alignment.End)) { Text("Back") }
        }
    }
}

/**
 * Adding (editing == null) or editing a cheat: name, codes (one per line), type. [check] reads the codes:
 * [the type found, the first bad line (0 = none)]. Save refuses unreadable codes and names the line.
 */
@Composable
fun CheatDialog(editing: Cheat?, check: (String, Int) -> IntArray, onSave: (Cheat) -> Unit, onDelete: (() -> Unit)?,
                onCancel: () -> Unit) {
    var name by remember { mutableStateOf(editing?.name ?: "") }
    var codes by remember { mutableStateOf(editing?.codes ?: "") }
    var type by remember { mutableStateOf(editing?.type ?: NativeBridge.CHEAT_AUTO) }
    var picking by remember { mutableStateOf(false) }
    var error by remember { mutableStateOf<String?>(null) }
    val found = remember(codes, type) { if (codes.isBlank()) null else check(codes, type) }
    AlertDialog(
        onDismissRequest = onCancel,
        title = { Text(if (editing == null) "Add cheat" else "Edit cheat") },
        text = {
            Column(Modifier.verticalScroll(rememberScrollState())) {
                OutlinedTextField(value = name, onValueChange = { name = it }, label = { Text("Name") }, singleLine = true,
                    modifier = Modifier.fillMaxWidth())
                Spacer(Modifier.height(8.dp))
                OutlinedTextField(value = codes, onValueChange = { codes = it; error = null },
                    label = { Text("Codes (one per line)") }, minLines = 4, maxLines = 10,
                    textStyle = MaterialTheme.typography.bodyMedium.copy(fontFamily = FontFamily.Monospace),
                    modifier = Modifier.fillMaxWidth())
                Spacer(Modifier.height(8.dp))
                Box {
                    OutlinedButton(onClick = { picking = true }) { Text("Type: ${CHEAT_TYPES[type]} ▾") }
                    DropdownMenu(expanded = picking, onDismissRequest = { picking = false }) {
                        CHEAT_TYPES.forEachIndexed { i, t ->
                            DropdownMenuItem(text = { Text(t) }, onClick = { type = i; picking = false; error = null })
                        }
                    }
                }
                val hint = when {
                    error != null -> error
                    found == null -> null
                    found[1] != 0 -> "Line ${found[1]} isn't a code this app can read."
                    type == NativeBridge.CHEAT_AUTO -> "Found: ${CHEAT_TYPES[found[0]]}"
                    else -> null
                }
                if (hint != null) {
                    Text(hint, color = if (found != null && found[1] == 0 && error == null) Color.Unspecified
                        else MaterialTheme.colorScheme.error, modifier = Modifier.padding(top = 6.dp))
                }
            }
        },
        confirmButton = {
            TextButton(onClick = {
                val result = check(codes, type)
                if (codes.isBlank()) error = "Type or paste the codes first."
                else if (result[1] != 0) error = "Line ${result[1]} isn't a code this app can read."
                else {
                    val shown = name.trim().ifEmpty { codes.lineSequence().first { it.isNotBlank() }.trim() }
                    onSave(Cheat(shown, type, codes, editing?.on ?: true))
                }
            }) { Text("Save") }
        },
        dismissButton = {
            Row {
                if (onDelete != null) TextButton(onClick = onDelete) { Text("Delete") }
                TextButton(onClick = onCancel) { Text("Cancel") }
            }
        },
    )
}

@Composable
fun DeleteCheatDialog(name: String, onDelete: () -> Unit, onCancel: () -> Unit) {
    AlertDialog(
        onDismissRequest = onCancel,
        title = { Text("Delete $name?") },
        text = { Text("The cheat and its codes are removed from this game.") },
        confirmButton = { TextButton(onClick = onDelete) { Text("Delete") } },
        dismissButton = { TextButton(onClick = onCancel) { Text("Cancel") } },
    )
}

@Composable
fun ReplaceDialog(slot: Int, onReplace: () -> Unit, onCancel: () -> Unit) {
    AlertDialog(
        onDismissRequest = onCancel,
        title = { Text("Replace slot $slot?") },
        text = { Text("The state saved there is overwritten.") },
        confirmButton = { TextButton(onClick = onReplace) { Text("Replace") } },
        dismissButton = { TextButton(onClick = onCancel) { Text("Cancel") } },
    )
}

@Composable
fun RestartDialog(onRestart: () -> Unit, onCancel: () -> Unit) {
    AlertDialog(
        onDismissRequest = onCancel,
        title = { Text("Restart from the title screen?") },
        text = { Text("The current moment is lost unless it's saved in a slot.") },
        confirmButton = { TextButton(onClick = onRestart) { Text("Restart") } },
        dismissButton = { TextButton(onClick = onCancel) { Text("Cancel") } },
    )
}

/**
 * The game (a SurfaceView handed to the native render thread) and the touch buttons, in layout A; for a solar
 * cartridge also the ☀ button with the light slider ([manualLight] null = Auto, the phone's light sensor).
 */
@Composable
fun GameScreen(controlsHidden: Boolean, fastOn: Boolean, onTouchMask: (Int) -> Unit, onShowControls: () -> Unit,
               onDrawFailed: () -> Unit, solar: Boolean = false, manualLight: Int? = null,
               onManualLight: (Int?) -> Unit = {}, touchLook: Int = Settings.LOOK_NORMAL, buzz: Boolean = true,
               layouts: LayoutStore.Layouts = LayoutStore.Layouts(), showSpeed: Boolean = false) {
    BoxWithConstraints(Modifier.fillMaxSize().background(Color.Black)) {
        val sideways = maxWidth > maxHeight
        val edits = layouts.of(sideways)
        // The game's surface in the layout's picture box (upright by default: full width at the top; sideways: the whole
        // screen, the game fitted inside); the buttons over the whole screen.
        val density = LocalDensity.current
        val pic = TouchLayout(constraints.maxWidth.toFloat(), constraints.maxHeight.toFloat(), density.density, sideways, edits).picture
        AndroidView(
            modifier = Modifier.offset { IntOffset(pic.left.roundToInt(), pic.top.roundToInt()) }
                .requiredSize(with(density) { (pic.right - pic.left).toDp() }, with(density) { (pic.bottom - pic.top).toDp() }),
            factory = { gameSurface(it, onDrawFailed) },
        )
        AndroidView(
            modifier = Modifier.fillMaxSize(),
            factory = { TouchControlsView(it).also { onTouchMask(0) } }, // a new view (e.g. after turning): nothing held
            update = { v ->
                v.sideways = sideways
                v.edits = edits
                v.hidden = controlsHidden
                v.fastOn = fastOn
                v.look = touchLook
                v.buzz = buzz
                v.onMask = onTouchMask
                v.onShow = onShowControls
            },
        )
        if (solar) SunControl(manualLight, onManualLight, Modifier.align(Alignment.BottomStart))
        if (showSpeed) SpeedLabel(Modifier.align(Alignment.TopCenter))
    }
}

/** Show speed: "60 fps · 1.0×", measured from the frames the emulator ran, changing once a second. */
@Composable
private fun SpeedLabel(modifier: Modifier) {
    var label by remember { mutableStateOf<String?>(null) }
    LaunchedEffect(Unit) {
        val meter = SpeedMeter()
        while (true) {
            label = meter.sample(System.nanoTime(), NativeBridge.debugFrameCount())
            delay(250)
        }
    }
    label?.let {
        Text(it, color = Color.White, fontSize = 12.sp,
            modifier = modifier.padding(top = 4.dp).background(Color(0x99000000), RoundedCornerShape(6.dp))
                .padding(horizontal = 6.dp, vertical = 2.dp))
    }
}

/** ☀: opens a light slider (moving it sets the level by hand) with an Auto switch (the phone's light sensor). */
@Composable
private fun SunControl(level: Int?, onLevel: (Int?) -> Unit, modifier: Modifier) {
    var open by remember { mutableStateOf(false) }
    Column(modifier.padding(8.dp)) {
        if (open) {
            Surface(shape = RoundedCornerShape(12.dp), color = Color(0xE0202020)) {
                Column(Modifier.padding(12.dp).width(260.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text("Light: ${level ?: "sensor"}", color = Color.White, modifier = Modifier.weight(1f))
                        Text("Auto ", color = Color.White)
                        Switch(checked = level == null, onCheckedChange = { onLevel(if (it) null else 128) })
                    }
                    Slider(value = (level ?: 128).toFloat(), onValueChange = { onLevel(it.roundToInt()) },
                        valueRange = 0f..255f)
                }
            }
            Spacer(Modifier.height(4.dp))
        }
        TextButton(onClick = { open = !open }) { Text("☀", fontSize = 28.sp, color = Color(0xFFFFD54F)) }
    }
}

private fun gameSurface(context: Context, onDrawFailed: () -> Unit) = SurfaceView(context).apply {
    holder.addCallback(object : SurfaceHolder.Callback {
        override fun surfaceCreated(holder: SurfaceHolder) {
            if (!NativeBridge.setSurface(holder.surface)) onDrawFailed()
        }

        override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
            if (!NativeBridge.setSurface(holder.surface)) onDrawFailed()
        }

        override fun surfaceDestroyed(holder: SurfaceHolder) {
            NativeBridge.setSurface(null)
        }
    })
}
```

Replace `app/src/main/java/com/tengen/gbaemu/MainActivity.kt`:

```kotlin
package com.tengen.gbaemu

import android.content.Intent
import android.content.pm.ActivityInfo
import android.content.res.Configuration
import android.graphics.Bitmap
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.provider.OpenableColumns
import android.view.KeyEvent
import android.view.WindowManager
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import java.io.File
import java.nio.ByteBuffer
import kotlin.concurrent.thread
import kotlinx.coroutines.delay

class MainActivity : ComponentActivity() {
    private var playing by mutableStateOf(false)
    private var message by mutableStateOf<String?>(null)
    private lateinit var library: Library
    private var rows by mutableStateOf(listOf<GameEntry>()) // the list as shown
    private var hasFolder by mutableStateOf(false) // library.folder is set (the screen watches this, not the Library)
    private var folderName by mutableStateOf<String?>(null)
    private var folderLost by mutableStateOf(false)
    private var scanning by mutableStateOf(false)
    private var scanRun = 0 // only the newest scan's result is used
    private var libraryRefresh by mutableStateOf(0) // re-reads the list's pictures
    private var holdGame by mutableStateOf<GameEntry?>(null)
    private var replaceSave by mutableStateOf<Pair<GameEntry, ByteArray>?>(null)
    private var saveGame: GameEntry? = null // the game whose save is being exported / imported
    private var exportBytes: ByteArray? = null
    private lateinit var settingsStore: IntStore
    private var settings by mutableStateOf(Settings())
    private var settingsPage by mutableStateOf<SettingsPage?>(null) // the Settings screen, or null
    private lateinit var keys: KeyBindings
    private var keysChanged by mutableStateOf(0) // re-reads the key table
    private var waitingKey by mutableStateOf<Pair<Int, Int>?>(null) // (button bit, KeyBindings device) being remapped
    private var unusualBios by mutableStateOf<ByteArray?>(null) // a 16 KB file that isn't the usual BIOS, to confirm
    private lateinit var gameSettings: GameSettings
    private lateinit var layoutStore: LayoutStore
    private var layouts by mutableStateOf(LayoutStore.Layouts()) // the user's touch layouts (6c)
    private var editLayout by mutableStateOf<Boolean?>(null) // the layout editor: sideways, upright, or closed
    private var editPicture: Bitmap? = null // the paused game's picture behind the editor
    private var orientationBeforeEditor = ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
    private var editGame by mutableStateOf<Triple<String, String, GameSettings.Game>?>(null) // name, id, its settings
    private var askImport by mutableStateOf(false)
    private var importError by mutableStateOf<String?>(null)
    private var menu by mutableStateOf<MenuPage?>(null) // the in-game menu (the game paused) or null
    private var replaceSlot by mutableStateOf<Int?>(null)
    private var askRestart by mutableStateOf(false)
    private var slotsChanged by mutableStateOf(0) // re-reads the slot pictures
    private var session by mutableStateOf(0) // a new game session gets a new game screen (see startGame)
    private var states: States? = null // the running game's save states
    private var cheatStore: Cheats? = null // the running game's cheats file
    private var cheats by mutableStateOf(listOf<Cheat>())
    private var editCheat by mutableStateOf<Int?>(null) // -1 = a new one
    private var deleteCheat by mutableStateOf<Int?>(null)
    private var current: GameEntry? = null // the running game's row
    private var currentTitle = ""
    private var controlsHidden by mutableStateOf(false)
    private var manualLight by mutableStateOf<Int?>(null)
    private var hardware = 0 // the cartridge's extras (NativeBridge.HW_*)
    private var motion: MotionInput? = null
    private var rumble: Rumble? = null
    private var keyboardMask = 0
    private var touchMask = 0
    private val fastSwitch = FastForwardSwitch()
    private val repeatFilter = KeyRepeatFilter() // F / R held in the phone emulator's window
    private val handler = Handler(Looper.getMainLooper())
    private var fastOn by mutableStateOf(false) // ⏩ lit
    private var fastSpeed by mutableStateOf(4) // 2, 4, 8 or NativeBridge.FF_MAX; remembered for all games

    private val openRom = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) openFromPicker(uri)
    }
    private val pickSave = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) importFrom(uri)
    }
    private val pickFolder = registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
        if (uri != null) useFolder(uri)
    }
    private val exportTo = registerForActivityResult(ActivityResultContracts.CreateDocument("application/octet-stream")) { uri ->
        if (uri != null) writeExport(uri)
    }
    private val pickBios = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) biosPicked(uri)
    }
    private val importFor = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) importPicked(uri)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        library = Library(filesDir)
        if (!library.load()) toast("The game list couldn't be read; it was kept aside and is built again from the folder.")
        carryOverLastGame()
        rows = library.rows()
        hasFolder = library.folder != null
        settingsStore = PrefsStore(getSharedPreferences("settings", MODE_PRIVATE))
        settings = Settings.load(settingsStore)
        fastSpeed = settings.fastSpeed
        applySettings(settings)
        keys = KeyBindings(PrefsStore(getSharedPreferences("keys", MODE_PRIVATE)))
        keys.use()
        gameSettings = GameSettings(PrefsStore(getSharedPreferences("game_settings", MODE_PRIVATE)))
        layoutStore = LayoutStore(filesDir)
        layouts = layoutStore.load()
        if (layoutStore.wasDamaged) toast("The touch layout couldn't be read; it was kept aside and the default layout is used.")
        setContent {
            MaterialTheme(colorScheme = darkColorScheme()) {
                if (playing) key(session) {
                    GameScreen(
                        controlsHidden = controlsHidden,
                        fastOn = fastOn,
                        onTouchMask = { touchMask = it; sendKeys() },
                        onShowControls = { controlsHidden = false },
                        onDrawFailed = {
                            stopGame()
                            message = "This device cannot show the game: OpenGL ES 3.0 failed."
                        },
                        solar = hardware and NativeBridge.HW_SOLAR != 0,
                        manualLight = manualLight,
                        onManualLight = {
                            manualLight = it
                            motion?.manualLight = it
                        },
                        touchLook = settings.touchLook,
                        buzz = settings.buzz,
                        layouts = layouts,
                        showSpeed = settings.showSpeed,
                    )
                    LaunchedEffect(Unit) { // a failed save-file write: tell once per game
                        while (true) {
                            delay(1000)
                            if (NativeBridge.saveError()) {
                                toast("Could not write the save file.")
                                break
                            }
                        }
                    }
                } else if (folderLost || !hasFolder) { // no folder yet, or it can't be opened
                    FirstStartScreen(folderLost, message, onChooseFolder = { pickFolder.launch(null) },
                        onOpenGame = { openRom.launch(arrayOf("*/*")) }, onSettings = { settingsPage = SettingsPage.MAIN })
                } else {
                    LibraryScreen(rows, folderName, scanning, message, libraryRefresh,
                        picture = { g -> if (g.id.isEmpty()) null else States(filesDir, g.id).loadPicture(States.AUTO) },
                        onPlay = { open(it) }, onHold = { holdGame = it },
                        onOpenGame = { openRom.launch(arrayOf("*/*")) }, onChangeFolder = { pickFolder.launch(null) },
                        onSettings = { settingsPage = SettingsPage.MAIN })
                }
                holdGame?.let { g ->
                    GameHoldMenu(g,
                        onStartFresh = { holdGame = null; open(g, resumeAuto = false) },
                        onImport = { holdGame = null; saveGame = g; importFor.launch(arrayOf("*/*")) },
                        onExport = { holdGame = null; exportSave(g) },
                        onGameSettings = {
                            holdGame = null
                            idOf(g)?.let { id -> editGame = Triple(g.name, id, gameSettings.of(id)) }
                        },
                        onRemove = if (g.outside) ({ holdGame = null; removeGame(g) }) else null,
                        onClose = { holdGame = null })
                }
                replaceSave?.let { (g, bytes) ->
                    ReplaceSaveDialog(g.name, onReplace = { replaceSave = null; importSave(g, bytes) },
                        onCancel = { replaceSave = null })
                }
                if (askImport) {
                    ImportDialog(importError,
                        onStartFresh = { askImport = false; startGame() },
                        onImport = { pickSave.launch(arrayOf("*/*")) },
                        onCancel = { askImport = false; NativeBridge.stop() })
                }
                val st = states
                when (menu) {
                    MenuPage.MAIN -> GameMenu(currentTitle, speedLabel(fastSpeed), onFastSpeed = ::nextFastSpeed,
                        onResume = ::resumeGame, onSave = { menu = MenuPage.SAVE },
                        onLoad = { menu = MenuPage.LOAD }, onCheats = { menu = MenuPage.CHEATS },
                        onSettings = { settingsPage = SettingsPage.MAIN },
                        onRestart = { askRestart = true }, onQuit = ::quitGame)
                    MenuPage.SAVE -> if (st != null) SlotGrid(saving = true, st, slotsChanged,
                        onPick = { if (st.used(it)) replaceSlot = it else saveTo(it) }, onBack = { menu = MenuPage.MAIN })
                    MenuPage.LOAD -> if (st != null) SlotGrid(saving = false, st, slotsChanged,
                        onPick = ::loadFrom, onBack = { menu = MenuPage.MAIN })
                    MenuPage.CHEATS -> CheatPanel(currentTitle, cheats,
                        onToggle = { i, on -> changeCheats(cheats.toMutableList().also { it[i] = it[i].copy(on = on) }) },
                        onEdit = { editCheat = it }, onAdd = { editCheat = -1 }, onBack = { menu = MenuPage.MAIN })
                    null -> {}
                }
                editCheat?.let { i ->
                    CheatDialog(editing = cheats.getOrNull(i), check = NativeBridge::checkCheat,
                        onSave = { c ->
                            changeCheats(cheats.toMutableList().also { if (i in it.indices) it[i] = c else it += c })
                            editCheat = null
                        },
                        onDelete = if (i in cheats.indices) ({ deleteCheat = i }) else null,
                        onCancel = { editCheat = null })
                }
                deleteCheat?.let { i ->
                    DeleteCheatDialog(cheats[i].name,
                        onDelete = {
                            changeCheats(cheats.toMutableList().also { it.removeAt(i) })
                            deleteCheat = null
                            editCheat = null
                        },
                        onCancel = { deleteCheat = null })
                }
                replaceSlot?.let { slot -> ReplaceDialog(slot, onReplace = { saveTo(slot) }, onCancel = { replaceSlot = null }) }
                if (askRestart) RestartDialog(onRestart = ::restartGame, onCancel = { askRestart = false })
                settingsPage?.let { page ->
                    SettingsScreen(page, settings, keys, keysChanged, if (settings.biosFile) "BIOS file" else "Built-in",
                        onPage = { settingsPage = it },
                        onBack = { settingsPage = if (page == SettingsPage.MAIN) null else SettingsPage.MAIN },
                        onChange = ::changeSettings,
                        onPickKey = { bit, device -> waitingKey = bit to device },
                        onResetKeys = { keys.reset(); keys.use(); keysChanged++ },
                        onChooseBios = { pickBios.launch(arrayOf("*/*")) },
                        onBuiltInBios = {
                            Bios.remove(filesDir)
                            changeSettings(settings.copy(biosFile = false))
                            toast("The built-in BIOS is used from the next game on.")
                        },
                        onEditLayout = ::openLayoutEditor)
                }
                editLayout?.let { side ->
                    LayoutEditor(side, layouts.of(side), editPicture, settings.touchLook,
                        onSave = { edits ->
                            layouts = layouts.with(side, edits)
                            if (!layoutStore.save(layouts)) toast("Couldn't save the layout.")
                            closeLayoutEditor()
                        },
                        onCancel = ::closeLayoutEditor)
                }
                waitingKey?.let { (bit, device) ->
                    KeyWaitLayer(KeyMap.bindings.first { it.bit == bit }.button, device, onCancel = { waitingKey = null })
                }
                unusualBios?.let { bytes ->
                    UnusualBiosDialog(onUse = { unusualBios = null; installBios(bytes) }, onCancel = { unusualBios = null })
                }
                editGame?.let { (name, id, game) ->
                    GameSettingsDialog(name, game, gameSettings.detected(id),
                        onChange = { gameSettings.set(id, it); editGame = Triple(name, id, it) },
                        onDone = { editGame = null })
                }
                BackHandler(enabled = playing && menu == null && settingsPage == null) { openMenu() }
                BackHandler(enabled = playing && menu != null && settingsPage == null) {
                    if (menu == MenuPage.MAIN) resumeGame() else menu = MenuPage.MAIN
                }
                BackHandler(enabled = settingsPage != null && waitingKey == null && editLayout == null) {
                    settingsPage = if (settingsPage == SettingsPage.MAIN) null else SettingsPage.MAIN
                }
            }
        }
    }

    private fun readBytes(uri: Uri): ByteArray? = try {
        contentResolver.openInputStream(uri)?.use { it.readBytes() }
    } catch (e: Exception) {
        null
    }

    /** The file's name as the picker showed it and its size (0 = unknown), or null. */
    private fun fileInfo(uri: Uri): Pair<String, Long>? = try {
        contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE), null, null, null)?.use {
            if (it.moveToFirst() && !it.isNull(0)) it.getString(0) to (if (it.isNull(1)) 0L else it.getLong(1)) else null
        }
    } catch (e: Exception) {
        null
    }

    /** "Open a game…": the game's row if the list has it (e.g. it is in the folder), else a new outside one. */
    private fun openFromPicker(uri: Uri) {
        try { /* so the list can read it again after a restart */
            contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
        } catch (e: SecurityException) {
        }
        val key = try {
            FolderScan.keyOf(uri)
        } catch (e: Exception) {
            uri.toString()
        }
        val info = fileInfo(uri)
        val entry = library.games.firstOrNull { it.key == key }
            ?: GameEntry(key, uri.toString(), Library.nameOf(info?.first ?: uri.lastPathSegment ?: "game"), outside = true,
                size = info?.second ?: 0)
        open(entry)
    }

    /** The games folder picked: its permission kept (the old folder's given back), the list read from it. */
    private fun useFolder(tree: Uri) {
        try {
            contentResolver.takePersistableUriPermission(tree, Intent.FLAG_GRANT_READ_URI_PERMISSION)
        } catch (e: SecurityException) {
        }
        library.folder?.let { old ->
            if (old != tree.toString()) try {
                contentResolver.releasePersistableUriPermission(Uri.parse(old), Intent.FLAG_GRANT_READ_URI_PERMISSION)
            } catch (e: Exception) {
            }
        }
        library.folder = tree.toString()
        library.save()
        hasFolder = true
        folderLost = false
        message = null
        refreshLibrary()
    }

    /**
     * Reads the folder again (off the main thread) and shows the list. Played games whose id is not known yet (the
     * last game carried over from before the list) are read once to learn it, so their picture shows. Outside games
     * of unknown size get it, so [Library.merge] can match them with the folder's copy by name and size.
     */
    private fun refreshLibrary() {
        rows = library.rows()
        val folder = library.folder?.let(Uri::parse)
        val unknown = library.games.filter { it.lastPlayed > 0 && it.id.isEmpty() }
        val unsized = library.games.filter { it.outside && it.size <= 0 }
        if (folder == null && unknown.isEmpty() && unsized.isEmpty()) return
        val run = ++scanRun
        scanning = true
        thread(name = "library-scan") {
            val found = folder?.let { FolderScan.scan(contentResolver, it) }
            val name = folder?.let { FolderScan.folderName(contentResolver, it) }
            val ids = unknown.mapNotNull { g -> readGame(Uri.parse(g.uri))?.let { g.key to GameId.of(it) } }.toMap()
            val sizes = unsized.mapNotNull { g -> fileInfo(Uri.parse(g.uri))?.second?.takeIf { it > 0 }?.let { g.key to it } }.toMap()
            runOnUiThread {
                if (run != scanRun) return@runOnUiThread
                scanning = false
                for (g in library.games) ids[g.key]?.let { library.played(g.copy(id = it)) }
                for (g in library.games) sizes[g.key]?.let { library.played(g.copy(size = it)) }
                if (folder != null) {
                    folderLost = found == null
                    if (found != null) library.merge(found)
                }
                folderName = name
                library.save()
                rows = library.rows()
                libraryRefresh++
            }
        }
    }

    /** Before the list (5c and earlier): the last game opened becomes the list's first game. */
    private fun carryOverLastGame() {
        val old = getSharedPreferences("last_game", MODE_PRIVATE)
        val u = old.getString("uri", null) ?: return
        val uri = Uri.parse(u)
        val key = try {
            FolderScan.keyOf(uri)
        } catch (e: Exception) {
            u
        }
        if (library.games.none { it.key == key }) {
            val info = fileInfo(uri)
            val name = info?.first?.let(Library::nameOf) ?: old.getString("title", null) ?: "last game"
            library.played(GameEntry(key, u, name, lastPlayed = System.currentTimeMillis(), outside = true, size = info?.second ?: 0))
            library.save()
        }
        old.edit().clear().apply()
    }

    /** The game's bytes (from a .gba or a .zip), or null. */
    private fun readGame(uri: Uri): ByteArray? = try {
        contentResolver.openInputStream(uri)?.use(RomFile::read)
    } catch (e: Exception) {
        null
    }

    /** The game's id, reading the game if the list doesn't know it yet (then remembered); null if it can't be read. */
    private fun idOf(g: GameEntry): String? {
        if (g.id.isNotEmpty()) return g.id
        val bytes = readGame(Uri.parse(g.uri)) ?: run { toast("Can't open ${g.name}."); return null }
        val id = GameId.of(bytes)
        library.played(g.copy(id = id))
        library.save()
        rows = library.rows()
        return id
    }

    private fun removeGame(g: GameEntry) {
        library.remove(g.key)
        library.save()
        try {
            contentResolver.releasePersistableUriPermission(Uri.parse(g.uri), Intent.FLAG_GRANT_READ_URI_PERMISSION)
        } catch (e: Exception) {
        }
        rows = library.rows()
    }

    private fun exportSave(g: GameEntry) {
        val id = idOf(g) ?: return
        val bytes = SaveFiles.export(filesDir, id) ?: run { toast("No save for this game yet."); return }
        exportBytes = bytes
        exportTo.launch("${g.name}.sav")
    }

    private fun writeExport(uri: Uri) {
        val bytes = exportBytes ?: return
        exportBytes = null
        try {
            contentResolver.openOutputStream(uri, "wt")?.use { it.write(bytes) } ?: error("no stream")
            toast("Save exported.")
        } catch (e: Exception) {
            toast("Couldn't write the save there.")
        }
    }

    private fun importPicked(uri: Uri) {
        val g = saveGame ?: return
        saveGame = null
        val bytes = readBytes(uri) ?: run { toast("Could not read that file."); return }
        if (!SaveFiles.validSize(bytes.size)) {
            toast("That file isn't a save for this game.")
            return
        }
        val id = idOf(g) ?: return
        if (SaveFiles.file(filesDir, id).isFile) replaceSave = g to bytes else importSave(g, bytes)
    }

    private fun importSave(g: GameEntry, bytes: ByteArray) {
        val id = idOf(g) ?: return
        when (SaveFiles.import(filesDir, id, bytes)) {
            SaveFiles.Import.OK -> toast("Save imported. ${g.name} will start from the title screen.")
            SaveFiles.Import.WRONG_SIZE -> toast("That file isn't a save for this game.")
            SaveFiles.Import.FAILED -> toast("Couldn't write the save.")
        }
        libraryRefresh++ // the picture of where it was left is gone
    }

    /** Opens a game; with its automatic state it resumes the exact moment ([resumeAuto] false: from the title). */
    private fun open(entry: GameEntry, resumeAuto: Boolean = true) {
        val uri = Uri.parse(entry.uri)
        val bytes = try {
            contentResolver.openInputStream(uri)?.use(RomFile::read)
        } catch (e: RomFile.NotAGame) {
            message = "Not a GBA game."
            return
        } catch (e: Exception) {
            null
        }
        if (bytes == null) {
            message = "Can't open ${entry.name}."
            return
        }
        val rom = ByteBuffer.allocateDirect(bytes.size).put(bytes)
        rom.flip()
        val saves = File(filesDir, "saves").apply { mkdirs() }
        val gameId = GameId.of(bytes)
        val bios = if (settings.biosFile) {
            Bios.load(filesDir) ?: null.also { toast("The BIOS file is missing; the built-in BIOS is used.") }
        } else null
        NativeBridge.setBios(bios)
        val own = gameSettings.of(gameId)
        NativeBridge.setGameOverrides(own.saveType, own.clock)
        if (NativeBridge.loadRom(rom, File(saves, "$gameId.sav").path) != 0) {
            message = "Not a GBA game."
            return
        }
        if (own == GameSettings.Game()) NativeBridge.cartSetup().let { gameSettings.remember(gameId, it[0], it[1]) }
        hardware = NativeBridge.cartHardware()
        manualLight = null
        val played = entry.copy(id = gameId, lastPlayed = System.currentTimeMillis())
        library.played(played)
        library.save()
        rows = library.rows()
        message = null
        val st = States(filesDir, gameId)
        states = st
        cheatStore = Cheats(filesDir, gameId).also { store ->
            cheats = store.load() ?: emptyList<Cheat>().also {
                toast("This game's cheats file couldn't be read; it was kept aside and the game starts without cheats.")
            }
        }
        current = played
        currentTitle = played.name
        if (resumeAuto && st.used(States.AUTO)) {
            if (st.load(States.AUTO)) {
                startGame()
                return
            }
            toast("Couldn't resume where you left off; starting the game.")
        }
        when (NativeBridge.saveStatus()) {
            NativeBridge.SAVE_EMPTY -> {
                importError = null
                askImport = true
            }
            NativeBridge.SAVE_MISFIT -> {
                startGame()
                toast("The save file for this game doesn't fit it and was not loaded.")
            }
            else -> startGame()
        }
    }

    private fun importFrom(uri: Uri) {
        val bytes = readBytes(uri)
        when {
            bytes == null -> importError = "Could not read that file."
            NativeBridge.importSave(bytes) != 0 -> importError = "This .sav doesn't fit this game."
            else -> {
                askImport = false
                startGame()
            }
        }
    }

    private fun startGame() {
        keyboardMask = 0
        touchMask = 0
        fastSwitch.reset()
        fastOn = false
        repeatFilter.clear()
        controlsHidden = false
        if (hardware and (NativeBridge.HW_TILT or NativeBridge.HW_GYRO or NativeBridge.HW_SOLAR) != 0) {
            motion = MotionInput(this, hardware) { toast(it) }
        }
        if (hardware and NativeBridge.HW_RUMBLE != 0) rumble = Rumble(this)
        // A fresh SurfaceView for every session: Restart stops and starts within one frame, and the new render
        // thread only gets a window when the view's surface is created (the old one kept the last frame).
        session++
        sendCheats() // after the ROM, its save file and the automatic state: before the first frame
        NativeBridge.start()
        playing = true
        setFullScreen(true)
        startExtras()
    }

    /** The cartridge's sensors and rumble run only while the game does. */
    private fun startExtras() {
        motion?.start()
        rumble?.start()
    }

    private fun stopExtras() {
        motion?.stop()
        rumble?.stop()
    }

    private fun stopGame() {
        stopExtras()
        motion = null
        rumble = null
        NativeBridge.stop() // writes the save
        playing = false
        menu = null
        setFullScreen(false)
    }

    /** Back: the game pauses (sound, sensors, rumble too; the save file is written) behind the menu. */
    private fun openMenu() {
        stopExtras()
        NativeBridge.pause()
        slotsChanged++
        menu = MenuPage.MAIN
    }

    private fun resumeGame() {
        menu = null
        NativeBridge.resume()
        startExtras()
    }

    private fun saveTo(slot: Int) {
        replaceSlot = null
        if (states?.save(slot) == true) toast("Saved to slot $slot") else toast("Couldn't save the state.")
        slotsChanged++
        resumeGame()
    }

    private fun loadFrom(slot: Int) {
        if (states?.load(slot) == true) resumeGame()
        else toast("This state can't be loaded (damaged, or made by an older version).")
    }

    /** The moment the player left, for resuming it next time (the game must be paused). */
    private fun saveAuto() {
        if (playing) states?.save(States.AUTO)
    }

    private fun quitGame() {
        saveAuto()
        stopGame()
        refreshLibrary() // it is now first, with the picture of where it was left
    }

    private fun restartGame() {
        askRestart = false
        val g = current ?: return
        stopGame()
        open(g, resumeAuto = false)
    }

    /** Buttons to the game; ⏩ and ⟲ (touch or keys) to the switch and the emulation thread. */
    private fun sendKeys(now: Long = SystemClock.uptimeMillis()) {
        val all = KeyMap.merge(keyboardMask, touchMask)
        NativeBridge.setKeys(all and KeyMap.GBA_BUTTONS)
        val fastDown = all and KeyMap.FAST != 0
        if (fastDown != fastSwitch.held) {
            if (fastDown) fastSwitch.press(now) else fastSwitch.release(now)
        }
        fastOn = fastSwitch.switchedOn
        NativeBridge.setFastForward(if (fastSwitch.fast) fastSpeed else 0)
        NativeBridge.setRewinding(all and KeyMap.REWIND != 0)
    }

    private fun releaseRepeatedKeys() {
        for ((bit, upAt) in repeatFilter.due(SystemClock.uptimeMillis())) {
            keyboardMask = keyboardMask and bit.inv()
            sendKeys(upAt) // the hold ended at the key-up, which decides tap or hold
        }
    }

    /** The panel changed the list (the game is paused behind the menu): keep it and switch the cheats. */
    private fun changeCheats(list: List<Cheat>) {
        cheats = list
        if (cheatStore?.save(list) == false) toast("Couldn't save the cheats.")
        sendCheats()
    }

    /** The switched-on cheats into the emulator; one that no longer reads is switched off (and said so). */
    private fun sendCheats() {
        val bad = cheats.filter { it.on && NativeBridge.checkCheat(it.codes, it.type)[1] != 0 }
        if (bad.isNotEmpty()) {
            for (c in bad) toast("Cheat '${c.name}' can't be read and was switched off.")
            cheats = cheats.map { if (it in bad) it.copy(on = false) else it }
            cheatStore?.save(cheats)
        }
        val on = cheats.filter { it.on }
        if (NativeBridge.setCheats(on.map { it.codes }.toTypedArray(), on.map { it.type }.toIntArray()) != 0 && on.isNotEmpty()) {
            toast("The cheats couldn't be switched on.")
        }
    }

    private fun speedLabel(speed: Int) = if (speed == NativeBridge.FF_MAX) "Max" else "$speed×"

    /** The menu's fast-forward row: 2× → 4× → 8× → Max → 2×, remembered for all games. */
    private fun nextFastSpeed() {
        changeSettings(settings.copy(fastSpeed = when (fastSpeed) {
            2 -> 4
            4 -> 8
            8 -> NativeBridge.FF_MAX
            else -> 2
        }))
    }

    /** A setting changed (Settings screen, menu row): kept, and used at once where it can be. */
    private fun changeSettings(s: Settings) {
        settings = s
        s.save(settingsStore)
        fastSpeed = s.fastSpeed
        applySettings(s)
        if (playing) sendKeys() // a switched-on fast mode uses the new speed
    }

    /** Picture and sound at once; rewind for the next game (the touch buttons read [settings] themselves). */
    private fun applySettings(s: Settings) {
        NativeBridge.setPicture(s.size, s.smooth)
        NativeBridge.setSound(s.sound, s.soundBursts)
        NativeBridge.setRewindBudget(s.rewindBudget)
    }

    /** The layout editor turns the screen to the edited orientation (the phone may be held either way). */
    private fun openLayoutEditor(sideways: Boolean) {
        editPicture = if (playing) NativeBridge.frameBgr555()?.let(States::bitmapOf) else null // paused behind the menu
        orientationBeforeEditor = if (resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE) {
            ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
        } else ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
        requestedOrientation = if (sideways) ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE else ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
        editLayout = sideways
    }

    /**
     * Back the way the screen was, then free again. Found while prototyping: with auto-rotate off, Android keeps the
     * editor's orientation as the locked one, so just freeing the screen would leave it turned.
     */
    private fun closeLayoutEditor() {
        editLayout = null
        editPicture = null
        requestedOrientation = orientationBeforeEditor
        handler.postDelayed({
            if (editLayout == null) requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
        }, 1000)
    }

    private fun biosPicked(uri: Uri) {
        val bytes = readBytes(uri) ?: run { toast("Could not read that file."); return }
        when (Bios.check(bytes)) {
            Bios.Check.WRONG_SIZE -> toast("That isn't a GBA BIOS (it must be 16 KB).")
            Bios.Check.UNUSUAL -> unusualBios = bytes
            Bios.Check.USUAL -> installBios(bytes)
        }
    }

    private fun installBios(bytes: ByteArray) {
        if (!Bios.install(filesDir, bytes)) {
            toast("Couldn't copy the BIOS file.")
            return
        }
        changeSettings(settings.copy(biosFile = true))
        toast("BIOS file chosen: it is used the next time a game opens.")
    }

    private fun toast(text: String) = Toast.makeText(this, text, Toast.LENGTH_LONG).show()

    private fun setFullScreen(on: Boolean) {
        val bars = WindowCompat.getInsetsController(window, window.decorView)
        if (on) {
            bars.systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            bars.hide(WindowInsetsCompat.Type.systemBars())
            window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        } else {
            bars.show(WindowInsetsCompat.Type.systemBars())
            window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        }
    }

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        waitingKey?.let { (bit, device) -> // Settings → Keyboard & controller: this key is the button's now
            if (event.keyCode == KeyEvent.KEYCODE_BACK || event.keyCode == KeyEvent.KEYCODE_ESCAPE) {
                if (event.action == KeyEvent.ACTION_UP) waitingKey = null
            } else if (event.action == KeyEvent.ACTION_DOWN && event.repeatCount == 0) {
                keys.assign(bit, device, event.keyCode)
                keys.use()
                keysChanged++
                waitingKey = null
            }
            return true
        }
        if (event.keyCode == KeyEvent.KEYCODE_ESCAPE) { // Esc on a keyboard = Back (the menu)
            if (event.action == KeyEvent.ACTION_UP) onBackPressedDispatcher.onBackPressed()
            return true
        }
        val motionKey = MotionMath.motionKey(event.keyCode)
        val m = motion
        if (playing && motionKey != 0 && m != null) { // I/J/K/L tilt, Q/E turn
            when (event.action) {
                KeyEvent.ACTION_DOWN -> m.setKey(motionKey, true)
                KeyEvent.ACTION_UP -> m.setKey(motionKey, false)
            }
            controlsHidden = true
            return true
        }
        if (playing && KeyMap.isKeyboardExtra(event.keyCode)) { // F / R: see KeyRepeatFilter
            val bit = KeyMap.bitFor(event.keyCode)
            when (event.action) {
                KeyEvent.ACTION_DOWN -> if (repeatFilter.down(bit, event.eventTime)) {
                    keyboardMask = keyboardMask or bit
                    sendKeys(event.eventTime)
                }
                KeyEvent.ACTION_UP -> {
                    val wait = repeatFilter.up(bit, event.eventTime)
                    if (wait >= 0) handler.postDelayed(::releaseRepeatedKeys, wait)
                }
            }
            controlsHidden = true
            return true
        }
        if (playing && KeyMap.bitFor(event.keyCode) != 0) {
            when (event.action) {
                KeyEvent.ACTION_DOWN -> keyboardMask = KeyMap.update(keyboardMask, event.keyCode, true)
                KeyEvent.ACTION_UP -> keyboardMask = KeyMap.update(keyboardMask, event.keyCode, false)
            }
            controlsHidden = true // a keyboard or gamepad is in use
            sendKeys()
            return true
        }
        return super.dispatchKeyEvent(event)
    }

    override fun onPause() {
        super.onPause()
        stopExtras()
        NativeBridge.pause() // returns once the save is written
        saveAuto() // Home, another app, screen off: keep the moment
    }

    override fun onResume() {
        super.onResume()
        if (playing && menu == null && replaceSlot == null && !askRestart) {
            NativeBridge.resume()
            startExtras()
        }
        if (!playing) refreshLibrary() // games added to the folder meanwhile, back from a picker
    }

    override fun onDestroy() {
        if (isFinishing) NativeBridge.stop()
        super.onDestroy()
    }
}
```

- [x] **Step 4: Run the tests**

```bash
bash tools/dev.sh jvmtest
grep -h -o 'testsuite name="[^"]*" tests="[0-9]*" skipped="[0-9]*" failures="[0-9]*"' app/build/test-results/testDebugUnitTest/*.xml
```
Expected: **84 tests** (81 + 3), 0 failures.

- [x] **Step 4b: Claude's check on the emulator (adb)** — in the user's library (games folder `Download/GBA` with Claude's test file `m7_demo.gba`); the user's `settings.xml` is kept aside and put back (switching Show speed back Off would add an explicit `show_speed` line). Use a scratchpad folder `$SP`. Helpers:

```bash
ADB_EXE="$(cygpath -u "$LOCALAPPDATA")/Android/Sdk/platform-tools/adb"
EMU=$(MSYS_NO_PATHCONV=1 "$ADB_EXE" devices | awk '$1 ~ /^emulator-/ && $2 == "device" { print $1; exit }')
ADB() {  # the emulator only: refuse when none is running (never fall back to a USB phone)
    [ -n "$EMU" ] || { echo "ADB: no emulator running - refusing" >&2; return 1; }
    MSYS_NO_PATHCONV=1 "$ADB_EXE" -s "$EMU" "$@"
}
texts() { ADB shell uiautomator dump /sdcard/ui.xml >/dev/null; ADB shell cat /sdcard/ui.xml | tr '>' '\n' | grep -oE 'text="[^"]+"'; }
tap_on() {  # tap the centre of the first element whose $1 (text or content-desc) is exactly $2; waits up to 10 s
    for i in 1 2 3 4 5 6 7 8 9 10; do
        ADB shell uiautomator dump /sdcard/ui.xml >/dev/null
        b=$(ADB shell cat /sdcard/ui.xml | tr '>' '\n' | grep -F "$1=\"$2\"" | grep -oE 'bounds="\[[0-9]+,[0-9]+\]\[[0-9]+,[0-9]+\]"' | head -1)
        if [ -n "$b" ]; then
            set -- $(echo "$b" | grep -oE '[0-9]+')
            ADB shell input tap $((($1 + $3) / 2)) $((($2 + $4) / 2))
            return 0
        fi
        sleep 1
    done
    echo "not on screen: $1=$2" >&2
    return 1
}
settings_md5() { ADB shell run-as com.tengen.gbaemu md5sum shared_prefs/settings.xml 2>/dev/null | cut -c1-12; }
show_speed() {  # from the game: in-game menu → Settings… → Picture → Show speed → $1, back to the game
    ADB shell input keyevent KEYCODE_BACK; sleep 1.5; tap_on text "Settings…"; sleep 1.5; tap_on text "Picture"; sleep 1
    tap_on text "Show speed"; sleep 1; tap_on text "$1"; sleep 1
    ADB shell input keyevent KEYCODE_BACK; sleep 1; ADB shell input keyevent KEYCODE_BACK; sleep 1; tap_on text "▶  Resume"; sleep 3
}
```

Then (as a script, `bash <file>`; it stops if m7_demo is not in the library):

```bash
ADB shell input keyevent KEYCODE_HOME; ADB shell am force-stop com.tengen.gbaemu
bash tools/dev.sh install
before=$(settings_md5)
# the user's settings kept aside (switching Show speed back Off would leave an explicit show_speed = 0 line)
ADB shell run-as com.tengen.gbaemu cp shared_prefs/settings.xml files/settings.xml.keep
bash tools/dev.sh launch; sleep 4
tap_on text "m7_demo" || { echo "STOP: m7_demo is not in the library"; exit 1; }
sleep 3; echo "labels while off: $(texts | grep -c 'fps')"                     # 0: off by default
ADB shell input keyevent KEYCODE_BACK; sleep 1.5; tap_on text "Settings…"; sleep 1.5; tap_on text "Picture"; sleep 1
texts | head -8                                                                # Pixels, Size, Show speed Off, the notes
ADB shell input keyevent KEYCODE_BACK; sleep 1; ADB shell input keyevent KEYCODE_BACK; sleep 1; tap_on text "▶  Resume"; sleep 2
show_speed On; texts | grep 'fps'                                              # e.g. 60 fps · 1.0×
show_speed Off; echo "labels after Off: $(texts | grep -c 'fps')"
ADB logcat -d -s AndroidRuntime:E | tail -3
ADB shell am force-stop com.tengen.gbaemu
ADB shell run-as com.tengen.gbaemu cp files/settings.xml.keep shared_prefs/settings.xml
ADB shell run-as com.tengen.gbaemu rm files/settings.xml.keep
echo "settings: $before -> $(settings_md5)"                                    # unchanged
```

Expected: `labels while off: 0`; the Picture page lists `Pixels`, `Size`, `Show speed`, `Off`; with it On a label like `text="60 fps · 1.0×"` (59-60 fps, 1.0×); `labels after Off: 0`; no `AndroidRuntime` crash; `settings: X -> X` (unchanged).

- [x] **Step 5: Commit**

```bash
git add app/src/main/java/com/tengen/gbaemu/SpeedMeter.kt app/src/test/java/com/tengen/gbaemu/SpeedMeterTest.kt app/src/main/java/com/tengen/gbaemu/Settings.kt app/src/test/java/com/tengen/gbaemu/SettingsTest.kt app/src/main/java/com/tengen/gbaemu/SettingsScreens.kt app/src/main/java/com/tengen/gbaemu/Screens.kt app/src/main/java/com/tengen/gbaemu/MainActivity.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-03-milestone-7a-speed.md
git commit -m "feat(android): Show speed - frames per second and x real time on the game screen" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 7: The rewind test on a real phone, and the speed on phone B

**Files:**
- Modify: `app/src/androidTest/java/com/tengen/gbaemu/RewindTest.kt` (`rewindingStepsBackToTheOldestSnapshotAndThenPlaysOn` counts frames from the first rewind step: on phone B it failed 59 → 60 — rewinding starts at the emulation thread's next loop step, so a frame already running still counts; `main`'s unchanged code failed it 4 of 5 times there)

- [x] **Step 1: Write the code**

Replace `app/src/androidTest/java/com/tengen/gbaemu/RewindTest.kt`:

```kotlin
package com.tengen.gbaemu

import androidx.test.ext.junit.runners.AndroidJUnit4
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Fast-forward and rewind on the phone: exact replays through a rewind, the speeds, rewinding a running game. */
@RunWith(AndroidJUnit4::class)
class RewindTest {
    @After
    fun stopSession() = NativeBridge.stop()

    @Test
    fun everyApprovedFingerprintSurvivesARewind() {
        val entries = JSONObject(TestRoms.text("expected_frames.json")).getJSONArray("frames")
        assertEquals("approved entries", 32, entries.length())
        val failures = mutableListOf<String>()
        for (i in 0 until entries.length()) {
            val e = entries.getJSONObject(i)
            val frames = e.getInt("frames")
            // As CMake's rewind_<name> tests: at two thirds (short runs: the last frame but one), back half the snapshots, at least 2.
            val at = (frames * 2 / 3).let { if (it < 9) frames - 1 else it }
            val steps = maxOf(2, at / 4 / 2)
            val got = NativeBridge.debugRunFramesViaRewind(TestRoms.rom(e.getString("rom")), frames, at, steps)
            val parts = got?.split(" ") ?: listOf("rewind-failed", "")
            val frameOk = parts[0] == e.getString("hash")
            val audioOk = !e.has("audio") || parts[1] == e.getString("audio")
            if (!frameOk || !audioOk) failures += "${e.getString("name")}: got $got"
        }
        assertTrue(failures.joinToString("\n"), failures.isEmpty())
    }

    /** Frames the game runs in [ms] at the given fast-forward speed. */
    private fun framesIn(ms: Long, speed: Int): Long {
        NativeBridge.setFastForward(speed)
        Thread.sleep(300) // settle
        val from = NativeBridge.debugFrameCount()
        Thread.sleep(ms)
        return NativeBridge.debugFrameCount() - from
    }

    @Test
    fun fastForwardRunsThatManyTimesFaster() { // by the clock (no sound)
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba")))
        NativeBridge.debugSetAudioEnabled(false)
        NativeBridge.start()
        val normal = framesIn(1000, 0)
        val double = framesIn(1000, 2)
        val four = framesIn(1000, 4)
        val max = framesIn(1000, NativeBridge.FF_MAX)
        val back = framesIn(1000, 0)
        assertTrue("normal $normal", normal in 50..70)
        assertTrue("2x $double vs $normal", double in (normal * 17 / 10)..(normal * 23 / 10))
        assertTrue("4x $four vs $normal", four >= normal * 3)
        assertTrue("max $max vs 4x $four", max >= four)
        assertTrue("back to normal $back", back in 50..70)
    }

    @Test
    fun fastForwardWithSoundIsPacedByTheSound() {
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba")))
        NativeBridge.start()
        val normal = framesIn(1000, 0)
        val double = framesIn(1000, 2)
        assertTrue("normal $normal", normal in 50..70)
        assertTrue("2x $double vs $normal", double in (normal * 17 / 10)..(normal * 23 / 10))
        assertEquals("sound still playing", 1L, NativeBridge.debugAudioStats()[0])
    }

    @Test
    fun rewindingStepsBackToTheOldestSnapshotAndThenPlaysOn() {
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba")))
        NativeBridge.debugSetAudioEnabled(false)
        NativeBridge.start()
        NativeBridge.setKeys(KeyMap.UP)
        Thread.sleep(1000)
        NativeBridge.setKeys(0)
        val snaps = NativeBridge.debugRewindStats()[0]
        assertTrue("snapshots after 1 s: $snaps", snaps in 10..20)
        NativeBridge.setRewinding(true)
        // Rewinding starts at the emulation thread's next loop step: a frame already running still finishes (on a
        // real phone a frame can take long enough for that to happen nearly every time), so count from the first step.
        val deadline = System.nanoTime() + 1_000_000_000L
        while (NativeBridge.debugRewindStats()[2] == 0L && System.nanoTime() < deadline) Thread.sleep(1)
        val framesBefore = NativeBridge.debugFrameCount()
        Thread.sleep(snaps * 1000 / 60 + 700) // one step per 1/60 s, then some more at the oldest
        val stats = NativeBridge.debugRewindStats()
        assertEquals("no frames run while rewinding", framesBefore, NativeBridge.debugFrameCount())
        assertEquals("only the oldest snapshot left", 1L, stats[0])
        assertTrue("steps ${stats[2]} for $snaps snapshots", stats[2] in (snaps - 1)..(snaps + 2)) // a snapshot may come before rewinding starts
        NativeBridge.setRewinding(false)
        Thread.sleep(500)
        assertTrue("plays on", NativeBridge.debugFrameCount() > framesBefore + 20)
    }
}
```

- [x] **Step 2: On-device tests on the emulator and on phone B**

```bash
rm -rf app/build/outputs/androidTest-results; bash tools/dev.sh androidtest 2>&1 | grep -E "^e: |Starting|BUILD FAILED"; grep -h -o 'tests="[0-9]*" failures="[0-9]*"' app/build/outputs/androidTest-results/connected/debug/*.xml
rm -rf app/build/outputs/androidTest-results; bash tools/dev.sh phone-androidtest 2>&1 | grep -E "^e: |Starting|BUILD FAILED"; grep -h -o 'tests="[0-9]*" failures="[0-9]*"' app/build/outputs/androidTest-results/connected/debug/*.xml
ADB_EXE="$(cygpath -u "$LOCALAPPDATA")/Android/Sdk/platform-tools/adb"
for i in 1 2 3 4 5; do MSYS_NO_PATHCONV=1 "$ADB_EXE" -s its serial shell am instrument -w -e class com.tengen.gbaemu.RewindTest#rewindingStepsBackToTheOldestSnapshotAndThenPlaysOn com.tengen.gbaemu.test/androidx.test.runner.AndroidJUnitRunner | tr -d '\r' | grep -E "^OK|expected" | head -1; done
```

Expected: `tests="53" failures="0"` on the emulator (`Pixel_9_API_36_1`) and on phone B — the 32 fingerprints now proven on a real arm64 phone too; then `OK (1 test)` five times.

- [x] **Step 3: The speed on phone B (the finish line)**

```bash
bash tools/dev.sh phone-bench --runs 3 --check
```

Expected: 6 scenes `ok ok` and at least (spec §1): game A ≥ 1.93×, game C ≥ 3.05×, game D ≥ 1.55×, game B ≥ 1.15×, `m7_demo` ≥ 4.25×, `snd1_demo` ≥ 11.8× (the prototype: 2.03 / 3.21 / 1.63 / 1.22 / 4.50 / 12.5). A scene below its line: run once more (a warm or busy phone); still below → stop and tell the user. Record the before/after table in `docs/PROGRESS.md`.

- [x] **Step 4: Commit**

```bash
git add app/src/androidTest/java/com/tengen/gbaemu/RewindTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-03-milestone-7a-speed.md
git commit -m "test(android): the rewind test counts from the first rewind step (real phones)" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 8: User check and milestone 7a exit

- [x] **Step 1: Full run**

```bash
bash tools/dev.sh test && bash tools/dev.sh unit && bash tools/dev.sh jvmtest && bash tools/dev.sh androidtest && bash tools/dev.sh bench --check
```

Expected: CTest 120/120 (sanitizers on, no reports), 410 unit tests, Kotlin 84, on-device 53 (emulator), 6 scenes `ok`. Then `bash tools/dev.sh phone-install` (the branch's app on phone B, its data kept).

- [x] **Step 2: The user's check** (on phone B)

Settings → Picture → Show speed On; game A, game C and game B with sound look and sound as before, no glitches; fast-forward at Max clearly faster than before (the label well above 1.0×).

- [x] **Step 3: Docs and the phone**

`docs/PROGRESS.md` (7a ✅ with the before/after table and the user's check; Next = milestone 7b brainstorming — ARM64 assembly + NEON for the CPU loop and the pixel mixing, aiming at ≥ 4× on phone B's small core); spec status → `Implemented — milestone 7a complete <date> (tag milestone-7a)`; parent spec §10 row 7 → 7a ✅; `CLAUDE.md` key files → 7b needs brainstorming; tick every box here. On phone B: `bash tools/dev.sh phone-clean` (removes `/data/local/tmp/gbabench`) and remove the test package — the app and the user's `Download/GBA` stay:

```bash
MSYS_NO_PATHCONV=1 "$(cygpath -u "$LOCALAPPDATA")/Android/Sdk/platform-tools/adb" -s its serial uninstall com.tengen.gbaemu.test
```

- [x] **Step 4: Commit, tag, merge** — ask the user how to integrate (so far always a fast-forward merge into `main`); then superpowers:finishing-a-development-branch, `bash tools/dev.sh test` and `bash tools/dev.sh androidtest` on `main`, delete the branch, record the merge in PROGRESS.md; delete the prototype and replay worktrees (`rm -rf` the folders in Git Bash, `git worktree prune`, `git branch -D proto-7a replay-7a`) with the user's OK.

```bash
git add -A docs CLAUDE.md
git commit -m "docs: milestone 7a complete - measuring and C speed-ups" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git tag milestone-7a
```
