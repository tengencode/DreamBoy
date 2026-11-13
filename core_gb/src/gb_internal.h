#ifndef GB_INTERNAL_H
#define GB_INTERNAL_H

#include <stdbool.h>

#include "gb.h"
#include "gb_debug.h"

/* Every name seen outside its file starts with gb_: both cores are linked into one program. */

typedef enum { MBC_NONE, MBC1, MBC2, MBC3, MBC5 } mbc_t;

/* What the loaded ROM is: set by gb_load_rom, never part of a save state. */
typedef struct {
    uint8_t *rom;      /* a power-of-two copy, padded with 0xFF */
    uint32_t rom_size; /* of the copy */
    uint32_t crc32;    /* of the bytes as given */
    char title[17];
    bool color;
    mbc_t mbc;
    bool battery, clock, rumble;
    uint32_t ram_size; /* 0 or a power of two */
} cart_info_t;

struct gb {
    cart_info_t info;
    uint8_t *cart_ram; /* info.ram_size bytes */
};

gb_err_t gb_cart_load(gb_t *g, const uint8_t *rom, size_t size);
void gb_cart_free(gb_t *g);

#endif
