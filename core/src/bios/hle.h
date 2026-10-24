#ifndef GBA_HLE_H
#define GBA_HLE_H

#include <stdint.h>

typedef struct gba gba_t;

/* Reserved undefined instruction in the BIOS stub meaning "run SWI number r12 in C". */
#define HLE_TRAP_OPCODE 0xE7F000F0u

/* Install the BIOS stub and set the state the real BIOS leaves when it jumps to the cartridge. */
void hle_boot(gba_t *g);
/* Run BIOS function `number` on the current registers. */
void hle_swi(gba_t *g, uint32_t number);

#endif
