#include "emu_thread.h"

#include <android/log.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "audio_out.h"
#include "audio_ring.h"
#include "frame_pacer.h"
#include "gb.h"
#include "gba_debug.h"
#include "key_latch.h"
#include "rate_control.h"
#include "resampler.h"
#include "save_file.h"
#include "save_timer.h"

#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "gbaemu", __VA_ARGS__)

#define AUDIO_TARGET_MS 30        /* the sound buffer is kept at about this much */
#define WATCHDOG_NS 250000000LL   /* buffer not draining this long while playing → the stream is stuck */
#define WATCHDOG_GRACE_NS 2000000000LL /* ...but not in the first 2 s after a start/resume: a waking sound
                                        * device (seen: 350 ms on the phone emulator) takes a while */
#define CONVERTED_FRAMES 8192     /* room for 1,024 GBA frames converted at up to ~8x */
#define REWIND_EVERY 4            /* frames between snapshots; rewinding steps back one per 1/60 s */

struct emu {
    machine_t *m;
    gba_t *g; /* the machine's Game Boy Advance core, for what only that console has; NULL for a Game Boy game */
    size_t pixels;    /* in a frame */
    int clock_frames; /* a Game Boy cartridge clock is told the time about once a second */
    atomic_int look;  /* a grey game's colours as last asked for */
    int look_set;     /* … and as the machine has them (the emulation thread's) */
    triple_buffer_t tb;
    pthread_t thread;
    bool started;
    pthread_mutex_t lock; /* guards paused/quit */
    pthread_cond_t wake;
    bool paused, quit;
    key_latch_t keys;
    atomic_ullong frames;
    atomic_uint keys_seen; /* test hook */
    atomic_int tilt_x, tilt_y, turn, light; /* cartridge sensors, from the UI thread; applied at frame start */
    atomic_int seen[4];                     /* test hook: the sensor values last applied (x, y, turn, light) */
    atomic_bool rumble;                     /* the cartridge's rumble motor after the last frame */
    atomic_int ff_speed;                    /* 0/1 normal, 2/4/8, EMU_FF_MAX (from the UI thread) */
    atomic_bool rewinding;                  /* from the UI thread */
    atomic_llong rw_snaps, rw_bytes, rw_steps; /* rewind stats (test hook), kept by the emulation thread */
    ra_t *ra;                      /* RetroAchievements, or NULL (14) */
    atomic_bool ra_reset;          /* forget half-done achievement progress before the next frame (a state load, a rewind) */
    pthread_mutex_t core_lock;     /* held by whoever uses a parked or not-started core: the UI thread's state
                                    * save / load, cheats, frame copy, peeks, emu_ra_idle; and the paused thread's ra_idle */
    bool parked;           /* the thread is stopped in a pause with the save written (guarded by lock) */

    /* Save file. Before start only the caller touches these; after start, only the emulation thread
     * (save_failed is shared). */
    char *save_path;                /* NULL: no save file */
    save_timer_t save_timer;
    emu_save_status_t save_status;
    atomic_bool save_failed;        /* a write failed since the last emu_save_error */

    /* Sound. The emulation thread alone touches out/rs/converted/sound_applied/bursts_applied; the rest is shared
     * as marked. */
    atomic_bool sound_wanted;          /* Settings: sound on (any thread) */
    atomic_int sound_bursts;           /* Settings: AAudio's buffer in bursts (any thread) */
    bool sound_applied;                /* what the emulation thread last acted on: a stream that gave up stays */
    int bursts_applied;                /* closed until the wish changes */
    audio_out_t *out;                  /* NULL: no sound, the clock paces the game */
    audio_ring_t ring;                 /* emulation thread → AAudio callback */
    resampler_t rs;
    int16_t converted[CONVERTED_FRAMES * 2];
    audio_stats_t stats;               /* kept by the callback */
    atomic_bool sound_on;
    atomic_int rate, burst, reopens;
    atomic_bool fake_disconnect;       /* test hook */
    atomic_bool fake_stuck;            /* test hook */
    bool watchdog_used;
    int64_t sound_started_ns;          /* last start/resume of the stream */
};

