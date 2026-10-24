#include "audio_out.h"

#include <aaudio/AAudio.h>
#include <android/log.h>
#include <stdlib.h>
#include <string.h>

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "gbaemu", __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "gbaemu", __VA_ARGS__)

struct audio_out {
    AAudioStream *stream;
    audio_ring_t *ring;
    audio_stats_t *stats;
    atomic_bool disconnected;
    int rate, burst;
};

/* AAudio's high-priority thread: copy from the ring, silence for what is missing. No locks, no allocation. */
static aaudio_data_callback_result_t on_data(AAudioStream *stream, void *user, void *audio, int32_t frames) {
    (void)stream;
    audio_out_t *a = user;
    int16_t *out = audio;
    size_t got = audio_ring_pop(a->ring, out, (size_t)frames);
    if (got < (size_t)frames) memset(out + got * 2, 0, ((size_t)frames - got) * 4);
    long long loud = 0;
    for (size_t i = 0; i < got; i++) loud += (out[i * 2] | out[i * 2 + 1]) != 0;
    atomic_fetch_add_explicit(&a->stats->played, frames, memory_order_relaxed);
    atomic_fetch_add_explicit(&a->stats->silent, (long long)((size_t)frames - got), memory_order_relaxed);
    atomic_fetch_add_explicit(&a->stats->loud, loud, memory_order_relaxed);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

/* Called on its own thread when the stream breaks (e.g. headphones unplugged). Reopening is not
 * allowed here: only raise the flag; the emulation thread reopens at the next frame. */
static void on_error(AAudioStream *stream, void *user, aaudio_result_t error) {
    (void)stream;
    (void)error;
    audio_out_t *a = user;
    atomic_store(&a->disconnected, true);
}

audio_out_t *audio_out_open(audio_ring_t *ring, audio_stats_t *stats, int bursts) {
    audio_out_t *a = calloc(1, sizeof *a);
    if (!a) return NULL;
    a->ring = ring;
    a->stats = stats;
    atomic_init(&a->disconnected, false);
    AAudioStreamBuilder *b = NULL;
    aaudio_result_t res = AAudio_createStreamBuilder(&b);
    if (res != AAUDIO_OK) {
        LOGE("AAudio builder: %s", AAudio_convertResultToText(res));
        free(a);
        return NULL;
    }
    AAudioStreamBuilder_setDirection(b, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setChannelCount(b, 2);
    AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    if (__builtin_available(android 28, *)) {
        AAudioStreamBuilder_setUsage(b, AAUDIO_USAGE_GAME);
        AAudioStreamBuilder_setContentType(b, AAUDIO_CONTENT_TYPE_MUSIC);
    }
    AAudioStreamBuilder_setDataCallback(b, on_data, a);
    AAudioStreamBuilder_setErrorCallback(b, on_error, a);
    res = AAudioStreamBuilder_openStream(b, &a->stream);
    AAudioStreamBuilder_delete(b);
    if (res != AAUDIO_OK) {
        LOGE("AAudio open: %s", AAudio_convertResultToText(res));
        free(a);
        return NULL;
    }
    a->rate = AAudioStream_getSampleRate(a->stream);
    a->burst = AAudioStream_getFramesPerBurst(a->stream);
    /* Two bursts of AAudio's own buffering is enough (Google's advice; the phone emulator's default was
     * 4,360 frames = 91 ms of extra delay); more (Settings → Sound → Delay) for a phone that crackles. */
    AAudioStream_setBufferSizeInFrames(a->stream, a->burst * (bursts >= 2 ? bursts : 2));
    LOGI("AAudio stream: %d Hz, burst %d frames, buffer %d frames", a->rate, a->burst,
         AAudioStream_getBufferSizeInFrames(a->stream));
    return a;
}

bool audio_out_start(audio_out_t *a) {
    aaudio_result_t res = AAudioStream_requestStart(a->stream);
    if (res != AAUDIO_OK) LOGE("AAudio start: %s", AAudio_convertResultToText(res));
    return res == AAUDIO_OK;
}

void audio_out_pause(audio_out_t *a) {
    if (AAudioStream_requestPause(a->stream) != AAUDIO_OK) return;
    aaudio_stream_state_t next = AAUDIO_STREAM_STATE_UNINITIALIZED;
    AAudioStream_waitForStateChange(a->stream, AAUDIO_STREAM_STATE_PAUSING, &next, 200 * 1000000LL);
}

bool audio_out_disconnected(audio_out_t *a) {
    return atomic_load(&a->disconnected);
}

int audio_out_rate(const audio_out_t *a) {
    return a->rate;
}

int audio_out_burst(const audio_out_t *a) {
    return a->burst;
}

void audio_out_close(audio_out_t *a) {
    if (!a) return;
    AAudioStream_requestStop(a->stream);
    AAudioStream_close(a->stream);
    free(a);
}
