#ifndef GBA_DMA_H
#define GBA_DMA_H

#include <stdbool.h>
#include <stdint.h>

typedef struct gba gba_t;

typedef struct {
    uint32_t src, dst, count; /* internal registers, latched when the channel is enabled */
} dma_channel_t;

typedef struct {
    dma_channel_t ch[4];
    uint32_t latch;  /* last value transferred; returned for sources DMA cannot read */
    uint8_t pending; /* channels waiting for their immediate start */
    bool active;     /* a transfer is running (its own bus accesses must not start another) */
    uint64_t due;    /* when the pending immediate transfers take the bus (UINT64_MAX: none) */
} dma_t;

void dma_reset(gba_t *g);
void dma_write_control(gba_t *g, int n, uint16_t old, uint16_t value); /* DMAnCNT_H was written */
void dma_on_vblank(gba_t *g);
void dma_on_hblank(gba_t *g); /* visible lines only */
void dma_run_immediate(gba_t *g);
/* Called before every CPU bus access: a due immediate DMA takes the bus first. */
static inline bool dma_bus_due(const dma_t *d, uint64_t now) {
    return now >= d->due && !d->active;
}
/* A sound FIFO (0x040000A0 or 0x040000A4) has 16 bytes or fewer: DMA1/DMA2 with special timing and
 * that destination copy 4 words to it. */
void dma_sound_request(gba_t *g, uint32_t fifo_addr);

#endif
