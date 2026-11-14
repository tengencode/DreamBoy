#ifndef GBA_TOOLS_KEY_SCRIPT_H
#define GBA_TOOLS_KEY_SCRIPT_H

#include <stdbool.h>
#include <stdint.h>

/* Button scripts for the PC tools (testrunner --keys, gbabench's scenes, the game check): "10:DOWN,12:-,14:A+B". */

#define KEY_CW (1u << 14) /* not GBA buttons: the rotation sensor turned while held */
#define KEY_CCW (1u << 15)
#define SPIN_RATE 16000
#define KEY_TILT_LEFT (1u << 10) /* nor these: the cartridge's tilt sensor, tipped that way while held */
#define KEY_TILT_RIGHT (1u << 11)
#define KEY_TILT_UP (1u << 12)
#define KEY_TILT_DOWN (1u << 13)
#define KEY_TILT_ANY (KEY_TILT_LEFT | KEY_TILT_RIGHT | KEY_TILT_UP | KEY_TILT_DOWN)
#define TILT_HELD 24000 /* as the viewer's I / J / K / L keys */

typedef struct {
    long frame;
    uint16_t keys; /* GBA_KEY_* (and KEY_CW / KEY_CCW, KEY_TILT_*) held from this frame on */
} key_event_t;

typedef struct {
    key_event_t *ev; /* grown by key_script_add; free() */
    int count, cap;
} key_script_t;

/* Appends one event; false when out of memory. */
bool key_script_add(key_script_t *ks, long frame, uint16_t keys);
/* "10:DOWN,12:-,14:A+B" → events in frame order; false for a bad entry or frames that do not increase. */
bool key_script_parse(const char *list, key_script_t *ks);
/* The keys held during a frame: those of the last event at or before it. */
uint16_t key_script_at(const key_script_t *ks, long frame);

#endif
