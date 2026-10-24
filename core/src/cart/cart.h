#ifndef GBA_CART_H
#define GBA_CART_H

#include <stddef.h>
#include <stdint.h>

#include "gba.h"
#include "cart/save.h"
#include "cart/hardware.h"

#define CART_MAX_ROM (32u * 1024 * 1024)
#define CART_HEADER_SIZE 0xC0u

typedef struct {
    uint8_t *rom;      /* rom_size bytes, zero-padded to a multiple of 4 */
    uint32_t rom_size;
    uint32_t crc32;    /* of the rom_size bytes as given (not the padding) */
    char title[13];    /* NUL-terminated header title (offset 0xA0) */
    char code[5];      /* NUL-terminated game code (offset 0xAC) */
    save_t save;
    hardware_t hw;
} cart_t;

gba_err_t cart_load(cart_t *c, const uint8_t *data, size_t size);
void cart_unload(cart_t *c);
/* addr: 0x08000000-0x0DFFFFFF aligned to size; size is 1, 2 or 4. */
uint32_t cart_rom_read(const cart_t *c, uint32_t addr, unsigned size);

#endif