static int64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

static bool paused_or_quitting(emu_t *e) {
    pthread_mutex_lock(&e->lock);
    bool stop = e->paused || e->quit;
    pthread_mutex_unlock(&e->lock);
    return stop;
}

static void sound_open(emu_t *e) {
    e->out = audio_out_open(&e->ring, &e->stats, e->bursts_applied);
    if (e->out && !audio_out_start(e->out)) {
        audio_out_close(e->out);
        e->out = NULL;
    }
    e->sound_started_ns = now_ns();
    atomic_store(&e->rate, e->out ? audio_out_rate(e->out) : 0);
    atomic_store(&e->burst, e->out ? audio_out_burst(e->out) : 0);
    atomic_store(&e->sound_on, e->out != NULL);
    if (!e->out) LOGW("no sound: the clock paces the game");
}

static void sound_close(emu_t *e) {
    audio_out_close(e->out);
    e->out = NULL;
    atomic_store(&e->sound_on, false);
}

/* Close and open again (device change, stuck stream). The ring is emptied while nothing reads it. */
static void sound_reopen(emu_t *e) {
    sound_close(e);
    audio_ring_clear(&e->ring);
    resampler_init(&e->rs);
    atomic_fetch_add(&e->reopens, 1);
    sound_open(e);
}

/* Settings changed (sound on/off, delay): act once per change. */
static void apply_sound_settings(emu_t *e) {
    bool want = atomic_load(&e->sound_wanted);
    int bursts = atomic_load(&e->sound_bursts);
    if (want == e->sound_applied && bursts == e->bursts_applied) return;
    e->sound_applied = want;
    e->bursts_applied = bursts;
    if (e->out) {
        sound_close(e);
        audio_ring_clear(&e->ring);
        resampler_init(&e->rs);
    }
    if (want) sound_open(e);
}

static size_t target_frames(emu_t *e) {
    return (size_t)audio_out_rate(e->out) * AUDIO_TARGET_MS / 1000;
}

/* Every sample of the frame: into the resampler, then into the ring (or dropped without sound, and at
 * EMU_FF_MAX). At N× speed N times fewer samples come out: the sound plays N times faster and higher. */
static void feed_sound(emu_t *e, int speed) {
    int16_t in[1024 * 2];
    size_t n;
    while ((n = machine_audio_read(e->m, in, 1024)) > 0) {
        if (!e->out || speed == EMU_FF_MAX) continue;
        double fill = (double)audio_ring_queued(&e->ring) / (2.0 * (double)target_frames(e));
        double ratio = rate_control_ratio(audio_out_rate(e->out), fill) / (speed >= 2 ? speed : 1);
        size_t m = resampler_run(&e->rs, in, n, ratio, e->converted, CONVERTED_FRAMES);
        audio_ring_push(&e->ring, e->converted, m);
    }
}

/* Sound pacing: wait (1 ms at a time) while the ring holds more than the target. A ring that stops
 * draining for 250 ms means a stuck stream: reopen once; the second time, give up on sound. */
static void sound_pace(emu_t *e) {
    size_t target = target_frames(e);
    size_t last = audio_ring_queued(&e->ring);
    int64_t since = now_ns();
    for (;;) {
        size_t q = audio_ring_queued(&e->ring);
        if (q <= target || paused_or_quitting(e)) return;
        int64_t now = now_ns();
        if (q < last || now - e->sound_started_ns < WATCHDOG_GRACE_NS) {
            last = q;
            since = now;
        } else if (now - since > WATCHDOG_NS) {
            LOGW("sound stream stuck: %s", e->watchdog_used ? "giving up on sound" : "reopening it");
            if (e->watchdog_used) {
                sound_close(e);
            } else {
                e->watchdog_used = true;
                sound_reopen(e);
            }
            return;
        }
        struct timespec ms = {0, 1000000};
        nanosleep(&ms, NULL);
    }
}

