#include "fit_rect.h"

fit_rect_t fit_rect(int width, int height) {
    fit_rect_t r = {0, 0, 0, 0};
    if (width <= 0 || height <= 0) return r;
    if ((long)width * 2 <= (long)height * 3) { /* the width is the limit: bars above and below */
        r.w = width;
        r.h = width * 2 / 3;
    } else { /* the height is the limit: bars left and right */
        r.h = height;
        r.w = height * 3 / 2;
    }
    r.x = (width - r.w) / 2;
    r.y = (height - r.h) / 2;
    return r;
}

fit_rect_t fit_rect_mode(int width, int height, fit_mode_t mode) {
    if (width <= 0 || height <= 0) return fit_rect(width, height);
    if (mode == FIT_STRETCH) return (fit_rect_t){0, 0, width, height};
    if (mode == FIT_INTEGER) {
        int n = width / 240 < height / 160 ? width / 240 : height / 160;
        if (n >= 1) return (fit_rect_t){(width - 240 * n) / 2, (height - 160 * n) / 2, 240 * n, 160 * n};
    }
    return fit_rect(width, height);
}

bool picture_needs_draw(picture_look_t *last, picture_look_t now, bool fresh) {
    bool same = last->w == now.w && last->h == now.h && last->mode == now.mode && last->smooth == now.smooth &&
                last->background == now.background;
    *last = now;
    return fresh || !same;
}
