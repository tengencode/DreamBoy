#ifndef GBAEMU_EMU_THREAD_H
#define GBAEMU_EMU_THREAD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gba.h"
#include "machine.h"
#include "ra.h"
#include "triple_buffer.h"

/* One running game: the core, its frames, its sound, and the thread that runs it — paced by the sound
 * (AAudio), or at 59.7275 frames/s by the clock when there is no sound. Once started, only the
 * emulation thread touches the core and the sound stream. */
typedef struct emu emu_t;

typedef enum {
    EMU_SAVE_NONE,   /* no save chip (or one the game has not shown yet): nothing to offer */
    EMU_SAVE_LOADED, /* the save file was loaded */
    EMU_SAVE_EMPTY,  /* a known save chip and no save file yet: offer an import */
    EMU_SAVE_MISFIT, /* a save file whose size does not fit the chip: not loaded, copied to `<save>.misfit` */
} emu_save_status_t;

/* How a game is set up (emu_create); emu_options_default() = the built-in BIOS, no overrides, rewind as in 5b,
 * sound on with 2 bursts. */
typedef struct {
    const uint8_t *bios;  /* a 16 KiB BIOS file's bytes (copied), or NULL: the built-in BIOS */
    size_t bios_size;
    int save_type;        /* EMU_AUTO or a gba_save_type_t forced before the save file is read */
    int clock;            /* EMU_AUTO, 0 = no clock, 1 = a clock (the cartridge's other extras stay) */
    size_t rewind_budget; /* bytes for rewind snapshots; 0 = rewind off */
    bool sound;           /* false: no stream, the clock paces the game */
    int sound_bursts;     /* AAudio's buffer in bursts: 2, 4 or 8 */
    ra_t *ra; /* RetroAchievements (14): checked after every frame and its hardcore limits obeyed; NULL = none.
                 Dropped for a Game Boy game: no achievements for those yet (17b) */
    int console; /* EMU_AUTO: told from the ROM's own header (a Game Boy Advance when it has none), or
                    EMU_CONSOLE_* when the caller knows better (from the file's name) */
} emu_options_t;
#define EMU_AUTO (-1)
enum { EMU_CONSOLE_GBA = 0, EMU_CONSOLE_GB = 1, EMU_CONSOLE_GBC = 2 };
/* emu_create's errors are gba_err_t numbers, plus this one: a Game Boy cartridge chip the core does not have. */
#define EMU_ERR_UNSUPPORTED ((gba_err_t)100)
#define EMU_REWIND_DEFAULT (64u * 1024 * 1024) /* ~2.5 minutes of snapshots in a large game (design: 5b) */
emu_options_t emu_options_default(void);

/* Creates the core, loads the BIOS (if any) and the ROM (copied), forces the save type and clock (if asked),
 * sets the clock from the phone and loads the save file at save_path if there is one (save_path NULL: no save
 * file); NULL + *err on failure (GBA_ERR_BIOS_SIZE for a BIOS not 16 KiB). From then on the game's saves are
 * written to save_path about 1 s after the game saves, and on pause and stop. opt NULL = the defaults. */
emu_t *emu_create(const uint8_t *rom, size_t size, const char *save_path, const emu_options_t *opt, gba_err_t *err);
void emu_start(emu_t *e);
void emu_pause(emu_t *e); /* returns once the thread has stopped and written the save (at most 1 s) */
void emu_resume(emu_t *e); /* timing starts afresh: no rush of catch-up frames */
void emu_set_keys(emu_t *e, uint16_t pressed); /* GBA_KEY_* mask */

#define EMU_FF_MAX (-1) /* fast-forward with no pacing and no sound */
/* Fast-forward (any thread; from the next loop step): 0 or 1 = normal; 2, 4, 8 = that many frames per
 * normal frame, the sound sped up to match (higher pitch); EMU_FF_MAX = as fast as it goes, silent. */
void emu_set_fast_forward(emu_t *e, int speed);
/* Rewind (any thread): while on, the game steps back one snapshot (4 frames) per 1/60 s, silent, keys
 * ignored; it stays on the oldest snapshot when there is none older. It wins over fast-forward. When it
 * goes off, play goes on from there and the save file is rewritten ~1 s later (a rewind may undo an
 * in-game save). The history is kept from the game's start (or its last loaded state), as long as the options'
 * rewind budget allows (~2.5 minutes in a large game with the default). */
