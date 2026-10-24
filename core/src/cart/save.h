#ifndef GBA_SAVE_H
#define GBA_SAVE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gba.h"

#define SAVE_MAX 0x20000u /* the largest chip: 128 KiB Flash */

typedef struct {
    uint8_t state; /* position in a command sequence (FLASH_* in flash.c) */
    bool id_mode;  /* reads of offsets 0 and 1 return the chip ID */
    uint8_t bank;  /* 64 KiB bank of a 128 KiB chip */
} flash_t;

typedef struct {
    uint8_t phase;     /* EE_* in eeprom.c */
    bool writing;      /* the command being received is a write */
    uint8_t count;     /* bits received in the current phase */
    uint8_t reply_pos; /* bits of a read reply already sent (0..67) */
    uint16_t addr;     /* block address */
    uint64_t data;     /* 8 data bytes, first byte in the top bits */
} eeprom_t;

typedef struct {
    gba_save_type_t type;
    bool dirty;
    flash_t flash;
    eeprom_t eeprom;
    uint8_t data[SAVE_MAX]; /* blank = 0xFF */
} save_t;

/* Blank save of the type the ROM's marker strings name (UNKNOWN when there is none). */
void save_setup(save_t *s, const uint8_t *rom, size_t size);
void save_reset(save_t *s); /* forget half-received commands; keeps type and data */
size_t save_type_size(gba_save_type_t type);
/* The save area 0x0E000000-0x0FFFFFFF, one byte at the exact address. */
uint8_t save_read8(gba_t *g, uint32_t addr);
void save_write8(gba_t *g, uint32_t addr, uint8_t value);

/* flash.c: off is the offset within the 64 KiB window (address & 0xFFFF). */
uint8_t flash_read(const save_t *s, uint32_t off);
void flash_write(save_t *s, uint32_t off, uint8_t value);

/* eeprom.c: one bit per access. */
uint16_t eeprom_read_bit(save_t *s);
void eeprom_write_bit(save_t *s, unsigned bit);
/* save.c: the EEPROM window on 0x0D. */
bool save_eeprom_mapped(const gba_t *g, uint32_t addr);
uint16_t save_eeprom_read(gba_t *g);
void save_eeprom_write(gba_t *g, uint32_t value);
void save_eeprom_dma(gba_t *g, uint32_t dst, uint32_t count); /* a DMA3 to dst of count units starts */

#endif
