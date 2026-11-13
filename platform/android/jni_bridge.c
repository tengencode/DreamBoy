/* JNI entry points for com.tengen.gbaemu.NativeBridge. Called from one thread at a time (the UI
 * thread in the app, the test thread in on-device tests); they keep one game session. */
#include <android/native_window_jni.h>
#include <inttypes.h>
#include <jni.h>
#include <stdio.h>
#include <stdlib.h>

#include "emu_thread.h"
#include "frame_hash.h"
#include "gl_readback.h"
#include "jni_shared.h"
#include "render_thread.h"

static emu_t *s_emu;
static render_thread_t *s_render;
static ANativeWindow *s_window; /* a window that arrived before start() */

/* Settings kept for every game (set any time; the BIOS and the rewind budget are used by the next loadRom). */
static uint8_t *s_bios;       /* a BIOS file's 16 KiB, or NULL: the built-in BIOS */
static size_t s_rewind_budget = EMU_REWIND_DEFAULT;
static bool s_sound = true;
static int s_sound_bursts = 2;
static int s_fit_mode;        /* fit_mode_t */
static bool s_smooth;
static uint32_t s_background = 0xFF000000u; /* round the picture: the skin's body (8b) */
/* The next game's own settings (used by the next loadRom only). */
static int s_next_save_type = EMU_AUTO, s_next_clock = EMU_AUTO;

#define JNI_FN(name) Java_com_tengen_gbaemu_NativeBridge_##name

emu_t *jni_emu(void) {
    return s_emu;
}

static void stop_session(void) {
    render_destroy(s_render); /* first: it reads the emulator's frames */
    s_render = NULL;
    emu_destroy(s_emu);
    s_emu = NULL;
    if (jni_ra()) ra_unload_game(jni_ra()); /* after the thread has stopped: no more frames */
}

/* loadRom / loadGame: console is EMU_AUTO or EMU_CONSOLE_* (what the app worked out from the file's name too). */
static jint load_game(JNIEnv *env, jobject rom, jstring save_path, jint console) {
    stop_session(); /* writes the previous game's save */
    const uint8_t *data = (*env)->GetDirectBufferAddress(env, rom);
    jlong size = (*env)->GetDirectBufferCapacity(env, rom);
    if (!data || size <= 0) return GBA_ERR_ROM_SIZE;
    const char *path = save_path ? (*env)->GetStringUTFChars(env, save_path, NULL) : NULL;
    emu_options_t opt = emu_options_default();
    opt.bios = s_bios;
    opt.bios_size = s_bios ? 16384 : 0;
    opt.save_type = s_next_save_type;
    opt.clock = s_next_clock;
    opt.rewind_budget = s_rewind_budget;
    opt.sound = s_sound;
    opt.sound_bursts = s_sound_bursts;
    opt.ra = jni_ra();
    opt.console = console >= EMU_CONSOLE_GBA && console <= EMU_CONSOLE_GBC ? console : EMU_AUTO;
    s_next_save_type = s_next_clock = EMU_AUTO;
    gba_err_t err;
    s_emu = emu_create(data, (size_t)size, path, &opt, &err);
    if (path) (*env)->ReleaseStringUTFChars(env, save_path, path);
    return err;
}

JNIEXPORT jint JNICALL JNI_FN(loadRom)(JNIEnv *env, jobject self, jobject rom, jstring save_path) {
    (void)self;
    return load_game(env, rom, save_path, EMU_AUTO);
}

JNIEXPORT jint JNICALL JNI_FN(loadGame)(JNIEnv *env, jobject self, jobject rom, jstring save_path, jint console) {
    (void)self;
    return load_game(env, rom, save_path, console);
}

/* The loaded game's console (EMU_CONSOLE_*; -1 without a game) and its picture's size (240x160 without one). */
JNIEXPORT jint JNICALL JNI_FN(console)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    return s_emu ? emu_console(s_emu) : -1;
}

JNIEXPORT jint JNICALL JNI_FN(frameWidth)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    int w = 240, h = 160;
    if (s_emu) emu_frame_size(s_emu, &w, &h);
    return w;
}

