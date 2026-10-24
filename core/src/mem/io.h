#ifndef GBA_IO_H
#define GBA_IO_H

#include <stdint.h>

typedef struct gba gba_t;

#define IO_SIZE 0x400u

typedef struct {
    uint8_t raw[IO_SIZE]; /* backing store for registers without dedicated handling */
    uint8_t postflg;
} io_t;

/* How the CPU sees an I/O halfword when it reads it. */
typedef enum { IO_OPEN = 0, IO_READ, IO_ZERO } io_kind_t;

typedef struct {
    uint8_t kind;  /* io_kind_t */
    uint16_t mask; /* IO_READ: the readable bits */
} io_rule_t;

void io_init_tables(void);       /* builds the read-rule table; call once before any system runs */
io_rule_t io_rule(uint32_t off); /* rule for the halfword at I/O offset off (0x000-0x3FE) */

void io_reset(gba_t *g);
/* addr in 0x04000000-0x04FFFFFF, aligned to size; size is 1, 2 or 4. */
uint32_t io_read(gba_t *g, uint32_t addr, unsigned size);
void io_write(gba_t *g, uint32_t addr, uint32_t value, unsigned size);

#endif
