#ifndef GBAEMU_RENDER_THREAD_H
#define GBAEMU_RENDER_THREAD_H

#include <android/native_window.h>
#include <stdbool.h>
#include <stdint.h>

#include "triple_buffer.h"

/* The thread that owns OpenGL: once per display refresh (AChoreographer) it draws the newest
 * frame from the triple buffer into the current window. */
typedef struct render_thread render_thread_t;

render_thread_t *render_create(triple_buffer_t *tb);
/* The window appeared, changed (w) or went away (NULL). Returns only after the render thread has
 * switched over (and let go of the old window); false if OpenGL ES 3 could not draw into w. */
bool render_set_window(render_thread_t *r, ANativeWindow *w);
/* The picture settings (fit_mode: fit_mode_t; smooth pixels): used from the next frame on, from any thread. */
void render_set_picture(render_thread_t *r, int fit_mode, bool smooth);
/* The colour round the picture (0xAARRGGBB, the skin's body): from the next frame on, from any thread. */
void render_set_background(render_thread_t *r, uint32_t argb);
void render_destroy(render_thread_t *r);

#endif