JNIEXPORT jint JNICALL JNI_FN(frameHeight)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    int w = 240, h = 160;
    if (s_emu) emu_frame_size(s_emu, &w, &h);
    return h;
}

/* The BIOS for the next games: 16 KiB, or null for the built-in one. GBA_ERR_BIOS_SIZE (nothing changes) otherwise. */
JNIEXPORT jint JNICALL JNI_FN(setBios)(JNIEnv *env, jobject self, jbyteArray bytes) {
    (void)self;
    if (!bytes) {
        free(s_bios);
        s_bios = NULL;
        return GBA_OK;
    }
    if ((*env)->GetArrayLength(env, bytes) != 16384) return GBA_ERR_BIOS_SIZE;
    uint8_t *copy = malloc(16384);
    if (!copy) return GBA_ERR_NOMEM;
    (*env)->GetByteArrayRegion(env, bytes, 0, 16384, (jbyte *)copy);
    free(s_bios);
    s_bios = copy;
    return GBA_OK;
}

/* Bytes for the next games' rewind snapshots (0 = rewind off). */
JNIEXPORT void JNICALL JNI_FN(setRewindBudget)(JNIEnv *env, jobject self, jlong bytes) {
    (void)env;
    (void)self;
    s_rewind_budget = bytes > 0 ? (size_t)bytes : 0;
}

/* The next loadRom's save type (a gba_save_type_t, or -1 = detect) and clock (-1 = detect, 0 = none, 1 = one). */
JNIEXPORT void JNICALL JNI_FN(setGameOverrides)(JNIEnv *env, jobject self, jint save_type, jint clock) {
    (void)env;
    (void)self;
    s_next_save_type = save_type;
    s_next_clock = clock;
}

/* Sound on/off and AAudio's buffer in bursts (2, 4, 8): at once for a running game, and for the next ones. */
JNIEXPORT void JNICALL JNI_FN(setSound)(JNIEnv *env, jobject self, jboolean on, jint bursts) {
    (void)env;
    (void)self;
    s_sound = on == JNI_TRUE;
    s_sound_bursts = bursts;
    if (s_emu) emu_set_sound(s_emu, s_sound, bursts);
}

/* How the picture fills the screen (fit_mode_t) and whether pixels are smoothed: from the next frame on. */
JNIEXPORT void JNICALL JNI_FN(setPicture)(JNIEnv *env, jobject self, jint fit_mode, jboolean smooth) {
    (void)env;
    (void)self;
    s_fit_mode = fit_mode;
    s_smooth = smooth == JNI_TRUE;
    if (s_render) render_set_picture(s_render, s_fit_mode, s_smooth);
}

/* The colour round the picture (0xAARRGGBB: the skin's body): from the next frame on. */
JNIEXPORT void JNICALL JNI_FN(setBackground)(JNIEnv *env, jobject self, jint argb) {
    (void)env;
    (void)self;
    s_background = (uint32_t)argb;
    if (s_render) render_set_background(s_render, s_background);
}

JNIEXPORT jint JNICALL JNI_FN(saveStatus)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    return s_emu ? (jint)emu_save_status(s_emu) : EMU_SAVE_NONE;
}

JNIEXPORT jint JNICALL JNI_FN(importSave)(JNIEnv *env, jobject self, jbyteArray bytes) {
    (void)self;
    if (!s_emu) return GBA_ERR_SAVE_SIZE;
    jsize n = (*env)->GetArrayLength(env, bytes);
    jbyte *b = (*env)->GetByteArrayElements(env, bytes, NULL);
    gba_err_t err = emu_import_save(s_emu, (const uint8_t *)b, (size_t)n);
    (*env)->ReleaseByteArrayElements(env, bytes, b, JNI_ABORT);
    return err;
}

