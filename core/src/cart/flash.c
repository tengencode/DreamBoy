#include <string.h>

#include "cart/save.h"

/* Where a command sequence has got to: prefix = 0xAA at 0x5555, 0x55 at 0x2AAA, command at 0x5555. */
enum {
    FLASH_READY,
    FLASH_AA,
    FLASH_55,
    FLASH_ERASE,    /* got 0x80: waiting for a second prefix */
    FLASH_ERASE_AA,
    FLASH_ERASE_55, /* next: 0x10 at 0x5555 (chip) or 0x30 at a sector */
    FLASH_WRITE,    /* got 0xA0: the next write stores one byte */
    FLASH_BANK,     /* got 0xB0: the next write to offset 0 picks the bank */
};

static uint32_t bank_base(const save_t *s) {
    return (uint32_t)s->flash.bank * 0x10000u;
}

uint8_t flash_read(const save_t *s, uint32_t off) {
    if (s->flash.id_mode && off < 2) {
        bool big = s->type == GBA_SAVE_FLASH128;
        if (off == 0) return big ? 0x62 : 0x32; /* Sanyo : Panasonic */
        return big ? 0x13 : 0x1B;
    }
    return s->data[bank_base(s) + off];
}

static void command(save_t *s, uint8_t value) {
    flash_t *f = &s->flash;
    switch (value) {
    case 0x90: f->id_mode = true; break;
    case 0xF0: f->id_mode = false; break;
    case 0x80: f->state = FLASH_ERASE; break;
    case 0xA0: f->state = FLASH_WRITE; break;
    case 0xB0:
        if (s->type == GBA_SAVE_FLASH128) f->state = FLASH_BANK;
        break;
    default: break;
    }
}

void flash_write(save_t *s, uint32_t off, uint8_t value) {
    flash_t *f = &s->flash;
    uint8_t state = f->state;
    f->state = FLASH_READY; /* anything unexpected starts over */
    switch (state) {
    case FLASH_READY:
        if (off == 0x5555 && value == 0xAA) f->state = FLASH_AA;
        break;
    case FLASH_AA:
        if (off == 0x2AAA && value == 0x55) f->state = FLASH_55;
        break;
    case FLASH_55:
        if (off == 0x5555) command(s, value);
        break;
    case FLASH_ERASE:
        if (off == 0x5555 && value == 0xAA) f->state = FLASH_ERASE_AA;
        break;
    case FLASH_ERASE_AA:
        if (off == 0x2AAA && value == 0x55) f->state = FLASH_ERASE_55;
        break;
    case FLASH_ERASE_55:
        if (off == 0x5555 && value == 0x10) {
            memset(s->data, 0xFF, save_type_size(s->type));
            s->dirty = true;
        } else if (value == 0x30) {
            memset(s->data + bank_base(s) + (off & 0xF000), 0xFF, 0x1000);
            s->dirty = true;
        }
        break;
    case FLASH_WRITE:
        s->data[bank_base(s) + off] = value;
        s->dirty = true;
        break;
    case FLASH_BANK:
        if (off == 0) f->bank = value & 1;
        break;
    default: break;
    }
}
