#ifndef GBAEMU_GL_READBACK_H
#define GBAEMU_GL_READBACK_H

#include <stdbool.h>
#include <stdint.h>

/* Test helper: draws one 240x160 BGR555 frame with the real video_gl code (fit_mode, smooth, background: as video_gl_draw) into an offscreen
 * width x height surface on the calling thread and reads it back, top row first, one
 * little-endian RGBA pixel per uint32_t (r | g << 8 | b << 16 | a << 24). */
bool gl_readback(const uint16_t *pixels, int width, int height, int fit_mode, bool smooth, uint32_t background,
                 uint32_t *out);

#endif
