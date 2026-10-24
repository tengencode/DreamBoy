#include "gl_readback.h"

#include <string.h>

#include "egl_ctx.h"
#include "video_gl.h"

bool gl_readback(const uint16_t *pixels, int width, int height, int fit_mode, bool smooth, uint32_t background,
                 uint32_t *out) {
    egl_ctx_t e;
    if (!egl_ctx_init(&e, true)) return false;
    const EGLint pb_attrs[] = {EGL_WIDTH, width, EGL_HEIGHT, height, EGL_NONE};
    EGLSurface s = eglCreatePbufferSurface(e.display, e.config, pb_attrs);
    bool ok = s != EGL_NO_SURFACE && eglMakeCurrent(e.display, s, s, e.context);
    video_gl_t v = {0, 0, 0, 0};
    if (ok) ok = video_gl_init(&v);
    if (ok) {
        video_gl_upload(&v, pixels);
        video_gl_draw(&v, width, height, fit_mode, smooth, background);
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, out);
        ok = glGetError() == GL_NO_ERROR;
        for (int y = 0; y < height / 2; y++) { /* GL rows start at the bottom */
            uint32_t *a = out + (size_t)y * width, *b = out + (size_t)(height - 1 - y) * width;
            for (int x = 0; x < width; x++) {
                uint32_t t = a[x];
                a[x] = b[x];
                b[x] = t;
            }
        }
        video_gl_destroy(&v);
    }
    if (s != EGL_NO_SURFACE) {
        eglMakeCurrent(e.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(e.display, s);
    }
    egl_ctx_destroy(&e);
    return ok;
}