/* The sensor values from the UI thread into the core (cheap: a few stores; harmless for carts without them). */
static void apply_sensors(emu_t *e) {
    int x = atomic_load(&e->tilt_x), y = atomic_load(&e->tilt_y);
    int turn = atomic_load(&e->turn), light = atomic_load(&e->light);
    machine_set_tilt(e->m, (int16_t)x, (int16_t)y); /* a tilt cartridge of either console */
    if (e->g) {
        gba_set_gyro(e->g, (int16_t)turn);
        gba_set_light(e->g, (uint8_t)light);
    }
    atomic_store(&e->seen[0], x);
    atomic_store(&e->seen[1], y);
    atomic_store(&e->seen[2], turn);
    atomic_store(&e->seen[3], light);
}

/* The core's save bytes into the save file (nothing without a path or a chip of known size). */
static void write_save(emu_t *e) {
    size_t size = machine_save_size(e->m);
    if (!e->save_path || !size) return;
    uint8_t *save = malloc(size);
    bool ok = save && machine_save_write(e->m, save, size) == size && save_file_write(e->save_path, save, size);
    free(save);
    if (!ok) {
        LOGW("cannot write the save file %s", e->save_path);
        atomic_store(&e->save_failed, true);
        save_timer_frame(&e->save_timer, true); /* still pending: tried again in about 1 s, and at pause / stop */
    }
}

/* Pause, stop: write now if the game saved since the last write. */
static void flush_save(emu_t *e) {
    if (machine_save_dirty(e->m)) save_timer_frame(&e->save_timer, true);
    if (save_timer_flush(&e->save_timer)) write_save(e);
}

/* Wait for the clock's next frame time. */
static void clock_wait(frame_pacer_t *pacer) {
    int64_t due = pacer_next(pacer, now_ns());
    struct timespec ts = {(time_t)(due / 1000000000), (long)(due % 1000000000)};
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) == EINTR) {
    }
}

static void keep_rewind_stats(emu_t *e) {
    size_t snaps = 0, bytes = 0;
    if (e->g) gba_debug_rewind_stats(e->g, &snaps, &bytes);
    atomic_store(&e->rw_snaps, (long long)snaps);
    atomic_store(&e->rw_bytes, (long long)bytes);
}

static uint32_t gb_memory(void *ctx, uint32_t address, uint8_t *buffer, uint32_t count) {
    return gb_ra_peek(ctx, address, buffer, count);
}

static ra_memory_t memory_of(emu_t *e) {
    if (!e->g) { /* a Game Boy game: its own layout (17c) */
        ra_memory_t gb = {.peek = gb_memory, .ctx = machine_gb(e->m)};
        return gb;
    }
    size_t sram = gba_saveram_size(e->g);
    /* a chip whose size isn't known yet (no save so far) still has its bytes: else rcheevos would switch off, for
     * the whole session, every achievement that watches save memory */
    if (!sram && gba_save_type(e->g) != GBA_SAVE_NONE) sram = RA_SRAM_END - RA_SRAM_AT;
    ra_memory_t m = {gba_iwram(e->g), gba_ewram(e->g), gba_saveram(e->g), sram, NULL, NULL};
    return m;
}

/* The hardcore limits apply (a signed-in user, hardcore on, a game with achievements). */
static bool hardcore(emu_t *e) {
    return e->ra && ra_hardcore_active(e->ra);
}

static void check_achievements(emu_t *e) {
    if (!e->ra) return;
    ra_memory_t m = memory_of(e);
    if (atomic_exchange(&e->ra_reset, false)) ra_reset_progress(e->ra, &m);
    ra_do_frame(e->ra, &m);
}