JNIEXPORT jboolean JNICALL JNI_FN(saveState)(JNIEnv *env, jobject self, jstring path) {
    (void)self;
    if (!s_emu) return JNI_FALSE;
    const char *p = (*env)->GetStringUTFChars(env, path, NULL);
    bool ok = emu_state_save(s_emu, p);
    (*env)->ReleaseStringUTFChars(env, path, p);
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL JNI_FN(loadState)(JNIEnv *env, jobject self, jstring path) {
    (void)self;
    if (!s_emu) return GBA_ERR_STATE;
    const char *p = (*env)->GetStringUTFChars(env, path, NULL);
    gba_err_t err = emu_state_load(s_emu, p);
    (*env)->ReleaseStringUTFChars(env, path, p);
    return err;
}

/* [the type it reads as, the first bad line (0 = none)]; no game needed. */
JNIEXPORT jintArray JNICALL JNI_FN(checkCheat)(JNIEnv *env, jobject self, jstring text, jint type) {
    (void)self;
    const char *t = (*env)->GetStringUTFChars(env, text, NULL);
    int bad = 1;
    gba_cheat_type_t used = t ? gba_cheat_check(t, (gba_cheat_type_t)type, &bad) : (gba_cheat_type_t)type;
    if (t) (*env)->ReleaseStringUTFChars(env, text, t);
    jint v[2] = {(jint)used, bad};
    jintArray out = (*env)->NewIntArray(env, 2);
    if (out) (*env)->SetIntArrayRegion(env, out, 0, 2, v);
    return out;
}

/* The switched-on cheats (only while paused or before start): 0, or a gba_err_t. */
JNIEXPORT jint JNICALL JNI_FN(setCheats)(JNIEnv *env, jobject self, jobjectArray texts, jintArray types) {
    (void)self;
    if (!s_emu) return GBA_ERR_CHEAT;
    jsize n = (*env)->GetArrayLength(env, texts);
    if ((*env)->GetArrayLength(env, types) != n) return GBA_ERR_CHEAT;
    const char **t = calloc((size_t)n + 1, sizeof *t);
    jstring *js = calloc((size_t)n + 1, sizeof *js);
    gba_cheat_type_t *ty = calloc((size_t)n + 1, sizeof *ty);
    jint *raw = (*env)->GetIntArrayElements(env, types, NULL);
    gba_err_t err = t && js && ty && raw ? GBA_OK : GBA_ERR_NOMEM;
    for (jsize i = 0; err == GBA_OK && i < n; i++) {
        js[i] = (jstring)(*env)->GetObjectArrayElement(env, texts, i);
        t[i] = js[i] ? (*env)->GetStringUTFChars(env, js[i], NULL) : NULL;
        if (!t[i]) err = GBA_ERR_CHEAT;
        ty[i] = (gba_cheat_type_t)raw[i];
    }
    if (err == GBA_OK) err = emu_cheats_set(s_emu, t, ty, (size_t)n);
    for (jsize i = 0; js && i < n; i++) {
        if (js[i] && t && t[i]) (*env)->ReleaseStringUTFChars(env, js[i], t[i]);
        if (js[i]) (*env)->DeleteLocalRef(env, js[i]);
    }
    if (raw) (*env)->ReleaseIntArrayElements(env, types, raw, JNI_ABORT);
    free(t);
    free(js);
    free(ty);
    return (jint)err;
}

JNIEXPORT jshortArray JNICALL JNI_FN(frameBgr555)(JNIEnv *env, jobject self) {
    (void)self;
    static uint16_t px[240 * 160];
    if (!s_emu || !emu_frame(s_emu, px)) return NULL;
    int w, h;
    emu_frame_size(s_emu, &w, &h);
    jshortArray out = (*env)->NewShortArray(env, w * h);
    (*env)->SetShortArrayRegion(env, out, 0, w * h, (const jshort *)px);
    return out;
}

JNIEXPORT void JNICALL JNI_FN(setMotion)(JNIEnv *env, jobject self, jint tilt_x, jint tilt_y, jint turn) {
    (void)env;
    (void)self;
    if (s_emu) emu_set_motion(s_emu, tilt_x, tilt_y, turn);
}

JNIEXPORT void JNICALL JNI_FN(setLight)(JNIEnv *env, jobject self, jint level) {
    (void)env;
    (void)self;
    if (s_emu) emu_set_light(s_emu, level);
}

JNIEXPORT jint JNICALL JNI_FN(cartHardware)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    return s_emu ? (jint)emu_cart_hardware(s_emu) : 0;
}

