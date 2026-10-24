#ifndef GBAEMU_EGL_CTX_H
#define GBAEMU_EGL_CTX_H

#include <EGL/egl.h>
#include <stdbool.h>

/* An EGL display + RGBA8888 config + OpenGL ES 3 context. */
typedef struct {
    EGLDisplay display;
    EGLConfig config;
    EGLContext context;
} egl_ctx_t;

/* for_pbuffer: the config must support offscreen pbuffer surfaces (tests), else window surfaces. */
bool egl_ctx_init(egl_ctx_t *e, bool for_pbuffer);
void egl_ctx_destroy(egl_ctx_t *e);

#endif