/* One frame of play: keys, sensors, the frame, its picture and sound, the save timer. */
static void play_frame(emu_t *e, int speed) {
    unsigned keys = key_latch_take(&e->keys);
    atomic_fetch_or(&e->keys_seen, keys);
    machine_set_keys(e->m, (uint16_t)keys);
    apply_sensors(e);
    int look = atomic_load(&e->look);
    if (look != e->look_set) machine_set_look(e->m, e->look_set = look);
    if (!e->g && ++e->clock_frames >= 60) {
        e->clock_frames = 0;
        machine_set_rtc(e->m, (int64_t)time(NULL), 0);
    }
    machine_run_frame(e->m);
    check_achievements(e);
    atomic_store(&e->rumble, machine_rumble_active(e->m) != 0);
    memcpy(tb_back(&e->tb), machine_framebuffer(e->m), sizeof(uint16_t) * e->pixels);
    tb_publish(&e->tb);
    feed_sound(e, speed);
    if (save_timer_frame(&e->save_timer, machine_save_dirty(e->m))) write_save(e);
    atomic_fetch_add(&e->frames, 1);
}

/* One step back while rewinding: the snapshot's picture at once; its unread sound is dropped (silence). */
static void rewind_step(emu_t *e) {
    if (machine_rewind_step_back(e->m)) atomic_fetch_add(&e->rw_steps, 1);
    int16_t drop[1024 * 2];
    while (machine_audio_read(e->m, drop, 1024) > 0) {
    }
    atomic_store(&e->rumble, false);
    memcpy(tb_back(&e->tb), machine_framebuffer(e->m), sizeof(uint16_t) * e->pixels);
    tb_publish(&e->tb);
}

static void *emu_main(void *arg) {
    emu_t *e = arg;
    frame_pacer_t pacer;
    pacer_reset(&pacer);
    bool was_rewinding = false;
    e->sound_applied = atomic_load(&e->sound_wanted);
    e->bursts_applied = atomic_load(&e->sound_bursts);
    if (e->sound_applied) sound_open(e);
    for (;;) {
        pthread_mutex_lock(&e->lock);
        bool pausing = e->paused && !e->quit;
        pthread_mutex_unlock(&e->lock);
        if (pausing) {
            if (e->out) { /* stop the callback, then drop the stale sound */
                audio_out_pause(e->out);
                audio_ring_clear(&e->ring);
                resampler_init(&e->rs);
            }
            flush_save(e); /* outside the lock, so emu_pause's 1 s limit keeps working */
            pthread_mutex_lock(&e->lock);
            e->parked = true;
            pthread_cond_broadcast(&e->wake);
            while (e->paused && !e->quit) { /* RetroAchievements' upkeep about once a second (14) */
                struct timespec until;
                clock_gettime(CLOCK_MONOTONIC, &until);
                until.tv_sec += 1;
                if (pthread_cond_timedwait(&e->wake, &e->lock, &until) == ETIMEDOUT && e->paused && !e->quit && e->ra) {
                    pthread_mutex_unlock(&e->lock);
                    pthread_mutex_lock(&e->core_lock);
                    ra_memory_t m = memory_of(e);
                    ra_idle(e->ra, &m);
                    pthread_mutex_unlock(&e->core_lock);
                    pthread_mutex_lock(&e->lock);
                }
            }
            e->parked = false;
            bool resuming = !e->quit;
            pthread_mutex_unlock(&e->lock);
            if (e->out && resuming) {
                audio_out_start(e->out);
                e->sound_started_ns = now_ns();
            }
            pacer_reset(&pacer);
        }
        pthread_mutex_lock(&e->lock);
        bool quit = e->quit;
        pthread_mutex_unlock(&e->lock);
        if (quit) break;

        if (atomic_load(&e->rewinding) && !hardcore(e)) { /* the sound ring simply runs dry: silence */
            rewind_step(e);
            keep_rewind_stats(e);
            was_rewinding = true;
            clock_wait(&pacer);
            continue;
        }
        if (was_rewinding) {
            was_rewinding = false;
            atomic_store(&e->ra_reset, true); /* achievements: the game went back */
            save_timer_frame(&e->save_timer, true); /* the save chip may have gone back: rewrite the file */
            pacer_reset(&pacer);
        }
        apply_sound_settings(e);
        int speed = atomic_load(&e->ff_speed);
        for (int i = 0, n = speed >= 2 ? speed : 1; i < n; i++) play_frame(e, speed);
        keep_rewind_stats(e);

        if (e->out && atomic_exchange(&e->fake_stuck, false)) audio_out_pause(e->out); /* stops draining, unannounced */
        if (e->out && (audio_out_disconnected(e->out) || atomic_exchange(&e->fake_disconnect, false))) {
            LOGW("sound device changed: reopening the stream");
            sound_reopen(e);
        }
        if (speed == EMU_FF_MAX) {
            pacer_reset(&pacer); /* no waiting at all */
        } else if (e->out) {
            sound_pace(e);
            pacer_reset(&pacer); /* if sound stops, the clock takes over from now */
        } else {
            clock_wait(&pacer); /* one wait per loop step: N frames per step at N× */
        }
    }
    flush_save(e);
    sound_close(e);
    return NULL;
}

