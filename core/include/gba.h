#ifndef GBA_H
#define GBA_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gba gba_t;

typedef struct {
    uint32_t flags; /* reserved; pass 0 */
} gba_config_t;

typedef enum {
    GBA_OK = 0,
    GBA_ERR_ROM_SIZE,
    GBA_ERR_BIOS_SIZE,
    GBA_ERR_NOMEM,
    GBA_ERR_SAVE_SIZE, /* a save file whose size does not fit the game's save chip */
    GBA_ERR_STATE,     /* a save state of another game, another format or version, or damaged */
    GBA_ERR_CHEAT,     /* a cheat's text that is not a code this core can read */
} gba_err_t;

enum {
    GBA_KEY_A = 1 << 0,
    GBA_KEY_B = 1 << 1,
    GBA_KEY_SELECT = 1 << 2,
    GBA_KEY_START = 1 << 3,
    GBA_KEY_RIGHT = 1 << 4,
    GBA_KEY_LEFT = 1 << 5,
    GBA_KEY_UP = 1 << 6,
    GBA_KEY_DOWN = 1 << 7,
    GBA_KEY_R = 1 << 8,
    GBA_KEY_L = 1 << 9,
};

gba_t *gba_create(const gba_config_t *cfg); /* cfg may be NULL */
void gba_destroy(gba_t *g);

/* Optional 16 KiB BIOS dump. Call before gba_load_rom; without it the built-in HLE BIOS is used. */
gba_err_t gba_load_bios(gba_t *g, const uint8_t *bios, size_t size);
/* Copies the ROM and resets the system. */
gba_err_t gba_load_rom(gba_t *g, const uint8_t *rom, size_t size);
void gba_reset(gba_t *g);

void gba_run_frame(gba_t *g);                    /* runs until the next VBlank starts */
void gba_set_keys(gba_t *g, uint16_t pressed);   /* GBA_KEY_* mask of held buttons */
/* The 240x160 picture in BGR555 (bit 0-4 red, 5-9 green, 10-14 blue), row-major. Lines are
 * drawn as the frame runs, so read it after gba_run_frame returns. */
const uint16_t *gba_framebuffer(const gba_t *g);
#define GBA_AUDIO_RATE 32768 /* stereo samples per second from gba_audio_read */
/* Copies up to max_frames stereo samples (interleaved int16 left, right) made since the last call
 * and returns how many. The core keeps about 3.7 frames of sound; anything older is dropped. */
size_t gba_audio_read(gba_t *g, int16_t *out, size_t max_frames);

const char *gba_rom_title(const gba_t *g);
const char *gba_rom_code(const gba_t *g);
uint32_t gba_rom_crc32(const gba_t *g); /* CRC-32 (as zlib) of the ROM bytes given to gba_load_rom */
/* The game's work RAM, read straight (no cycles, no side effects), for tools that watch it (RetroAchievements,
 * milestone 14). Valid while the machine lives; it changes as the game runs, so read it between gba_run_frame calls. */
#define GBA_IWRAM_SIZE 0x8000u  /* internal work RAM, at 0x03000000 */
#define GBA_EWRAM_SIZE 0x40000u /* external work RAM, at 0x02000000 */
const uint8_t *gba_iwram(const gba_t *g);
const uint8_t *gba_ewram(const gba_t *g);

/* Save states: the whole machine between two gba_run_frame calls (save RAM, clock and sensors
 * included). A state loads only into the same ROM and only a state of this build's format; anything
 * else is refused with GBA_ERR_STATE and changes nothing. */
size_t gba_state_size(const gba_t *g);
size_t gba_state_save(const gba_t *g, void *buf, size_t cap); /* bytes written; 0 if cap is too small */
gba_err_t gba_state_load(gba_t *g, const void *buf, size_t len); /* also forgets the rewind history */

/* Rewind: with a budget, gba_run_frame keeps a snapshot every interval_frames frames, each packed as its
 * difference to the next; the oldest are dropped when the budget is full. This reserves all the memory
 * (GBA_ERR_NOMEM: rewind stays off); a budget of 0 turns rewind off. The history is forgotten by
 * gba_reset, gba_load_rom, gba_state_load and configuring again. */
gba_err_t gba_rewind_configure(gba_t *g, size_t budget_bytes, int interval_frames);
/* The machine back to the previous snapshot (the newest one first, if frames ran since it): 1, or 0
 * with the machine unchanged when there is none. The picture is the snapshot's at once. */
