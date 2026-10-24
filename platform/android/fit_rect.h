#ifndef GBAEMU_FIT_RECT_H
#define GBAEMU_FIT_RECT_H

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

#endif