static bool known_save_chip(gba_save_type_t t) {
    return t != GBA_SAVE_NONE && t != GBA_SAVE_UNKNOWN;
}

/* The save file, if there is one, into the core. One that doesn't fit is copied to `<save>.misfit` first:
 * the game starts on a blank chip, and its first saves replace the file and then its `.bak`. */
static void load_save(emu_t *e) {
    size_t size = 0;
    uint8_t *data = e->save_path ? save_file_load(e->save_path, &size) : NULL;
    if (data) {
        e->save_status = machine_save_read(e->m, data, size) == MACHINE_OK ? EMU_SAVE_LOADED : EMU_SAVE_MISFIT;
        char aside[1024];
        if (e->save_status == EMU_SAVE_MISFIT && snprintf(aside, sizeof aside, "%s.misfit", e->save_path) < (int)sizeof aside &&
            !save_file_replace(aside, data, size)) {
            LOGW("cannot keep the save file that doesn't fit as %s", aside);
        }
        free(data);
    } else {
        bool saves = e->g ? known_save_chip(gba_save_type(e->g)) : machine_save_size(e->m) > 0;
        e->save_status = saves ? EMU_SAVE_EMPTY : EMU_SAVE_NONE;
    }
}

emu_options_t emu_options_default(void) {
    emu_options_t o = {NULL, 0, EMU_AUTO, EMU_AUTO, EMU_REWIND_DEFAULT, true, 2, NULL, EMU_AUTO, 0};
    return o;
}

