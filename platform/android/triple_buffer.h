#ifndef GBAEMU_TRIPLE_BUFFER_H
#define GBAEMU_TRIPLE_BUFFER_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#define TB_WIDTH 240
#define TB_HEIGHT 160
#define TB_PIXELS (TB_WIDTH * TB_HEIGHT)

/* Three frame slots shared by one producer (the emulation thread) and one consumer (the render
 * thread) without locks: the producer fills its back slot and swaps it into the middle; the
 * consumer swaps the middle out when it holds a frame it has not taken yet. */
typedef struct {
    uint16_t slot[3][TB_PIXELS];
    atomic_uint middle; /* slot index, | TB_FRESH when it holds a frame the consumer has not taken */
    unsigned back;      /* producer's slot */
    unsigned front;     /* consumer's slot */
} triple_buffer_t;

#define TB_FRESH 4u

void tb_init(triple_buffer_t *tb);
uint16_t *tb_back(triple_buffer_t *tb); /* producer: the slot to fill */
void tb_publish(triple_buffer_t *tb);   /* producer: hand the filled slot over as the newest frame */
/* Consumer: the newest complete frame; *fresh tells whether it is new since the last call. */
const uint16_t *tb_take(triple_buffer_t *tb, bool *fresh);

#endif
