#ifndef GBA_NOTES_H
#define GBA_NOTES_H

/* The warning log (milestone 13): what a game uses that the emulator does not do, so a game check finds gaps
 * the picture does not show. Kept only with GBA_NOTES (tools/dev.sh's build/host and build/check); everywhere
 * else the NOTE... macros are empty and cost nothing. Writes made while the CPU is in the BIOS are not noted. */
#include <stdint.h>

typedef struct gba gba_t;

typedef enum {
    NOTE_BIOS_CALL,       /* the built-in BIOS skipped a call; detail: its number */
    NOTE_MIDLINE_IO,      /* a picture register written while a visible line is drawn; detail: its offset */
    NOTE_MIDLINE_PALETTE, /* the palette written while a visible line is drawn */
    NOTE_GREEN_SWAP,      /* green swap switched on (not drawn) */
    NOTE_VIDEO_CAPTURE,   /* a DMA3 video-capture transfer switched on (never runs) */
    NOTE_IO_UNUSED,       /* a write to an I/O address with no register; detail: the first such offset */
    NOTE_EXEC_EMPTY,      /* code fetched from empty memory; detail: the first such address */
    NOTE_UNDEFINED,       /* an undefined instruction; detail: the first one's address */
    NOTE_DROPPED,         /* the log was full: how many notes were left out */
    NOTE_KIND_COUNT
} note_kind_t;

#define NOTES_MAX 64 /* = GBA_DEBUG_NOTES_MAX */

typedef struct {
    uint8_t kind; /* note_kind_t */
    uint32_t detail, first_frame, count;
} note_t;

/* The game's own messages through mGBA's debug-print registers (0x04FFF600 text, 0x04FFF700 level | 0x100 to
 * send): decomp hacks and homebrew report failed checks this way. Real hardware ignores these addresses. */
#define NOTES_PRINTS 16
#define NOTES_PRINT_LEN 128
typedef struct {
    uint32_t level, first_frame, count; /* level: 0 fatal, 1 error, 2 warning, 3 info, 4 debug */
    char text[NOTES_PRINT_LEN];
} note_print_t;

typedef struct {
    note_t list[NOTES_MAX];
    uint32_t count; /* entries used */
    uint32_t frame; /* frames finished since the last reset */
    note_print_t prints[NOTES_PRINTS];
    uint32_t print_count;
    char print_area[257]; /* 0x04FFF600-0x04FFF6FF as written so far, always NUL-terminated */
} notes_t;

#ifdef GBA_NOTES
void notes_add(gba_t *g, note_kind_t kind, uint32_t detail);
void notes_drawing(gba_t *g, note_kind_t kind, uint32_t detail); /* noted only while a visible line is drawn */
void notes_io_write(gba_t *g, uint32_t off, uint32_t value, unsigned size); /* unused registers; debug prints */
void notes_fetch(gba_t *g, uint32_t addr);                       /* noted when addr is empty memory */
void notes_clear(gba_t *g);
#define NOTE(g, kind, detail) notes_add((g), (kind), (detail))
#define NOTE_DRAWING(g, kind, detail) notes_drawing((g), (kind), (detail))
#define NOTE_IO_WRITE(g, off, value, size) notes_io_write((g), (off), (value), (size))
#define NOTE_FETCH(g, addr) notes_fetch((g), (addr))
#define NOTES_CLEAR(g) notes_clear(g)
#define NOTES_FRAME(g) ((g)->notes.frame++)
#else
#define NOTE(g, kind, detail) ((void)0)
#define NOTE_DRAWING(g, kind, detail) ((void)0)
#define NOTE_IO_WRITE(g, off, value, size) ((void)0)
#define NOTE_FETCH(g, addr) ((void)0)
#define NOTES_CLEAR(g) ((void)0)
#define NOTES_FRAME(g) ((void)0)
#endif

#endif
