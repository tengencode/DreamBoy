#ifndef GBAEMU_VIDEO_GL_H
#define GBAEMU_VIDEO_GL_H

#include <GLES3/gl3.h>
#include <stdbool.h>
#include <stdint.h>

/* Draws 240x160 BGR555 frames with OpenGL ES 3.0: one integer texture, one quad, a shader that
 * unpacks the 15-bit colour (sharp: the nearest texel; smooth: the 4 nearest mixed). Every call needs the
 * same current GLES 3 context. */
typedef struct {
    GLuint program, texture, vao;
    GLint smooth_loc;
} video_gl_t;

bool video_gl_init(video_gl_t *v);                          /* false if the shader will not build */
void video_gl_upload(video_gl_t *v, const uint16_t *pixels); /* a new frame */
/* The surface in background (0xAARRGGBB, the skin's body colour; black before 8b) + the frame, placed by
 * fit_rect_mode(fit_mode: fit_mode_t), sharp or smooth. */
void video_gl_draw(video_gl_t *v, int width, int height, int fit_mode, bool smooth, uint32_t background);
void video_gl_destroy(video_gl_t *v);

#endif
