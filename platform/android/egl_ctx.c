#include "egl_ctx.h"

#include <EGL/eglext.h>
#include <android/log.h>

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "gbaemu", __VA_ARGS__)

bool egl_ctx_init(egl_ctx_t *e, bool for_pbuffer) {
    e->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    e->context = EGL_NO_CONTEXT;
    if (e->display == EGL_NO_DISPLAY || !eglInitialize(e->display, NULL, NULL)) {
        LOGE("eglInitialize failed: 0x%x", eglGetError());
        return false;
    }
    const EGLint attrs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
                            EGL_SURFACE_TYPE, for_pbuffer ? EGL_PBUFFER_BIT : EGL_WINDOW_BIT,
                            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                            EGL_NONE};
    EGLint count = 0;
    if (!eglChooseConfig(e->display, attrs, &e->config, 1, &count) || count < 1) {
        LOGE("no RGBA8888 OpenGL ES 3 config: 0x%x", eglGetError());
        return false;
    }
    const EGLint ctx_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    e->context = eglCreateContext(e->display, e->config, EGL_NO_CONTEXT, ctx_attrs);
    if (e->context == EGL_NO_CONTEXT) {
        LOGE("eglCreateContext (ES 3) failed: 0x%x", eglGetError());
        return false;
    }
    return true;
}

void egl_ctx_destroy(egl_ctx_t *e) {
    if (e->display == EGL_NO_DISPLAY) return;
    eglMakeCurrent(e->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (e->context != EGL_NO_CONTEXT) eglDestroyContext(e->display, e->context);
    e->context = EGL_NO_CONTEXT;
}