emu_t *emu_create(const uint8_t *rom, size_t size, const char *save_path, const emu_options_t *opt, gba_err_t *err) {
    emu_options_t o = opt ? *opt : emu_options_default();
    emu_t *e = calloc(1, sizeof *e);
    /* Which console: as the caller says, else by the ROM's header; a ROM with no header to go by is taken for a
     * Game Boy Advance one (as before there were two consoles). */
    machine_kind_t kind = o.console == EMU_CONSOLE_GB    ? MACHINE_GB
                          : o.console == EMU_CONSOLE_GBC ? MACHINE_GBC
                          : o.console == EMU_CONSOLE_GBA ? MACHINE_GBA
                                                         : machine_detect(rom, size, NULL);
    if (kind == MACHINE_NONE) kind = MACHINE_GBA;
    if (!e || !(e->m = machine_create(kind))) {
        free(e);
        *err = GBA_ERR_NOMEM;
        return NULL;
    }
    e->g = machine_gba(e->m);
    *err = o.bios && e->g ? gba_load_bios(e->g, o.bios, o.bios_size) : GBA_OK;
    if (*err == GBA_OK) {
        int load = machine_load_rom(e->m, rom, size);
        *err = load == MACHINE_OK            ? GBA_OK
               : load == MACHINE_ERR_UNSUPPORTED ? EMU_ERR_UNSUPPORTED
               : load == MACHINE_ERR_NOMEM       ? GBA_ERR_NOMEM
                                                 : GBA_ERR_ROM_SIZE;
    }
    if (*err != GBA_OK) {
        machine_destroy(e->m);
        free(e);
        return NULL;
    }
    e->pixels = (size_t)machine_width(e->m) * (size_t)machine_height(e->m);
    /* Game settings, before the save file is read (a forced save type blanks the save memory). */
    if (e->g && o.save_type != EMU_AUTO) gba_set_save_type(e->g, (gba_save_type_t)o.save_type);
    if (e->g && o.clock != EMU_AUTO) {
        uint32_t hw = gba_cart_hardware(e->g);
        gba_set_cart_hardware(e->g, o.clock ? hw | GBA_HW_RTC : hw & ~(uint32_t)GBA_HW_RTC);
    }
    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    machine_set_rtc(e->m, (int64_t)now, (int32_t)local.tm_gmtoff);
    tb_init(&e->tb, machine_width(e->m), machine_height(e->m));
    pthread_mutex_init(&e->lock, NULL);
    pthread_condattr_t attr; /* emu_pause's time limit counts on the monotonic clock */
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    pthread_cond_init(&e->wake, &attr);
    pthread_condattr_destroy(&attr);
    pthread_mutex_init(&e->core_lock, NULL);
    key_latch_init(&e->keys);
    atomic_init(&e->frames, 0ull);
    atomic_init(&e->keys_seen, 0u);
    atomic_init(&e->tilt_x, 0);
    atomic_init(&e->tilt_y, 0);
    atomic_init(&e->turn, 0);
    atomic_init(&e->light, 0);
    for (int i = 0; i < 4; i++) atomic_init(&e->seen[i], 0);
    atomic_init(&e->rumble, false);
    atomic_init(&e->sound_wanted, o.sound);
    atomic_init(&e->sound_bursts, o.sound_bursts);
    audio_ring_init(&e->ring);
    resampler_init(&e->rs);
    atomic_init(&e->stats.played, 0);
    atomic_init(&e->stats.silent, 0);
    atomic_init(&e->stats.loud, 0);
    atomic_init(&e->sound_on, false);
    atomic_init(&e->rate, 0);
    atomic_init(&e->burst, 0);
    atomic_init(&e->reopens, 0);
    atomic_init(&e->fake_disconnect, false);
    atomic_init(&e->fake_stuck, false);
    atomic_init(&e->save_failed, false);
    save_timer_init(&e->save_timer);
    e->save_path = save_path ? strdup(save_path) : NULL;
    load_save(e);
    atomic_init(&e->ff_speed, 0);
    atomic_init(&e->rewinding, false);
    atomic_init(&e->rw_snaps, 0);
    atomic_init(&e->rw_bytes, 0);
    atomic_init(&e->rw_steps, 0);
    atomic_init(&e->ra_reset, false);
    atomic_init(&e->look, o.look);
    machine_set_look(e->m, e->look_set = o.look);
    e->ra = o.ra;
    if (o.rewind_budget && machine_rewind_configure(e->m, o.rewind_budget, REWIND_EVERY) != MACHINE_OK) {
        LOGW("no memory for rewind: rewind off");
    }
    return e;
}

void emu_start(emu_t *e) {
    if (e->started) return;
    e->started = pthread_create(&e->thread, NULL, emu_main, e) == 0;
}

static void set_paused(emu_t *e, bool paused) {
    pthread_mutex_lock(&e->lock);
    e->paused = paused;
    pthread_cond_broadcast(&e->wake);
    pthread_mutex_unlock(&e->lock);
}

void emu_pause(emu_t *e) {
    pthread_mutex_lock(&e->lock);
    e->paused = true;
    pthread_cond_broadcast(&e->wake);
    if (e->started) { /* wait until the thread has stopped and written the save: at most 1 s */
        struct timespec until;
        clock_gettime(CLOCK_MONOTONIC, &until);
        until.tv_sec += 1;
        while (!e->parked && !e->quit) {
            if (pthread_cond_timedwait(&e->wake, &e->lock, &until) == ETIMEDOUT) break;
        }
    }
    pthread_mutex_unlock(&e->lock);
}

void emu_resume(emu_t *e) {
    set_paused(e, false);
}

