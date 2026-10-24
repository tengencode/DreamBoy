#ifndef GBAEMU_RESAMPLER_H
#define GBAEMU_RESAMPLER_H

#include <stddef.h>
#include <stdint.h>

/* Stereo int16 rate conversion with 4-point cubic Hermite (Catmull-Rom) interpolation. It keeps its
 * place between calls, so converting in pieces gives exactly the samples of converting all at once. */
typedef struct {
    int16_t w[4][2]; /* the last four input frames: x[n-1], x[n], x[n+1], x[n+2] */
    double t;        /* position between x[n] and x[n+1], in input frames; >= 1 means "take more input" */
} resampler_t;

void resampler_init(resampler_t *r);
/* Converts all `in_frames` input frames at `ratio` output frames per input frame into `out` (room for
 * `out_cap` frames — give it at least in_frames × ratio + 4); returns the frames written. The ratio
 * may change from call to call. */
size_t resampler_run(resampler_t *r, const int16_t *in, size_t in_frames, double ratio, int16_t *out,
                     size_t out_cap);

#endif
