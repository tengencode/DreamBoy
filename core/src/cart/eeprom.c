#include "cart/save.h"

/* Command: 1 1 (read) or 1 0 (write), the block address, 64 data bits (writes only), a stop bit. */
enum { EE_IDLE, EE_CMD, EE_ADDR, EE_DATA, EE_STOP, EE_REPLY };

static unsigned addr_bits(const save_t *s) {
    return s->type == GBA_SAVE_EEPROM512 ? 6 : 14;
}

static uint8_t *block(save_t *s) {
    uint32_t mask = s->type == GBA_SAVE_EEPROM512 ? 0x3Fu : 0x3FFu; /* 64 or 1024 blocks of 8 bytes */
    return s->data + (s->eeprom.addr & mask) * 8u;
}

uint16_t eeprom_read_bit(save_t *s) {
    eeprom_t *e = &s->eeprom;
    if (e->phase != EE_REPLY) return 1; /* ready: writes finish at once */
    uint16_t bit = e->reply_pos < 4 ? 0 : (uint16_t)((e->data >> (67 - e->reply_pos)) & 1);
    if (++e->reply_pos == 68) e->phase = EE_IDLE;
    return bit;
}

void eeprom_write_bit(save_t *s, unsigned bit) {
    eeprom_t *e = &s->eeprom;
    if (s->type == GBA_SAVE_EEPROM) s->type = GBA_SAVE_EEPROM8K; /* no DMA told us the size */
    bit &= 1;
    switch (e->phase) {
    case EE_IDLE:
    case EE_REPLY: /* a new command abandons an unread reply */
        e->phase = bit ? EE_CMD : EE_IDLE;
        break;
    case EE_CMD:
        e->writing = bit == 0;
        e->phase = EE_ADDR;
        e->count = 0;
        e->addr = 0;
        break;
    case EE_ADDR:
        e->addr = (uint16_t)(e->addr << 1 | bit);
        if (++e->count == addr_bits(s)) {
            e->phase = e->writing ? EE_DATA : EE_STOP;
            e->count = 0;
            e->data = 0;
        }
        break;
    case EE_DATA:
        e->data = e->data << 1 | bit;
        if (++e->count == 64) e->phase = EE_STOP;
        break;
    case EE_STOP: {
        uint8_t *b = block(s);
        if (e->writing) {
            for (int i = 0; i < 8; i++) b[i] = (uint8_t)(e->data >> (56 - 8 * i));
            s->dirty = true;
            e->phase = EE_IDLE;
        } else {
            e->data = 0;
            for (int i = 0; i < 8; i++) e->data = e->data << 8 | b[i];
            e->reply_pos = 0;
            e->phase = EE_REPLY;
        }
        break;
    }
    default: e->phase = EE_IDLE; break;
    }
}