JNIEXPORT jboolean JNICALL JNI_FN(rumbleActive)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    return s_emu && emu_rumble_active(s_emu) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL JNI_FN(saveError)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    return s_emu && emu_save_error(s_emu) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL JNI_FN(start)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    if (!s_emu) return;
    if (!s_render) {
        s_render = render_create(emu_frames(s_emu));
        if (s_render) {
            render_set_picture(s_render, s_fit_mode, s_smooth);
            render_set_background(s_render, s_background);
        }
    }
    if (s_render && s_window) {
        render_set_window(s_render, s_window);
        ANativeWindow_release(s_window);
        s_window = NULL;
    }
    emu_start(s_emu);
}

JNIEXPORT jboolean JNICALL JNI_FN(setSurface)(JNIEnv *env, jobject self, jobject surface) {
    (void)self;
    ANativeWindow *w = surface ? ANativeWindow_fromSurface(env, surface) : NULL;
    if (!s_render) { /* not started yet: hand it over in start() */
        if (s_window) ANativeWindow_release(s_window);
        s_window = w;
        return JNI_TRUE;
    }
    bool ok = render_set_window(s_render, w);
    if (w) ANativeWindow_release(w);
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL JNI_FN(stop)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    stop_session();
    if (s_window) ANativeWindow_release(s_window);
    s_window = NULL;
}

JNIEXPORT void JNICALL JNI_FN(pause)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    if (s_emu) emu_pause(s_emu);
}

JNIEXPORT void JNICALL JNI_FN(resume)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    if (s_emu) emu_resume(s_emu);
}

JNIEXPORT void JNICALL JNI_FN(setKeys)(JNIEnv *env, jobject self, jint pressed) {
    (void)env;
    (void)self;
    if (s_emu) emu_set_keys(s_emu, (uint16_t)(pressed & 0x3FF));
}

JNIEXPORT void JNICALL JNI_FN(setFastForward)(JNIEnv *env, jobject self, jint speed) {
    (void)env;
    (void)self;
    if (s_emu) emu_set_fast_forward(s_emu, speed);
}

JNIEXPORT void JNICALL JNI_FN(setRewinding)(JNIEnv *env, jobject self, jboolean on) {
    (void)env;
    (void)self;
    if (s_emu) emu_set_rewinding(s_emu, on);
}

JNIEXPORT jlongArray JNICALL JNI_FN(debugRewindStats)(JNIEnv *env, jobject self) {
    (void)self;
    int64_t v[3] = {0, 0, 0};
    if (s_emu) emu_rewind_stats(s_emu, v);
    jlongArray out = (*env)->NewLongArray(env, 3);
    if (out) (*env)->SetLongArrayRegion(env, out, 0, 3, (const jlong *)v);
    return out;
}

JNIEXPORT jint JNICALL JNI_FN(debugPeek32)(JNIEnv *env, jobject self, jint addr) {
    (void)env;
    (void)self;
    return s_emu ? (jint)emu_debug_peek32(s_emu, (uint32_t)addr) : 0;
}

JNIEXPORT jlong JNICALL JNI_FN(debugFrameCount)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    return s_emu ? (jlong)emu_frame_count(s_emu) : 0;
}

JNIEXPORT jlongArray JNICALL JNI_FN(debugAudioStats)(JNIEnv *env, jobject self) {
    (void)self;
    int64_t v[7] = {0, 0, 0, 0, 0, 0, 0};
    if (s_emu) emu_audio_stats(s_emu, v);
    jlongArray out = (*env)->NewLongArray(env, 7);
    (*env)->SetLongArrayRegion(env, out, 0, 7, (const jlong *)v);
    return out;
}

JNIEXPORT void JNICALL JNI_FN(debugSetAudioEnabled)(JNIEnv *env, jobject self, jboolean enabled) {
    (void)env;
    (void)self;
    if (s_emu) emu_set_audio_enabled(s_emu, enabled == JNI_TRUE);
}

JNIEXPORT void JNICALL JNI_FN(debugSimulateAudioDisconnect)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    if (s_emu) emu_simulate_audio_disconnect(s_emu);
}

