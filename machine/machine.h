#ifndef MACHINE_H
#define MACHINE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One set of calls over the two cores (core/ = Game Boy Advance, core_gb/ = Game Boy and Game Boy Color), so the
 * tools and the app need to know in one place only that there are two consoles. Both cores make BGR555 pictures,
 * 32,768 Hz stereo sound and 59.7275 frames a second; the picture's size differs. */

typedef enum { MACHINE_NONE = 0, MACHINE_GBA, MACHINE_GB, MACHINE_GBC } machine_kind_t;
typedef struct machine machine_t;
struct gba;
struct gb;

enum { MACHINE_OK = 0, MACHINE_ERR_ROM, MACHINE_ERR_UNSUPPORTED, MACHINE_ERR_NOMEM, MACHINE_ERR_STATE,
       MACHINE_ERR_SAVE };

/* Which console a ROM file is for. By content first: a Game Boy Advance header (its fixed byte and checksum), else
 * a Game Boy header (its checksum, and a size byte that fits the file; bit 7 of byte 0x143 = Game Boy Color).
 * Only when neither fits, by the file name's ending (.gba / .gb / .gbc, any case; name may be NULL). */
machine_kind_t machine_detect(const uint8_t *rom, size_t size, const char *name);

machine_t *machine_create(machine_kind_t kind); /* NULL: MACHINE_NONE or no memory */
void machine_destroy(machine_t *m);
/* MACHINE_GBA, or for the Game Boy core what the loaded ROM runs as (MACHINE_GB before a ROM is loaded). */
machine_kind_t machine_kind(const machine_t *m);
int machine_load_rom(machine_t *m, const uint8_t *rom, size_t size); /* MACHINE_OK or MACHINE_ERR_* */
void machine_reset(machine_t *m);

void machine_run_frame(machine_t *m);
int machine_width(const machine_t *m);  /* 240, or 160 */
int machine_height(const machine_t *m); /* 160, or 144 */
const uint16_t *machine_framebuffer(const machine_t *m);
#define MACHINE_AUDIO_RATE 32768
size_t machine_audio_read(machine_t *m, int16_t *out, size_t max_frames);
void machine_set_keys(machine_t *m, uint16_t pressed); /* GBA_KEY_* bits; a Game Boy has no L and R */

const char *machine_title(const machine_t *m);
uint32_t machine_crc32(const machine_t *m);

/* The save file as it is kept on disk. */
size_t machine_save_size(const machine_t *m); /* 0: nothing to save (yet) */
size_t machine_save_write(const machine_t *m, uint8_t *buf, size_t cap); /* bytes written; 0 if cap is too small */
/* MACHINE_ERR_SAVE: a Game Boy Advance save of the wrong size (nothing changes). A Game Boy takes any length. */
int machine_save_read(machine_t *m, const uint8_t *data, size_t size);
int machine_save_dirty(machine_t *m);
void machine_set_rtc(machine_t *m, int64_t unix_seconds, int32_t utc_offset_s);

/* A rumble cartridge: whether the loaded game has a motor, and whether it is running now. */
int machine_has_rumble(const machine_t *m);
int machine_rumble_active(const machine_t *m);

/* A grey Game Boy game's colours (gb.h: GB_LOOK_AUTO, 0 = grey, …); nothing for the other consoles' games. */
void machine_set_look(machine_t *m, int look);

size_t machine_state_size(const machine_t *m);
size_t machine_state_save(const machine_t *m, void *buf, size_t cap);
int machine_state_load(machine_t *m, const void *buf, size_t len); /* MACHINE_OK or MACHINE_ERR_STATE */
int machine_rewind_configure(machine_t *m, size_t budget_bytes, int interval_frames); /* MACHINE_OK / _NOMEM */
int machine_rewind_step_back(machine_t *m);                                           /* 1, or 0: no snapshot */

/* The core itself, for what only one console has (BIOS, cheats, sensors, RAM views, debug calls); NULL when the
 * machine is the other kind. */
struct gba *machine_gba(machine_t *m);
struct gb *machine_gb(machine_t *m);

#ifdef __cplusplus
}
#endif

#endif