void emu_set_keys(emu_t *e, uint16_t pressed) {
    key_latch_set(&e->keys, pressed);
}

void emu_set_fast_forward(emu_t *e, int speed) {
    atomic_store(&e->ff_speed, speed == EMU_FF_MAX || (speed >= 2 && speed <= 8) ? speed : 0);
}

void emu_set_rewinding(emu_t *e, bool on) {
    atomic_store(&e->rewinding, on);
}

void emu_rewind_stats(emu_t *e, int64_t out[3]) {
    out[0] = atomic_load(&e->rw_snaps);
    out[1] = atomic_load(&e->rw_bytes);
    out[2] = atomic_load(&e->rw_steps);
}

triple_buffer_t *emu_frames(emu_t *e) {
    return &e->tb;
}

uint64_t emu_frame_count(const emu_t *e) {
    return atomic_load(&((emu_t *)e)->frames);
}

void emu_set_audio_enabled(emu_t *e, bool enabled) {
    if (!e->started) atomic_store(&e->sound_wanted, enabled);
}

void emu_set_sound(emu_t *e, bool on, int bursts) {
    atomic_store(&e->sound_bursts, bursts == 4 || bursts == 8 ? bursts : 2);
    atomic_store(&e->sound_wanted, on);
}

static int clamp(int v, int lo, int hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

void emu_set_motion(emu_t *e, int tilt_x, int tilt_y, int turn) {
    atomic_store(&e->tilt_x, clamp(tilt_x, -32767, 32767));
    atomic_store(&e->tilt_y, clamp(tilt_y, -32767, 32767));
    atomic_store(&e->turn, clamp(turn, -32767, 32767));
}

void emu_set_light(emu_t *e, int level) {
    atomic_store(&e->light, clamp(level, 0, 255));
}

void emu_set_look(emu_t *e, int look) {
    atomic_store(&e->look, look);
}

uint32_t emu_cart_hardware(const emu_t *e) {
    if (e->g) return gba_cart_hardware(e->g);
    /* A Game Boy cartridge's extras (17c), named as the GBA's so the app's sensors and rumble start for them too. */
    return (machine_has_rumble(e->m) ? GBA_HW_RUMBLE : 0) | (machine_has_tilt(e->m) ? GBA_HW_TILT : 0);
}

gba_save_type_t emu_save_type(const emu_t *e) {
    return e->g ? gba_save_type(e->g) : GBA_SAVE_NONE;
}

int emu_console(const emu_t *e) {
    return (int)machine_kind(e->m) - 1;
}

void emu_frame_size(const emu_t *e, int *width, int *height) {
    *width = machine_width(e->m);
    *height = machine_height(e->m);
}

bool emu_rumble_active(emu_t *e) {
    return atomic_load(&e->rumble);
}

void emu_motion_seen(emu_t *e, int32_t out[4]) {
    for (int i = 0; i < 4; i++) out[i] = atomic_load(&e->seen[i]);
}

emu_save_status_t emu_save_status(const emu_t *e) {
    return e->save_status;
}

gba_err_t emu_import_save(emu_t *e, const uint8_t *data, size_t size) {
    if (e->started) return GBA_ERR_SAVE_SIZE; /* only before start: the thread owns the core then */
    if (machine_save_read(e->m, data, size) != MACHINE_OK) return GBA_ERR_SAVE_SIZE;
    e->save_status = EMU_SAVE_LOADED;
    write_save(e);
    return GBA_OK;
}

/* The core may be used from another thread only while the emulation thread is parked or not started; core_take holds
 * the core lock (so the paused thread's ra_idle waits) and says whether it is free. Give it back with core_give. */
static bool core_take(emu_t *e) {
    pthread_mutex_lock(&e->core_lock);
    pthread_mutex_lock(&e->lock);
    bool free_now = !e->started || e->parked;
    pthread_mutex_unlock(&e->lock);
    if (!free_now) pthread_mutex_unlock(&e->core_lock);
    return free_now;
}

static void core_give(emu_t *e) {
    pthread_mutex_unlock(&e->core_lock);
}

bool emu_state_save(emu_t *e, const char *path) {
    if (!core_take(e)) return false;
    size_t size = machine_state_size(e->m);
    uint8_t *buf = malloc(size);
    bool ok = buf && machine_state_save(e->m, buf, size) == size && save_file_replace(path, buf, size);
    free(buf);
    core_give(e);
    return ok;
}

gba_err_t emu_state_load(emu_t *e, const char *path) {
    if (hardcore(e) || !core_take(e)) return GBA_ERR_STATE; /* hardcore: no state loads (14) */
    size_t size = 0;
    uint8_t *buf = save_file_load(path, &size);
    gba_err_t err = buf && machine_state_load(e->m, buf, size) == MACHINE_OK ? GBA_OK : GBA_ERR_STATE;
    free(buf);
    if (err == GBA_OK) {
        save_timer_frame(&e->save_timer, true); /* the loaded save chip → the save file */
        atomic_store(&e->ra_reset, true);        /* achievements: the game jumped */
    }
    core_give(e);
    return err;
}

gba_err_t emu_cheats_set(emu_t *e, const char *const *texts, const int *types, size_t count) {
    if (!core_take(e)) return GBA_ERR_CHEAT;
    gba_err_t err;
    if (hardcore(e)) { /* hardcore: no cheats (14) */
        machine_cheats_set(e->m, NULL, NULL, 0);
        err = count ? GBA_ERR_CHEAT : GBA_OK;
    } else {
        err = machine_cheats_set(e->m, texts, types, count) == MACHINE_OK ? GBA_OK : GBA_ERR_CHEAT;
    }
    core_give(e);
    return err;
}

bool emu_frame(emu_t *e, uint16_t out[240 * 160]) {
    if (!core_take(e)) return false;
    memcpy(out, machine_framebuffer(e->m), sizeof(uint16_t) * e->pixels);
    core_give(e);
    return true;
}

bool emu_save_error(emu_t *e) {
    return atomic_exchange(&e->save_failed, false);
}

size_t emu_debug_saveram(emu_t *e, const uint8_t **data) {
    *data = e->g ? gba_saveram(e->g) : NULL;
    return e->g ? gba_saveram_size(e->g) : 0;
}

uint32_t emu_debug_peek32(emu_t *e, uint32_t addr) {
    if (!e->g || !core_take(e)) return 0xDEADDEADu;
    uint32_t v = gba_debug_peek32(e->g, addr);
    core_give(e);
    return v;
}

void emu_ra_idle(emu_t *e) {
    if (!e->ra || !core_take(e)) return;
    ra_memory_t m = memory_of(e);
    ra_idle(e->ra, &m);
    core_give(e);
}

unsigned emu_keys_seen(emu_t *e) {
    return atomic_exchange(&e->keys_seen, 0u);
}

void emu_simulate_audio_disconnect(emu_t *e) {
    atomic_store(&e->fake_disconnect, true);
}

void emu_simulate_stuck_stream(emu_t *e) {
    atomic_store(&e->fake_stuck, true);
}

void emu_audio_stats(emu_t *e, int64_t out[7]) {
    out[0] = atomic_load(&e->sound_on) ? 1 : 0;
    out[1] = atomic_load(&e->rate);
    out[2] = atomic_load(&e->burst);
    out[3] = atomic_load(&e->stats.played);
    out[4] = atomic_load(&e->stats.silent);
    out[5] = atomic_load(&e->stats.loud);
    out[6] = atomic_load(&e->reopens);
}

void emu_destroy(emu_t *e) {
    if (!e) return;
    pthread_mutex_lock(&e->lock);
    e->quit = true;
    pthread_cond_broadcast(&e->wake);
    pthread_mutex_unlock(&e->lock);
    if (e->started) pthread_join(e->thread, NULL);
    machine_destroy(e->m);
    pthread_mutex_destroy(&e->lock);
    pthread_mutex_destroy(&e->core_lock);
    pthread_cond_destroy(&e->wake);
    free(e->save_path);
    free(e);
}