JNIEXPORT void JNICALL JNI_FN(debugSimulateStuckStream)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    if (s_emu) emu_simulate_stuck_stream(s_emu);
}

JNIEXPORT jbyteArray JNICALL JNI_FN(debugSaveRam)(JNIEnv *env, jobject self) {
    (void)self;
    if (!s_emu) return NULL;
    const uint8_t *data;
    size_t n = emu_debug_saveram(s_emu, &data);
    jbyteArray out = (*env)->NewByteArray(env, (jsize)n);
    (*env)->SetByteArrayRegion(env, out, 0, (jsize)n, (const jbyte *)data);
    return out;
}

JNIEXPORT jintArray JNICALL JNI_FN(debugMotionSeen)(JNIEnv *env, jobject self) {
    (void)self;
    int32_t v[4] = {0, 0, 0, 0};
    if (s_emu) emu_motion_seen(s_emu, v);
    jintArray out = (*env)->NewIntArray(env, 4);
    (*env)->SetIntArrayRegion(env, out, 0, 4, (const jint *)v);
    return out;
}

JNIEXPORT jint JNICALL JNI_FN(debugKeysSeen)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    return s_emu ? (jint)emu_keys_seen(s_emu) : 0;
}

#define REWIND_EVERY 4

/* Headless run on the calling thread, exactly like `testrunner --frames n [--state-at k]
 * [--rewind-at f:s]` (state_at / rewind_at < 0: none): "<frame hash> <audio hash>", or NULL when the ROM
 * or the state did not load, or a rewind step found no snapshot. */
static jstring run_headless(JNIEnv *env, jobject rom, jint frames, jint state_at, jint rewind_at, jint rewind_steps) {
    const uint8_t *data = (*env)->GetDirectBufferAddress(env, rom);
    jlong size = (*env)->GetDirectBufferCapacity(env, rom);
    machine_kind_t kind = data && size > 0 ? machine_detect(data, (size_t)size, NULL) : MACHINE_NONE;
    machine_t *g = machine_create(kind == MACHINE_NONE ? MACHINE_GBA : kind); /* no header: a GBA test ROM */
    if (!g || !data || machine_load_rom(g, data, (size_t)size) != MACHINE_OK) {
        machine_destroy(g);
        return NULL;
    }
    uint64_t audio_hash = AUDIO_HASH_INIT;
    int16_t buf[1024 * 2];
    uint64_t *hash_before = NULL; /* the audio hash before each frame's sound was read */
    if (rewind_at >= 0) {
        hash_before = malloc(sizeof(uint64_t) * (size_t)frames);
        if (!hash_before || machine_rewind_configure(g, 64u * 1024 * 1024, REWIND_EVERY) != MACHINE_OK) {
            free(hash_before);
            machine_destroy(g);
            return NULL;
        }
    }
    bool rewound = false;
    for (jint f = 0; f < frames; f++) {
        if (f == rewind_at && !rewound) { /* as testrunner --rewind-at */
            for (jint s = 0; s < rewind_steps; s++) {
                if (!machine_rewind_step_back(g)) {
                    free(hash_before);
                    machine_destroy(g);
                    return NULL;
                }
            }
            jint to = rewind_at - rewind_at % REWIND_EVERY - REWIND_EVERY * (rewind_steps - (rewind_at % REWIND_EVERY ? 1 : 0));
            f = to;
            audio_hash = hash_before[to - 1];
            rewound = true;
        }
        if (hash_before) hash_before[f] = audio_hash;
        if (f == state_at) { /* carry on in a brand-new machine loaded from a state */
            size_t n = machine_state_size(g);
            uint8_t *state = malloc(n);
            machine_t *fresh = machine_create(machine_kind(g));
            bool ok = state && fresh && machine_state_save(g, state, n) == n &&
                      machine_load_rom(fresh, data, (size_t)size) == MACHINE_OK && machine_state_load(fresh, state, n) == MACHINE_OK;
            free(state);
            machine_destroy(g);
            g = fresh;
            if (!ok) {
                machine_destroy(g);
                return NULL;
            }
        }
        machine_set_keys(g, 0);
        machine_run_frame(g);
        size_t n;
        while ((n = machine_audio_read(g, buf, 1024)) > 0) audio_hash = audio_hash_update(audio_hash, buf, n * 2);
    }
    free(hash_before);
    char out[40];
    snprintf(out, sizeof out, "%016" PRIx64 " %016" PRIx64, frame_hash(machine_framebuffer(g), (size_t)machine_width(g) * (size_t)machine_height(g)), audio_hash);
    machine_destroy(g);
    return (*env)->NewStringUTF(env, out);
}

