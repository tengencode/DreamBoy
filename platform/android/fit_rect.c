#include "fit_rect.h"

fit_rect_t fit_rect(int width, int height, int frame_w, int frame_h) {
    fit_rect_t r = {0, 0, 0, 0};
    if (width <= 0 || height <= 0 || frame_w <= 0 || frame_h <= 0) return r;
    if ((long)width * frame_h <= (long)height * frame_w) { /* the width is the limit: bars above and below */
        r.w = width;
        r.h = (int)((long)width * frame_h / frame_w);
    } else { /* the height is the limit: bars left and right */
        r.h = height;
        r.w = (int)((long)height * frame_w / frame_h);
    }
    r.x = (width - r.w) / 2;
    r.y = (height - r.h) / 2;
    return r;
}

fit_rect_t fit_rect_mode(int width, int height, int frame_w, int frame_h, fit_mode_t mode) {
    if (width <= 0 || height <= 0 || frame_w <= 0 || frame_h <= 0) return fit_rect(width, height, frame_w, frame_h);
    if (mode == FIT_STRETCH) return (fit_rect_t){0, 0, width, height};
    if (mode == FIT_INTEGER) {
        int n = width / frame_w < height / frame_h ? width / frame_w : height / frame_h;
        if (n >= 1) return (fit_rect_t){(width - frame_w * n) / 2, (height - frame_h * n) / 2, frame_w * n, frame_h * n};
    }
    return fit_rect(width, height, frame_w, frame_h);
}

bool picture_needs_draw(picture_look_t *last, picture_look_t now, bool fresh) {
    bool same = last->w == now.w && last->h == now.h && last->mode == now.mode && last->smooth == now.smooth &&
                last->background == now.background && last->frame_w == now.frame_w && last->frame_h == now.frame_h;
    *last = now;
    return fresh || !same;
}