void emu_set_rewinding(emu_t *e, bool on);
triple_buffer_t *emu_frames(emu_t *e);
int emu_console(const emu_t *e); /* EMU_CONSOLE_*: what the loaded game runs as */
void emu_frame_size(const emu_t *e, int *width, int *height); /* 240x160, or 160x144 */
uint64_t emu_frame_count(const emu_t *e);
void emu_destroy(emu_t *e); /* stops the thread (if started), writes the save, frees everything */

/* Cartridge sensors (any thread; applied at the next frame start): tilt and turn ±32767, light 0..255. */
void emu_set_motion(emu_t *e, int tilt_x, int tilt_y, int turn);
void emu_set_light(emu_t *e, int level);
uint32_t emu_cart_hardware(const emu_t *e); /* GBA_HW_* extras of the loaded cartridge */
gba_save_type_t emu_save_type(const emu_t *e); /* the save chip (detected or forced) */
bool emu_rumble_active(emu_t *e);           /* the rumble motor after the last frame */
emu_save_status_t emu_save_status(const emu_t *e);
/* Before emu_start only: puts a save file's bytes in and writes them to the save file at once.
 * GBA_ERR_SAVE_SIZE (nothing changes) if they do not fit the chip. */
gba_err_t emu_import_save(emu_t *e, const uint8_t *data, size_t size);
bool emu_save_error(emu_t *e); /* true once after a failed save-file write */

/* Save states — only while the game is paused (emu_pause has returned) or not started yet; otherwise
 * refused. Saving writes the whole machine to `path` (safely: a temporary file, then swapped in). */
bool emu_state_save(emu_t *e, const char *path);
/* GBA_ERR_STATE: not paused, unreadable, or not a state of this game and build (the game is unchanged).
 * After a load the game's save file is rewritten from the loaded save chip about 1 s after resuming. */
gba_err_t emu_state_load(emu_t *e, const char *path);
/* RetroAchievements while the game waits to start or is paused (any thread): ra_idle with the game's memory, when no
 * one else is using the core. */
void emu_ra_idle(emu_t *e);
/* The switched-on cheats (gba_cheats_set) — only while paused or not started; GBA_ERR_CHEAT when running or when
 * a text does not read (then no cheats are on). */
gba_err_t emu_cheats_set(emu_t *e, const char *const *texts, const gba_cheat_type_t *types, size_t count);
/* The current BGR555 picture, emu_frame_size pixels (for a state's thumbnail); same rule: only while paused. */
bool emu_frame(emu_t *e, uint16_t out[240 * 160]);

/* Sound on/off and its delay (any thread): the emulation thread opens, closes or reopens the stream between
 * frames; without a stream the clock paces the game. */
void emu_set_sound(emu_t *e, bool on, int bursts);

/* ---- test hooks ---- */
void emu_set_audio_enabled(emu_t *e, bool enabled); /* before emu_start: false = no stream, clock pacing */
void emu_simulate_audio_disconnect(emu_t *e);        /* as if the sound device went away */
void emu_simulate_stuck_stream(emu_t *e);            /* the stream stops taking sound, unannounced */
unsigned emu_keys_seen(emu_t *e);
/* A word of the machine's memory, only while paused or not started (else 0xDEADDEAD). */
uint32_t emu_debug_peek32(emu_t *e, uint32_t addr);
void emu_motion_seen(emu_t *e, int32_t out[4]); /* the sensor values last applied: tilt x, tilt y, turn, light */
/* The core's save bytes (read them only while paused or before start); returns their size. */
size_t emu_debug_saveram(emu_t *e, const uint8_t **data); /* buttons the game was given in any frame since the last call */
/* active (1/0), sample rate, frames per burst, frames played, frames silence-filled, loud frames, reopens */
void emu_audio_stats(emu_t *e, int64_t out[7]);
/* Rewind: snapshots kept, their packed bytes, steps back taken since start. */
void emu_rewind_stats(emu_t *e, int64_t out[3]);

#endif
