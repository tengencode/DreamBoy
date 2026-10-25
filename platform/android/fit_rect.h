#ifndef GBAEMU_FIT_RECT_H
#define GBAEMU_FIT_RECT_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int x, y, w, h;
} fit_rect_t;

/* The largest 3:2 rectangle inside a width x height surface, centred, in whole pixels
 * (x and y are the offsets of the free space on each side; all zero for an empty surface). */
fit_rect_t fit_rect(int width, int height);

/* How the picture fills the surface (Settings → Picture → Size). */
typedef enum {
    FIT_ASPECT = 0,  /* fit_rect: the largest 3:2 rectangle */
    FIT_INTEGER = 1, /* the largest whole multiple of 240x160 that fits, centred (FIT_ASPECT if not even 1x fits) */
    FIT_STRETCH = 2, /* the whole surface */
} fit_mode_t;

fit_rect_t fit_rect_mode(int width, int height, fit_mode_t mode);

/* What the drawn picture depends on besides the frame: the surface size and the picture settings. */
typedef struct {
    int w, h, mode;
    bool smooth;
    uint32_t background;
} picture_look_t;

/* Whether the picture must be drawn again: a fresh frame, or a look different from *last (then kept in *last).
 * A zeroed *last means not drawn yet. */
bool picture_needs_draw(picture_look_t *last, picture_look_t now, bool fresh);

#endif
