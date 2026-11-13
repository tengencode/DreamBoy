#ifndef GBAEMU_FIT_RECT_H
#define GBAEMU_FIT_RECT_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int x, y, w, h;
} fit_rect_t;

/* The largest rectangle of the frame's shape (frame_w : frame_h — 3:2 for a Game Boy Advance, 10:9 for a Game
 * Boy) inside a width x height surface, centred, in whole pixels (x and y are the offsets of the free space on
 * each side; all zero for an empty surface). */
fit_rect_t fit_rect(int width, int height, int frame_w, int frame_h);

/* How the picture fills the surface (Settings → Picture → Size). */
typedef enum {
    FIT_ASPECT = 0,  /* fit_rect: the largest rectangle of the frame's shape */
    FIT_INTEGER = 1, /* the largest whole multiple of the frame that fits, centred (FIT_ASPECT if not even 1x fits) */
    FIT_STRETCH = 2, /* the whole surface */
} fit_mode_t;

fit_rect_t fit_rect_mode(int width, int height, int frame_w, int frame_h, fit_mode_t mode);

/* What the drawn picture depends on besides the frame: the surface size and the picture settings. */
typedef struct {
    int w, h, mode;
    int frame_w, frame_h;
    bool smooth;
    uint32_t background;
} picture_look_t;

/* Whether the picture must be drawn again: a fresh frame, or a look different from *last (then kept in *last).
 * A zeroed *last means not drawn yet. */
bool picture_needs_draw(picture_look_t *last, picture_look_t now, bool fresh);

#endif
