#ifndef GBA_SIO_H
#define GBA_SIO_H

#include <stdint.h>

typedef struct gba gba_t;

/* The link port with no cable attached. */
typedef struct {
    uint16_t siocnt, rcnt;  /* as written: SIOCNT & 0x7FFF, RCNT & 0xC1FF */
    uint32_t data32;        /* SIODATA32 (normal 32-bit mode) */
    uint16_t multi[4];      /* SIOMULTI0-3 (multiplayer; nothing ever arrives) */
    uint16_t data8;         /* SIODATA8 / SIOMLT_SEND */
    uint16_t joycnt, joystat;
} sio_t;

void sio_reset(gba_t *g);
uint16_t sio_read16(gba_t *g, uint32_t off); /* I/O offsets 0x120-0x12E and 0x134-0x15E */
void sio_write16(gba_t *g, uint32_t off, uint16_t value);
void sio_on_transfer_done(gba_t *g); /* EV_SIO */

#endif
