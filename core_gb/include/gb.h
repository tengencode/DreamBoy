#ifndef GB_H
#define GB_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The Game Boy / Game Boy Color core. Shaped like gba.h so both sit behind one layer (machine/). */
typedef struct gb gb_t;

typedef enum {
    GB_OK = 0,
    GB_ERR_ROM_SIZE,         /* not 0x150 bytes .. 8 MiB */
    GB_ERR_UNSUPPORTED_CART, /* a cartridge chip this core does not have */
    GB_ERR_NOMEM,
    GB_ERR_STATE,            /* a save state of another game, another format or version, or damaged */
    GB_ERR_CHEAT,            /* a cheat text that does not read, or too many codes */
} gb_err_t;

enum { /* the same bits as GBA_KEY_* */
    GB_KEY_A = 1 << 0,
    GB_KEY_B = 1 << 1,
    GB_KEY_SELECT = 1 << 2,
    GB_KEY_START = 1 << 3,
    GB_KEY_RIGHT = 1 << 4,
    GB_KEY_LEFT = 1 << 5,
    GB_KEY_UP = 1 << 6,
    GB_KEY_DOWN = 1 << 7,
};

#define GB_WIDTH 160
#define GB_HEIGHT 144
#define GB_AUDIO_RATE 32768  /* stereo samples per second: the 4,194,304 Hz clock / 128 */
#define GB_FRAME_TICKS 70224 /* clock ticks in one frame (59.7275 Hz, the GBA's rate) */

gb_t *gb_create(void);
void gb_destroy(gb_t *g);
/* Copies the ROM and resets the machine. Which console it runs as comes from the header (gb_is_color). */
gb_err_t gb_load_rom(gb_t *g, const uint8_t *rom, size_t size);
void gb_reset(gb_t *g);
int gb_is_color(const gb_t *g); /* 1: runs as a Game Boy Color (header byte 0x143, bit 7) */

void gb_run_frame(gb_t *g);                    /* exactly GB_FRAME_TICKS, also while the screen is off */
void gb_set_keys(gb_t *g, uint8_t pressed);    /* GB_KEY_* mask of held buttons */
const uint16_t *gb_framebuffer(const gb_t *g); /* 160x144, BGR555 as the GBA core's, row-major */
/* Up to max_frames stereo samples (int16 left, right) made since the last call; returns how many. */
size_t gb_audio_read(gb_t *g, int16_t *out, size_t max_frames);

const char *gb_rom_title(const gb_t *g); /* the header's title, printable ASCII only */
uint32_t gb_rom_crc32(const gb_t *g);    /* CRC-32 (as zlib) of the bytes given to gb_load_rom */
#define GB_WRAM_SIZE 0x8000u
const uint8_t *gb_wram(const gb_t *g); /* work RAM, 8 banks of 0x1000 (a plain Game Boy uses the first two) */

/* Looks for grey games: the colours a plain Game Boy's four shades are shown in — the background and the two sprite
 * palettes each have their own four. Looks 0 .. gb_look_count()-1 are fixed: 0 the four greys (the default), 1 the
 * first Game Boy's green, 2 the Pocket's, then the Game Boy Color's own eleven. GB_LOOK_AUTO is what a Game Boy
 * Color picks for the loaded game from its header (grey for a game it does not know). A look is how the picture is
 * shown, from the next drawn line on: it is in no save state, and Game Boy Color games ignore it. */
#define GB_LOOK_AUTO (-1)
#define GB_LOOK_GREY 0
int gb_look_count(void);
/* The look's colours (BGR555): background 0-3, sprites 0's 0-3, sprites 1's 0-3. A number that is no look: grey. */
void gb_look_colours(int look, uint16_t out[12]);
/* The look GB_LOOK_AUTO means for this ROM: GB_LOOK_GREY, or a number at or above gb_look_count() (one of the
 * console's combinations; good for gb_look_colours and gb_set_look). */
int gb_look_auto(const uint8_t *rom, size_t size);
void gb_set_look(gb_t *g, int look); /* kept across gb_reset and gb_load_rom */

/* Cheats: Game Genie (ABC-DEF or ABC-DEF-GHI, dashes optional: a ROM byte reads as another) and GameShark
 * (TTVVLLHH: a RAM byte is written every frame). A cheat is its codes as text, one per line or separated by '+'
 * or spaces (blank lines allowed); Auto reads each code as what it is. */
typedef enum { GB_CHEAT_AUTO, GB_CHEAT_GAME_GENIE, GB_CHEAT_GAMESHARK } gb_cheat_type_t;
/* Reads a cheat without a machine: the type it is read as with *bad_line = 0, or *bad_line = the first line that
 * is not a code of that type (1-based, blank lines counted; 1 for a text without any code). */
gb_cheat_type_t gb_cheat_check(const char *text, gb_cheat_type_t type, int *bad_line);
/* The switched-on cheats, replacing the previous ones (whose ROM patches are undone first). Call between frames.
 * GameShark codes run at the end of every gb_run_frame; save states and rewind never contain cheats, and loading
 * another ROM drops them. GB_ERR_CHEAT (a text that does not read, more than 256 codes) leaves none on. */
gb_err_t gb_cheats_set(gb_t *g, const char *const *texts, const gb_cheat_type_t *types, size_t count);

/* Rumble cartridges (MBC5 with a motor). */
int gb_has_rumble(const gb_t *g);    /* 1: the loaded cartridge has a motor */
int gb_rumble_active(const gb_t *g); /* 1 while the game holds the motor on */

/* The save file: the cartridge RAM, then for a clock cartridge 48 bytes (the clock registers and a Unix time),
 * the format other emulators use. */
size_t gb_save_size(const gb_t *g); /* 0: this cartridge keeps no save */
size_t gb_save_write(const gb_t *g, uint8_t *buf, size_t cap); /* bytes written; 0 if cap is too small */
/* Puts a save file back (after gb_load_rom). Any length is taken: what fits is copied, the rest is 0xFF. */
void gb_save_read(gb_t *g, const uint8_t *data, size_t size);
int gb_save_dirty(gb_t *g); /* 1 if the game wrote its cartridge RAM since the last call (then clears it) */
/* "It is now this time": the cartridge clock moves on by the seconds since the last call (or since the save's
 * time stamp), unless the game stopped it. Call after loading and about once a second. */
void gb_set_rtc(gb_t *g, int64_t unix_seconds);

/* Save states and rewind: as gba.h. A state loads only into the same ROM; anything else is GB_ERR_STATE and
 * changes nothing. */
size_t gb_state_size(const gb_t *g);
size_t gb_state_save(const gb_t *g, void *buf, size_t cap);
gb_err_t gb_state_load(gb_t *g, const void *buf, size_t len);
gb_err_t gb_rewind_configure(gb_t *g, size_t budget_bytes, int interval_frames);
int gb_rewind_step_back(gb_t *g);

#ifdef __cplusplus
}
#endif

#endif