JNIEXPORT jstring JNICALL JNI_FN(debugRunFrames)(JNIEnv *env, jobject self, jobject rom, jint frames) {
    (void)self;
    return run_headless(env, rom, frames, -1, -1, 0);
}

JNIEXPORT jstring JNICALL JNI_FN(debugRunFramesViaState)(JNIEnv *env, jobject self, jobject rom, jint frames,
                                                         jint state_at) {
    (void)self;
    return run_headless(env, rom, frames, state_at, -1, 0);
}

JNIEXPORT jstring JNICALL JNI_FN(debugRunFramesViaRewind)(JNIEnv *env, jobject self, jobject rom, jint frames,
                                                          jint rewind_at, jint rewind_steps) {
    (void)self;
    return run_headless(env, rom, frames, -1, rewind_at, rewind_steps);
}

/* Draws one frame offscreen with the real OpenGL code, round it the colour background (0xAARRGGBB); RGBA pixels top
 * row first, or null. */
JNIEXPORT jintArray JNICALL JNI_FN(debugDrawFrameOn)(JNIEnv *env, jobject self, jshortArray pixels, jint width,
                                                     jint height, jint fit_mode, jboolean smooth, jint background) {
    (void)self;
    jsize count = (*env)->GetArrayLength(env, pixels); /* a Game Boy Advance frame or a Game Boy one */
    int fw = count == 160 * 144 ? 160 : 240, fh = count == 160 * 144 ? 144 : 160;
    if (count != fw * fh || width <= 0 || height <= 0) return NULL;
    jshort *px = (*env)->GetShortArrayElements(env, pixels, NULL);
    uint32_t *out = malloc((size_t)width * (size_t)height * 4);
    bool ok = out && gl_readback((const uint16_t *)px, fw, fh, width, height, fit_mode, smooth == JNI_TRUE, (uint32_t)background, out);
    (*env)->ReleaseShortArrayElements(env, pixels, px, JNI_ABORT);
    jintArray result = NULL;
    if (ok) {
        result = (*env)->NewIntArray(env, width * height);
        (*env)->SetIntArrayRegion(env, result, 0, width * height, (const jint *)out);
    }
    free(out);
    return result;
}

/* As debugDrawFrameOn on black. */
JNIEXPORT jintArray JNICALL JNI_FN(debugDrawFrameWith)(JNIEnv *env, jobject self, jshortArray pixels, jint width,
                                                       jint height, jint fit_mode, jboolean smooth) {
    return JNI_FN(debugDrawFrameOn)(env, self, pixels, width, height, fit_mode, smooth, (jint)0xFF000000u);
}

/* As debugDrawFrameWith with the picture as in 4a (fit, sharp). */
JNIEXPORT jintArray JNICALL JNI_FN(debugDrawFrame)(JNIEnv *env, jobject self, jshortArray pixels, jint width,
                                                   jint height) {
    return JNI_FN(debugDrawFrameWith)(env, self, pixels, width, height, 0, JNI_FALSE);
}

/* The loaded game's save type (gba_save_type_t) and extras (GBA_HW_*), detected or forced; -1 without a game. */
JNIEXPORT jintArray JNICALL JNI_FN(cartSetup)(JNIEnv *env, jobject self) {
    (void)self;
    jint v[2] = {-1, -1};
    if (s_emu) {
        v[0] = (jint)emu_save_type(s_emu);
        v[1] = (jint)emu_cart_hardware(s_emu);
    }
    jintArray out = (*env)->NewIntArray(env, 2);
    (*env)->SetIntArrayRegion(env, out, 0, 2, v);
    return out;
}
