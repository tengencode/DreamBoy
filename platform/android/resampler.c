#include "resampler.h"

#include <string.h>

void resampler_init(resampler_t *r) {
    memset(r->w, 0, sizeof r->w);
    r->t = 1.0; /* the first call starts by taking input */
}

static int16_t hermite(double xm1, double x0, double x1, double x2, double t) {
    double y = x0 + 0.5 * t * (x1 - xm1 + t * (2.0 * xm1 - 5.0 * x0 + 4.0 * x1 - x2 + t * (3.0 * (x0 - x1) + x2 - xm1)));
    double rounded = y >= 0.0 ? y + 0.5 : y - 0.5;
    if (rounded > 32767.0) return 32767;
    if (rounded < -32768.0) return -32768;
    return (int16_t)rounded;
}

size_t resampler_run(resampler_t *r, const int16_t *in, size_t in_frames, double ratio, int16_t *out,
                     size_t out_cap) {
    double step = 1.0 / ratio;
    size_t i = 0, n = 0;
    for (;;) {
        while (r->t >= 1.0) {
            if (i == in_frames) return n;
            memmove(r->w[0], r->w[1], sizeof r->w[0] * 3);
            r->w[3][0] = in[i * 2];
            r->w[3][1] = in[i * 2 + 1];
            i++;
            r->t -= 1.0;
        }
        if (n < out_cap) {
            for (int c = 0; c < 2; c++) out[n * 2 + c] = hermite(r->w[0][c], r->w[1][c], r->w[2][c], r->w[3][c], r->t);
            n++;
        } /* out of room: the input is still consumed (and those outputs dropped) */
        r->t += step;
    }
}
