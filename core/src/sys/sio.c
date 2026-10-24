#include "sys/sio.h"

#include <string.h>

#include "gba_internal.h"

/* Rules checked against all 90 rows of the mGBA suite's "SIO register R/W" group (hardware values). */

enum { MODE_NORMAL8, MODE_NORMAL32, MODE_MULTI, MODE_UART, MODE_GPIO, MODE_JOY };

static int mode(const sio_t *s) {
    if (s->rcnt & 0x8000) return (s->rcnt & 0x4000) ? MODE_JOY : MODE_GPIO;
    return (s->siocnt >> 12) & 3; /* normal 8, normal 32, multiplayer, UART */
}

void sio_reset(gba_t *g) {
    memset(&g->sio, 0, sizeof g->sio);
}

uint16_t sio_read16(gba_t *g, uint32_t off) {
    const sio_t *s = &g->sio;
    int m = mode(s);
    switch (off) {
    case 0x120:
    case 0x122:
        if (m == MODE_NORMAL32) return (uint16_t)(s->data32 >> ((off - 0x120) * 8));
        if (m == MODE_MULTI) return s->multi[(off - 0x120) >> 1];
        return 0;
    case 0x124:
    case 0x126: return m == MODE_MULTI ? s->multi[(off - 0x120) >> 1] : 0;
    case 0x128:
        if (m == MODE_MULTI) return (uint16_t)((s->siocnt & 0x7F83) | 0x000C); /* SI, SD high: a child, "ready" */
        if (m == MODE_UART) return (uint16_t)((s->siocnt & 0x7F8F) | 0x0020);  /* receive FIFO empty */
        return (uint16_t)((s->siocnt & 0x7F8B) | 0x0004);                      /* SI high */
    case 0x12A: return m == MODE_UART ? 0 : s->data8;
    case 0x134: {
        uint16_t pins = m == MODE_GPIO ? (s->rcnt & 0xF) : m == MODE_JOY ? 0xC : m <= MODE_NORMAL32 ? 0x5 : 0xF;
        return (uint16_t)((s->rcnt & 0xC1F0) | pins);
    }
    case 0x140: return s->joycnt & 0x47;
    case 0x158: return s->joystat & 0x3A;
    default: return 0; /* JOY_RECV (nothing arrives), JOY_TRANS (reads 0), unused */
    }
}

static void write_siocnt(gba_t *g, uint16_t value) {
    sio_t *s = &g->sio;
    bool was_busy = (s->siocnt & 0x80) != 0;
    s->siocnt = value & 0x7FFF;
    int m = mode(s);
    if (!(value & 0x80) || (m != MODE_NORMAL8 && m != MODE_NORMAL32)) {
        sched_cancel(&g->sched, EV_SIO); /* stopped, or a mode where nothing ever finishes */
        return;
    }
    if (was_busy || !(value & 1)) return; /* already running, or an external clock nobody drives */
    uint32_t bits = m == MODE_NORMAL32 ? 32 : 8;
    uint32_t cycles_per_bit = (value & 2) ? 8 : 64; /* 2 MHz or 256 kHz */
    sched_add(&g->sched, EV_SIO, g->now + bits * cycles_per_bit);
}

void sio_write16(gba_t *g, uint32_t off, uint16_t value) {
    sio_t *s = &g->sio;
    switch (off) {
    case 0x120:
    case 0x122:
        if (mode(s) == MODE_NORMAL32) {
            uint32_t shift = (off - 0x120) * 8;
            s->data32 = (s->data32 & ~(0xFFFFu << shift)) | (uint32_t)value << shift;
        }
        break;
    case 0x128: write_siocnt(g, value); break;
    case 0x12A: s->data8 = value; break;
    case 0x134: s->rcnt = value & 0xC1FF; break;
    case 0x140: s->joycnt = (uint16_t)((s->joycnt & ~(value & 7) & ~0x40) | (value & 0x40)); break;
    case 0x158: s->joystat = (uint16_t)((s->joystat & ~0x30) | (value & 0x30)); break;
    default: break;
    }
}

/* A normal-mode transfer with the internal clock has finished: the input line stayed high. */
void sio_on_transfer_done(gba_t *g) {
    sio_t *s = &g->sio;
    if (mode(s) == MODE_NORMAL32) s->data32 = 0xFFFFFFFF;
    else s->data8 = (uint16_t)(s->data8 | 0x00FF);
    s->siocnt &= (uint16_t)~0x80;
    if (s->siocnt & 0x4000) irq_raise(g, IRQ_SERIAL);
}