int gba_rewind_step_back(gba_t *g);

/* Cheats: Action Replay v3, GameShark v1/v2 (8 + 8 hex digits, scrambled or plain), CodeBreaker (8 + 4, plain or
 * scrambled), address:value. A cheat is its codes as text, one per line (spaces and blank lines allowed). */
typedef enum {
    GBA_CHEAT_AUTO, /* detected from the first code */
    GBA_CHEAT_ACTION_REPLAY,
    GBA_CHEAT_GAMESHARK,
    GBA_CHEAT_CODEBREAKER,
    GBA_CHEAT_RAW, /* AAAAAAAA:VV, :VVVV or :VVVVVVVV */
} gba_cheat_type_t;
/* Reads a cheat without a machine: the type it is read as (Auto → the detected one) with *bad_line = 0, or
 * *bad_line = the first line that is not a code (1-based, blank lines counted). */
gba_cheat_type_t gba_cheat_check(const char *text, gba_cheat_type_t type, int *bad_line);
/* The switched-on cheats, replacing the previous ones (whose ROM patches are undone first). Call between
 * frames. They run at the end of every gba_run_frame; save states and rewind never contain them.
 * GBA_ERR_CHEAT (a text that does not read) leaves no cheats on. count 0 = none. */
gba_err_t gba_cheats_set(gba_t *g, const char *const *texts, const gba_cheat_type_t *types, size_t count);

typedef enum {
    GBA_SAVE_NONE,      /* no save chip */
    GBA_SAVE_UNKNOWN,   /* no marker string: decided on first use */
    GBA_SAVE_SRAM,      /* 32 KiB */
    GBA_SAVE_FLASH64,   /* 64 KiB */
    GBA_SAVE_FLASH128,  /* 128 KiB, two banks */
    GBA_SAVE_EEPROM,    /* EEPROM, size not known yet */
    GBA_SAVE_EEPROM512, /* 512 B */
    GBA_SAVE_EEPROM8K,  /* 8 KiB */
} gba_save_type_t;

gba_save_type_t gba_save_type(const gba_t *g);
uint8_t *gba_saveram(gba_t *g);          /* the save bytes, valid for gba_saveram_size bytes */
size_t gba_saveram_size(const gba_t *g); /* 0 while there is no chip or its size is unknown */
/* Puts a save file back (after gba_load_rom). The size must fit the chip; for an unknown type or
 * an EEPROM of unknown size, the size picks the chip. GBA_ERR_SAVE_SIZE changes nothing. */
gba_err_t gba_load_saveram(gba_t *g, const uint8_t *data, size_t size);
int gba_saveram_dirty(gba_t *g); /* 1 if the game saved since the last call (then clears it) */
void gba_set_save_type(gba_t *g, gba_save_type_t type); /* manual override; blanks the save */

enum {
    GBA_HW_RTC = 1 << 0,    /* real-time clock */
    GBA_HW_SOLAR = 1 << 1,  /* solar (light) sensor */
    GBA_HW_GYRO = 1 << 2,   /* rotation sensor */
    GBA_HW_RUMBLE = 1 << 3, /* rumble motor */
    GBA_HW_TILT = 1 << 4,   /* two-axis tilt sensor */
};

uint32_t gba_cart_hardware(const gba_t *g);           /* GBA_HW_* extras of the loaded cartridge */
void gba_set_cart_hardware(gba_t *g, uint32_t flags); /* manual override (resets the extras' state) */
/* The clock shows this time now and keeps running with emulated time (call after gba_load_rom;
 * without it the clock starts at 2000-01-01 00:00:00 when the ROM is loaded). */
void gba_set_rtc(gba_t *g, int64_t unix_seconds, int32_t utc_offset_s);
void gba_set_light(gba_t *g, uint8_t level);       /* solar sensor: 0 = dark .. 255 = full sun */
void gba_set_gyro(gba_t *g, int16_t z_rate);       /* rotation speed: > 0 clockwise, ±32767 = very fast */
void gba_set_tilt(gba_t *g, int16_t x, int16_t y); /* ±32767 = fully tilted, 0 = level */
int gba_rumble_active(const gba_t *g);             /* 1 while the rumble motor runs */

#ifdef __cplusplus
}
#endif

#endif
