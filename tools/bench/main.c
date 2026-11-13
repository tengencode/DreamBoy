/*
 * Speed benchmark: plays the scenes of a scene list and times them.
 *   gbabench [--scenes <file>] [--roms <dir>] [--runs <n>] [--only <scene>] [--check]
 *            [--shot <frame>:<file.png>]... [--cpu <n>] [--profile <file>]
 *            [--ra <n>]
 * Scene list: one scene per line, fields separated by '|' (blank lines and lines starting with # skipped):
 *   name | rom file | save file or - | keys | warm-up frames | timed frames | picture hash | sound hash
 * keys: the testrunner's --keys syntax ("10:DOWN,12:-,14:A+B"), or - for none; CW / CCW also turn a rotation
 * sensor clockwise / counter-clockwise while held. The ROM and save are read from
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
 * --ra n (milestones 14-15): n made-up RetroAchievements (2 conditions each, never met) and n/10 made-up leaderboards
 *   (never started) checked after every frame, as the app does.
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
#include "key_script.h"
#include "png.h"
#include "ra.h"
#include "ra_fake.h"

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
#define MAX_SHOTS 16

typedef struct {
    char name[64], rom[256], save[256];
    key_script_t keys;
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

/* The scene's keys: the testrunner's --keys syntax, or - for none. */
static bool parse_keys(const char *list, scene_t *sc) {
    sc->keys = (key_script_t){NULL, 0, 0};
    return !strcmp(list, "-") || key_script_parse(list, &sc->keys);
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

/* --ra n: one RetroAchievements client with n made-up achievements, signed in to the pretend server. */
static ra_t *bench_ra;

static char *made_up_set(int n) {
    char *json = malloc((size_t)n * 200 + 3);
    if (!json) return NULL;
    char *p = json;
    *p++ = '[';
    uint32_t x = 12345;
    for (int i = 0; i < n; i++) {
        x = x * 1103515245u + 12345u;
        uint32_t a = (x >> 8) % 0x47FFCu, b = (x >> 4) % 0x47FFCu;
        p += sprintf(p, "%s{\"ID\":%d,\"Title\":\"A%d\",\"Description\":\"-\",\"Flags\":3,\"Points\":1,"
                        "\"MemAddr\":\"0xX%06x=%u_0xX%06x=%u\",\"Author\":\"-\",\"BadgeName\":\"00001\","
                        "\"Created\":1,\"Modified\":1}",
                     i ? "," : "", 100000 + i, i, (unsigned)a, (unsigned)(x | 0x80000000u), (unsigned)b,
                     (unsigned)((x * 7u) | 0x80000000u));
    }
    *p++ = ']';
    *p = '\0';
    return json;
}

/* n made-up leaderboards whose start (a 32-bit compare) is practically never true: their conditions are tested every
 * frame, as in a game with boards (rcheevos runs boards in hardcore, its default here). */
static char *made_up_boards(int n) {
    char *json = malloc((size_t)n * 240 + 3);
    if (!json) return NULL;
    char *p = json;
    *p++ = '[';
    uint32_t x = 54321;
    for (int i = 0; i < n; i++) {
        x = x * 1103515245u + 12345u;
        uint32_t a = (x >> 8) % 0x47FFCu, b = (x >> 4) % 0x47FFCu;
        p += sprintf(p, "%s{\"ID\":%d,\"Title\":\"L%d\",\"Description\":\"-\","
                        "\"Mem\":\"STA:0xX%06x=%u::CAN:0=1::SUB:0=1::VAL:0xX%06x\",\"Format\":\"VALUE\","
                        "\"LowerIsBetter\":false,\"Hidden\":false}",
                     i ? "," : "", 200000 + i, i, (unsigned)a, (unsigned)(x | 0x80000000u), (unsigned)b);
    }
    *p++ = ']';
    *p = '\0';
    return json;
}

static bool bench_ra_start(int n) {
    char *set = made_up_set(n);
    char *boards = made_up_boards(n / 10 > 0 ? n / 10 : 1);
    ra_fake_t f = {0};
    f.hash = "00000000000000000000000000000000";
    f.achievements = set;
    f.leaderboards = boards;
    bench_ra = ra_create();
    if (!set || !boards || !bench_ra) return false;
    ra_login_password(bench_ra, "Tester", "secret");
    ra_fake_serve(&f, bench_ra);
    ra_load_game(bench_ra, f.hash);
    ra_fake_serve(&f, bench_ra);
    static uint8_t zeros[0x40000];
    ra_memory_t m = {zeros, zeros, NULL, 0};
    ra_idle(bench_ra, &m);
    free(set);
    free(boards);
    ra_summary_t s;
    if (!ra_summary(bench_ra, &s) || s.total != (uint32_t)n) {
        fprintf(stderr, "--ra: the made-up set did not load (%u of %d)\n", s.total, n);
        return false;
    }
    if (!ra_has_leaderboards(bench_ra)) {
        fprintf(stderr, "--ra: the made-up boards did not load\n");
        return false;
    }
    return true;
}

/* Frames [from, to) of the scene: its keys, the sound into *sound, the screenshots asked for. */
static void play(gba_t *g, const scene_t *sc, long from, long to, uint64_t *sound, const shot_t *shots, int shot_count) {
    int16_t buf[1024 * 2];
    size_t n;
    for (long frame = from; frame < to; frame++) {
        uint16_t keys = key_script_at(&sc->keys, frame);
        gba_set_keys(g, keys & 0x3FF);
        gba_set_gyro(g, (keys & KEY_CW) ? SPIN_RATE : (keys & KEY_CCW) ? -SPIN_RATE : 0);
        gba_run_frame(g);
        if (bench_ra) {
            ra_memory_t m = {gba_iwram(g), gba_ewram(g), gba_saveram(g), gba_saveram_size(g)};
            ra_do_frame(bench_ra, &m);
        }
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
                    "                [--shot <frame>:<file.png>]... [--cpu <n>] [--profile <file>] [--ra <n>]\n");
    return 3;
}

int main(int argc, char **argv) {
    const char *scenes_path = "tests/bench/scenes.txt", *roms = ".", *only = NULL;
    int runs = 3, shot_count = 0, cpu = -1, ra_count = 0;
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
        else if (!strcmp(argv[i], "--ra") && i + 1 < argc) ra_count = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc && shot_count < MAX_SHOTS) {
            char *colon;
            long frame = strtol(argv[++i], &colon, 10);
            if (*colon != ':') return usage();
            shots[shot_count++] = (shot_t){frame, colon + 1};
        } else return usage();
    }
    if (runs < 1 || (shot_count && !only)) return usage();
    if (ra_count > 0 && !bench_ra_start(ra_count)) return 3;

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
            res.picture = frame_hash(gba_framebuffer(g), 240 * 160);
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
    ra_destroy(bench_ra);
    return check && bad ? 1 : 0;
}
