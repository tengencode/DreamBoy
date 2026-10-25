#include "render_thread.h"

#include <android/choreographer.h>
#include <android/log.h>
#include <android/looper.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <unistd.h>

#include "egl_ctx.h"
#include "fit_rect.h"
#include "video_gl.h"

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "gbaemu", __VA_ARGS__)

struct render_thread {
    pthread_t thread;
    triple_buffer_t *tb;
    int pipe_fd[2]; /* messages to the thread: 'W' window changed, 'Q' quit */

    pthread_mutex_t lock; /* guards pending/requested/handled/last_ok */
    pthread_cond_t done;
    ANativeWindow *pending;
    atomic_int fit_mode; /* the picture settings, set from any thread */
    atomic_bool smooth;
    atomic_uint background; /* 0xAARRGGBB round the picture */
    unsigned requested, handled;
    bool last_ok;

    /* Only the render thread touches these. */
    AChoreographer *choreographer;
    egl_ctx_t egl;
    bool egl_ready, gl_ready, frame_posted, quit;
    EGLSurface surface;
    ANativeWindow *window;
    video_gl_t gl;
    picture_look_t drawn; /* zeroed for a new surface: draw it once */
};

static void post_frame(render_thread_t *r);

static void drop_surface(render_thread_t *r) {
    if (r->surface != EGL_NO_SURFACE) {
        eglMakeCurrent(r->egl.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(r->egl.display, r->surface);
        r->surface = EGL_NO_SURFACE;
    }
    if (r->window) ANativeWindow_release(r->window);
    r->window = NULL;
}

/* Tear down the whole context (it was lost, or the thread is ending); GL objects go with it. */
static void drop_context(render_thread_t *r) {
    if (r->surface != EGL_NO_SURFACE) {
        eglMakeCurrent(r->egl.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(r->egl.display, r->surface);
        r->surface = EGL_NO_SURFACE;
    }
    if (r->egl_ready) egl_ctx_destroy(&r->egl);
    r->egl_ready = r->gl_ready = false;
}

/* Surface for r->window; context and GL objects made on first use. */
static bool make_surface(render_thread_t *r) {
    if (!r->egl_ready) {
        r->egl_ready = egl_ctx_init(&r->egl, false);
        if (!r->egl_ready) return false;
    }
    r->surface = eglCreateWindowSurface(r->egl.display, r->egl.config, r->window, NULL);
    if (r->surface == EGL_NO_SURFACE || !eglMakeCurrent(r->egl.display, r->surface, r->surface, r->egl.context)) {
        LOGE("window surface failed: 0x%x", eglGetError());
        return false;
    }
    r->drawn = (picture_look_t){0};
    if (!r->gl_ready) {
        r->gl_ready = video_gl_init(&r->gl);
        if (!r->gl_ready) return false;
        bool fresh;
        video_gl_upload(&r->gl, tb_take(r->tb, &fresh)); /* show the newest frame straight away */
    }
    post_frame(r);
    return true;
}

static bool apply_window(render_thread_t *r, ANativeWindow *w) {
    if (w && w == r->window) { /* same window, new size: drawing reads the size every frame */
        ANativeWindow_release(w);
        return r->gl_ready && r->surface != EGL_NO_SURFACE;
    }
    drop_surface(r);
    if (!w) return true;
    r->window = w;
    return make_surface(r);
}

static void on_frame(render_thread_t *r) {
    r->frame_posted = false;
    if (r->quit || r->surface == EGL_NO_SURFACE || !r->gl_ready) return;
    bool fresh;
    const uint16_t *frame = tb_take(r->tb, &fresh);
    if (fresh) video_gl_upload(&r->gl, frame);
    EGLint w = 0, h = 0;
    eglQuerySurface(r->egl.display, r->surface, EGL_WIDTH, &w);
    eglQuerySurface(r->egl.display, r->surface, EGL_HEIGHT, &h);
    picture_look_t look = {w, h, atomic_load(&r->fit_mode), atomic_load(&r->smooth), atomic_load(&r->background)};
    if (!picture_needs_draw(&r->drawn, look, fresh)) { /* paused: the screen keeps the last picture */
        post_frame(r);
        return;
    }
    video_gl_draw(&r->gl, w, h, look.mode, look.smooth, look.background);
    if (!eglSwapBuffers(r->egl.display, r->surface) && eglGetError() == EGL_CONTEXT_LOST) {
        LOGE("EGL context lost: rebuilding");
        drop_context(r);
        if (!make_surface(r)) return;
    }
    post_frame(r);
}

static void frame_cb64(int64_t frame_time_ns, void *data) {
    (void)frame_time_ns;
    on_frame(data);
}

static void frame_cb(long frame_time_ns, void *data) {
    (void)frame_time_ns;
    on_frame(data);
}

static void post_frame(render_thread_t *r) {
    if (r->frame_posted) return;
    r->frame_posted = true;
    if (__builtin_available(android 29, *)) {
        AChoreographer_postFrameCallback64(r->choreographer, frame_cb64, r);
    } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        AChoreographer_postFrameCallback(r->choreographer, frame_cb, r);
#pragma clang diagnostic pop
    }
}

static int on_message(int fd, int events, void *data) {
    (void)events;
    render_thread_t *r = data;
    char msg;
    if (read(fd, &msg, 1) != 1) return 1;
    if (msg == 'Q') {
        r->quit = true;
        return 1;
    }
    pthread_mutex_lock(&r->lock);
    ANativeWindow *w = r->pending;
    r->pending = NULL;
    unsigned seq = r->requested;
    pthread_mutex_unlock(&r->lock);
    bool ok = apply_window(r, w);
    pthread_mutex_lock(&r->lock);
    r->handled = seq;
    r->last_ok = ok;
    pthread_cond_broadcast(&r->done);
    pthread_mutex_unlock(&r->lock);
    return 1;
}

static void *render_main(void *arg) {
    render_thread_t *r = arg;
    ALooper *looper = ALooper_prepare(0);
    ALooper_addFd(looper, r->pipe_fd[0], 1, ALOOPER_EVENT_INPUT, on_message, r);
    r->choreographer = AChoreographer_getInstance();
    while (!r->quit) ALooper_pollOnce(-1, NULL, NULL, NULL);
    ALooper_removeFd(looper, r->pipe_fd[0]);
    drop_surface(r);
    drop_context(r);
    return NULL;
}

render_thread_t *render_create(triple_buffer_t *tb) {
    render_thread_t *r = calloc(1, sizeof *r);
    if (!r) return NULL;
    r->tb = tb;
    r->surface = EGL_NO_SURFACE;
    atomic_init(&r->background, 0xFF000000u); /* black until the skin's colour comes */
    if (pipe(r->pipe_fd) != 0) {
        free(r);
        return NULL;
    }
    pthread_mutex_init(&r->lock, NULL);
    pthread_cond_init(&r->done, NULL);
    if (pthread_create(&r->thread, NULL, render_main, r) != 0) { /* low on memory: no picture, but no hang */
        pthread_mutex_destroy(&r->lock);
        pthread_cond_destroy(&r->done);
        close(r->pipe_fd[0]);
        close(r->pipe_fd[1]);
        free(r);
        return NULL;
    }
    return r;
}

bool render_set_window(render_thread_t *r, ANativeWindow *w) {
    if (w) ANativeWindow_acquire(w); /* the render thread owns this reference now */
    pthread_mutex_lock(&r->lock);
    r->pending = w;
    unsigned seq = ++r->requested;
    pthread_mutex_unlock(&r->lock);
    char msg = 'W';
    write(r->pipe_fd[1], &msg, 1);
    pthread_mutex_lock(&r->lock);
    while (r->handled != seq) pthread_cond_wait(&r->done, &r->lock);
    bool ok = r->last_ok;
    pthread_mutex_unlock(&r->lock);
    return ok;
}

void render_set_picture(render_thread_t *r, int fit_mode, bool smooth) {
    atomic_store(&r->fit_mode, fit_mode);
    atomic_store(&r->smooth, smooth);
}

void render_set_background(render_thread_t *r, uint32_t argb) {
    atomic_store(&r->background, argb);
}

void render_destroy(render_thread_t *r) {
    if (!r) return;
    char msg = 'Q';
    write(r->pipe_fd[1], &msg, 1);
    pthread_join(r->thread, NULL);
    close(r->pipe_fd[0]);
    close(r->pipe_fd[1]);
    pthread_mutex_destroy(&r->lock);
    pthread_cond_destroy(&r->done);
    free(r);
}
