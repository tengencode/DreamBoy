#ifndef GBA_CHEATS_H
#define GBA_CHEATS_H

/* Cheat codes: text → a list of steps, run in order at the end of every frame.
 *
 * Formats (as mGBA reads them; our own code — mGBA is MPL-2.0): Action Replay v3 and GameShark v1/v2
 * (8 + 8 hex digits, TEA-scrambled or plain), CodeBreaker (8 + 4 hex digits, plain or scrambled after a
 * 9xxxxxxx line), address:value (VBA style). Master / hook / game-id / slowdown lines are skipped; cheats
 * run once per frame instead of at the hook. The DEADFACE re-keying lines are not supported. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gba.h"

typedef enum { STEP_WRITE, STEP_WRITE_INDIRECT, STEP_ADD, STEP_AND, STEP_OR, STEP_IF } step_kind_t;
typedef enum { IF_EQ, IF_NE, IF_LT, IF_GT, IF_LE, IF_GE, IF_ULT, IF_UGT, IF_AND, IF_NAND, IF_NEVER, IF_BUTTON } if_kind_t;

typedef struct {
    uint8_t kind;  /* step_kind_t */
    uint8_t cond;  /* if_kind_t, for STEP_IF */
    uint8_t width; /* 1, 2 or 4 bytes */
    uint32_t addr, value;
    uint32_t count;      /* writes: how many times (a fill); 1 normally */
    uint32_t addr_step;  /* added to the address after each write (indirect: added to the pointer read) */
    uint32_t value_step; /* added to the value after each write */
    uint32_t then_n;     /* STEP_IF: the steps after it that run when it holds */
    uint32_t else_n;     /* STEP_IF: the steps after those that run when it does not */
} cheat_step_t;

typedef struct {
    uint32_t addr;  /* in the cartridge ROM (0x08000000-based), halfword-aligned */
    uint16_t value; /* the patched halfword */
    uint16_t old;   /* what the ROM held (valid while applied) */
} cheat_patch_t;

typedef struct cheat_list {
    cheat_step_t *steps;
    size_t nsteps, capsteps;
    cheat_patch_t *patches;
    size_t npatches, cappatches;
    bool applied; /* the patches are in the ROM */
} cheat_list_t;

/* Reads one cheat's text into `out` (appending). 0 on success, else the first bad line (1-based, blank
 * lines counted). *used = the type read (Auto → the one detected). */
int cheat_decode(const char *text, gba_cheat_type_t type, cheat_list_t *out, gba_cheat_type_t *used);

void cheat_list_free(cheat_list_t *l);
bool cheat_push_step(cheat_list_t *l, cheat_step_t s);
bool cheat_push_patch(cheat_list_t *l, uint32_t addr, uint16_t value);

/* End of every frame (nothing without cheats). */
void cheats_apply(gba_t *g);
/* Undo the ROM patches and free the list (before a new ROM, at the end). */
void cheats_drop(gba_t *g);

/* ---- the ciphers (decrypt.c) ---- */
extern const uint32_t CHEAT_GS_SEEDS[4];  /* GameShark v1/v2 */
extern const uint32_t CHEAT_AR3_SEEDS[4]; /* Action Replay v3 */
void cheat_tea_decrypt(uint32_t *op1, uint32_t *op2, const uint32_t seeds[4]);
void cheat_tea_encrypt(uint32_t *op1, uint32_t *op2, const uint32_t seeds[4]); /* for tests */

typedef struct {
    uint32_t master; /* the 9xxxxxxx line's first word; 0 = codes are plain */
    uint32_t seeds[4];
    uint8_t table[0x30];
    uint32_t rng;
} cb_crypt_t;
void cheat_cb_reseed(cb_crypt_t *c, uint32_t op1, uint16_t op2); /* after a 9xxxxxxx yyyy line */
void cheat_cb_decrypt(const cb_crypt_t *c, uint32_t *op1, uint16_t *op2);
void cheat_cb_encrypt(const cb_crypt_t *c, uint32_t *op1, uint16_t *op2); /* for tests */

#endif
