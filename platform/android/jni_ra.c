/* JNI entry points for RetroAchievements (milestone 14): NativeBridge's ra* functions over platform/ra. One ra_t for
 * the app's life. raWaitRequest blocks on the Kotlin request thread; the rest come from the UI thread (or a test
 * thread). Text goes both ways as UTF-8 bytes (Java's own "modified UTF-8" would break emoji in titles). */
#include <android/log.h>
#include <jni.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jni_shared.h"

#define JNI_FN(name) Java_com_tengen_gbaemu_NativeBridge_##name

static ra_t *s_ra;
static pthread_once_t s_once = PTHREAD_ONCE_INIT;

static void log_line(const char *line) {
    __android_log_print(ANDROID_LOG_WARN, "gbaemu-ra", "%s", line);
}

static void make_ra(void) {
    s_ra = ra_create();
    if (s_ra) ra_set_log(s_ra, log_line);
}

ra_t *jni_ra(void) {
    pthread_once(&s_once, make_ra);
    return s_ra;
}

static jbyteArray bytes_of(JNIEnv *env, const char *s) {
    jsize n = (jsize)strlen(s);
    jbyteArray a = (*env)->NewByteArray(env, n);
    if (a) (*env)->SetByteArrayRegion(env, a, 0, n, (const jbyte *)s);
    return a;
}

static jobjectArray byte_arrays(JNIEnv *env, const char *const *v, int n) {
    jclass cls = (*env)->FindClass(env, "[B");
    jobjectArray out = cls ? (*env)->NewObjectArray(env, n, cls, NULL) : NULL;
    for (int i = 0; out && i < n; i++) {
        jbyteArray b = bytes_of(env, v[i] ? v[i] : "");
        (*env)->SetObjectArrayElement(env, out, i, b);
        (*env)->DeleteLocalRef(env, b);
    }
    if (cls) (*env)->DeleteLocalRef(env, cls);
    return out;
}

/* A Kotlin ByteArray as a C string (free it), or NULL. */
static char *text_of(JNIEnv *env, jbyteArray a) {
    if (!a) return NULL;
    jsize n = (*env)->GetArrayLength(env, a);
    char *s = malloc((size_t)n + 1);
    if (!s) return NULL;
    (*env)->GetByteArrayRegion(env, a, 0, n, (jbyte *)s);
    s[n] = '\0';
    return s;
}

static jintArray ints(JNIEnv *env, const jint *v, int n) {
    jintArray out = (*env)->NewIntArray(env, n);
    if (out) (*env)->SetIntArrayRegion(env, out, 0, n, v);
    return out;
}

/* [id, url, post, content type] (post and type null for a GET), or null after timeout_ms without one. */
JNIEXPORT jobjectArray JNICALL JNI_FN(raWaitRequest)(JNIEnv *env, jobject self, jint timeout_ms) {
    (void)self;
    ra_t *ra = jni_ra();
    ra_request_t r;
    if (!ra || !ra_take_request(ra, timeout_ms, &r)) return NULL;
    jclass str = (*env)->FindClass(env, "java/lang/String");
    jobjectArray out = str ? (*env)->NewObjectArray(env, 4, str, NULL) : NULL;
    char id[16];
    snprintf(id, sizeof id, "%d", r.id);
    const char *v[4] = {id, r.url, r.post, r.content_type}; /* URLs and form data: plain ASCII */
    for (int i = 0; out && i < 4; i++) {
        if (!v[i]) continue;
        jstring s = (*env)->NewStringUTF(env, v[i]);
        (*env)->SetObjectArrayElement(env, out, i, s);
        (*env)->DeleteLocalRef(env, s);
    }
    if (str) (*env)->DeleteLocalRef(env, str);
    if (!out) ra_reply(ra, r.id, RA_NO_REPLY, NULL, 0);
    ra_request_free(&r);
    return out;
}

JNIEXPORT void JNICALL JNI_FN(raReply)(JNIEnv *env, jobject self, jint id, jint status, jbyteArray body) {
    (void)self;
    ra_t *ra = jni_ra();
    if (!ra) return;
    jsize n = body ? (*env)->GetArrayLength(env, body) : 0;
    jbyte *b = body ? (*env)->GetByteArrayElements(env, body, NULL) : NULL;
    ra_reply(ra, id, b || !body ? status : RA_NO_REPLY, (const char *)b, (size_t)n);
    if (b) (*env)->ReleaseByteArrayElements(env, body, b, JNI_ABORT);
}

JNIEXPORT void JNICALL JNI_FN(raLoginPassword)(JNIEnv *env, jobject self, jbyteArray user, jbyteArray password) {
    (void)self;
    char *u = text_of(env, user), *p = text_of(env, password);
    if (jni_ra() && u && p) ra_login_password(jni_ra(), u, p);
    if (p) memset(p, 0, strlen(p)); /* the password is not kept */
    free(u);
    free(p);
}

JNIEXPORT void JNICALL JNI_FN(raLoginToken)(JNIEnv *env, jobject self, jbyteArray user, jbyteArray token) {
    (void)self;
    char *u = text_of(env, user), *t = text_of(env, token);
    if (jni_ra() && u && t) ra_login_token(jni_ra(), u, t);
    free(u);
    free(t);
}

JNIEXPORT void JNICALL JNI_FN(raLogout)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    if (jni_ra()) ra_logout(jni_ra());
}

/* [status (ra_status_t), code]. */
JNIEXPORT jintArray JNICALL JNI_FN(raLoginStatus)(JNIEnv *env, jobject self) {
    (void)self;
    int code = 0;
    jint v[2] = {jni_ra() ? (jint)ra_login_status(jni_ra(), &code, NULL, 0) : RA_NOTHING, 0};
    v[1] = code;
    return ints(env, v, 2);
}

JNIEXPORT jbyteArray JNICALL JNI_FN(raLoginMessage)(JNIEnv *env, jobject self) {
    (void)self;
    char message[256] = "";
    if (jni_ra()) ra_login_status(jni_ra(), NULL, message, sizeof message);
    return bytes_of(env, message);
}

/* [name, token], or null when signed out. */
JNIEXPORT jobjectArray JNICALL JNI_FN(raUser)(JNIEnv *env, jobject self) {
    (void)self;
    char name[128], token[128];
    if (!jni_ra() || !ra_user(jni_ra(), name, sizeof name, token, sizeof token)) return NULL;
    const char *v[2] = {name, token};
    jobjectArray out = byte_arrays(env, v, 2);
    memset(token, 0, sizeof token);
    return out;
}

/* Identifies the game (the ROM's bytes, as given to loadRom) and starts loading its achievements. */
JNIEXPORT void JNICALL JNI_FN(raLoadGame)(JNIEnv *env, jobject self, jobject rom) {
    (void)self;
    const uint8_t *data = (*env)->GetDirectBufferAddress(env, rom);
    jlong size = (*env)->GetDirectBufferCapacity(env, rom);
    if (!jni_ra() || !data || size <= 0) return;
    char hash[33];
    ra_md5_hex(data, (size_t)size, hash);
    ra_load_game(jni_ra(), hash);
}

JNIEXPORT void JNICALL JNI_FN(raUnloadGame)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    if (jni_ra()) ra_unload_game(jni_ra());
}

/* While waiting for a game load: lets it finish (ra_idle with the game's memory, when the core is free). */
JNIEXPORT void JNICALL JNI_FN(raPoll)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    if (!jni_ra()) return;
    if (jni_emu()) {
        emu_ra_idle(jni_emu());
    } else {
        ra_memory_t none = {NULL, NULL, NULL, 0};
        ra_idle(jni_ra(), &none);
    }
}

JNIEXPORT jintArray JNICALL JNI_FN(raGameStatus)(JNIEnv *env, jobject self) {
    (void)self;
    int code = 0;
    jint v[2] = {jni_ra() ? (jint)ra_game_status(jni_ra(), &code) : RA_NOTHING, 0};
    v[1] = code;
    return ints(env, v, 2);
}

JNIEXPORT void JNICALL JNI_FN(raSetHardcore)(JNIEnv *env, jobject self, jboolean on) {
    (void)env;
    (void)self;
    if (jni_ra()) ra_set_hardcore(jni_ra(), on == JNI_TRUE);
}

JNIEXPORT jboolean JNICALL JNI_FN(raHardcoreActive)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    return jni_ra() && ra_hardcore_active(jni_ra()) ? JNI_TRUE : JNI_FALSE;
}

/* 0: the player may pause now; else frames to play first (hardcore). */
JNIEXPORT jint JNICALL JNI_FN(raCanPause)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    uint32_t left = 0;
    if (!jni_ra() || ra_can_pause(jni_ra(), &left)) return 0;
    return left ? (jint)left : 1;
}

/* [kind, id, points, title, description, progress, badge url, rank, entries, best] as UTF-8, or null when none is
 * waiting. */
JNIEXPORT jobjectArray JNICALL JNI_FN(raTakeEvent)(JNIEnv *env, jobject self) {
    (void)self;
    ra_event_t e;
    if (!jni_ra() || !ra_take_event(jni_ra(), &e)) return NULL;
    char kind[16], id[16], points[16], rank[16], entries[16];
    snprintf(kind, sizeof kind, "%d", e.kind);
    snprintf(id, sizeof id, "%u", (unsigned)e.id);
    snprintf(points, sizeof points, "%u", (unsigned)e.points);
    snprintf(rank, sizeof rank, "%u", (unsigned)e.rank);
    snprintf(entries, sizeof entries, "%u", (unsigned)e.entries);
    const char *v[10] = {kind, id, points, e.title, e.description, e.progress, e.badge_url, rank, entries, e.best};
    return byte_arrays(env, v, 10);
}

/* [unlocked, total, points unlocked, points total], or null without a game with achievements. */
JNIEXPORT jintArray JNICALL JNI_FN(raSummary)(JNIEnv *env, jobject self) {
    (void)self;
    ra_summary_t s;
    if (!jni_ra() || !ra_summary(jni_ra(), &s)) return NULL;
    jint v[4] = {(jint)s.unlocked, (jint)s.total, (jint)s.points_unlocked, (jint)s.points_total};
    return ints(env, v, 4);
}

JNIEXPORT jbyteArray JNICALL JNI_FN(raGameTitle)(JNIEnv *env, jobject self) {
    (void)self;
    ra_summary_t s;
    if (!jni_ra() || !ra_summary(jni_ra(), &s)) return bytes_of(env, "");
    return bytes_of(env, s.title);
}

/* 7 UTF-8 texts per achievement, unlocked first: id, points, unlocked (1/0), title, description, progress, badge url. */
JNIEXPORT jobjectArray JNICALL JNI_FN(raAchievements)(JNIEnv *env, jobject self) {
    (void)self;
    ra_achievement_t *list = malloc(sizeof(ra_achievement_t) * RA_LIST_MAX);
    size_t n = list && jni_ra() ? ra_achievements(jni_ra(), list, RA_LIST_MAX) : 0;
    const char **v = calloc(n * 7 + 1, sizeof *v);
    char (*num)[16] = calloc(n * 3 + 1, sizeof *num);
    jobjectArray out;
    if (!v || !num) {
        out = byte_arrays(env, NULL, 0);
    } else {
        for (size_t i = 0; i < n; i++) {
            snprintf(num[3 * i], 16, "%u", (unsigned)list[i].id);
            snprintf(num[3 * i + 1], 16, "%u", (unsigned)list[i].points);
            snprintf(num[3 * i + 2], 16, "%d", list[i].unlocked ? 1 : 0);
            const char *row[7] = {num[3 * i], num[3 * i + 1], num[3 * i + 2], list[i].title, list[i].description,
                                  list[i].progress, list[i].badge_url};
            memcpy(&v[7 * i], row, sizeof row);
        }
        out = byte_arrays(env, v, (int)(n * 7));
    }
    free(num);
    free(v);
    free(list);
    return out;
}

JNIEXPORT jstring JNICALL JNI_FN(raUserAgentClause)(JNIEnv *env, jobject self) {
    (void)self;
    char clause[64] = "";
    if (jni_ra()) ra_user_agent_clause(jni_ra(), clause, sizeof clause);
    return (*env)->NewStringUTF(env, clause);
}

/* ---- leaderboards (milestone 15) ---- */

JNIEXPORT jboolean JNICALL JNI_FN(raHasLeaderboards)(JNIEnv *env, jobject self) {
    (void)env;
    (void)self;
    return jni_ra() && ra_has_leaderboards(jni_ra()) ? JNI_TRUE : JNI_FALSE;
}

/* 4 UTF-8 texts per board: id, lower is better (1/0), title, description. */
JNIEXPORT jobjectArray JNICALL JNI_FN(raLeaderboards)(JNIEnv *env, jobject self) {
    (void)self;
    ra_board_t *list = malloc(sizeof(ra_board_t) * RA_BOARDS_MAX);
    size_t n = list && jni_ra() ? ra_leaderboards(jni_ra(), list, RA_BOARDS_MAX) : 0;
    const char **v = calloc(n * 4 + 1, sizeof *v);
    char (*num)[16] = calloc(n * 2 + 1, sizeof *num);
    jobjectArray out;
    if (!v || !num) {
        out = byte_arrays(env, NULL, 0);
    } else {
        for (size_t i = 0; i < n; i++) {
            snprintf(num[2 * i], 16, "%u", (unsigned)list[i].id);
            snprintf(num[2 * i + 1], 16, "%d", list[i].lower_is_better ? 1 : 0);
            const char *row[4] = {num[2 * i], num[2 * i + 1], list[i].title, list[i].description};
            memcpy(&v[4 * i], row, sizeof row);
        }
        out = byte_arrays(env, v, (int)(n * 4));
    }
    free(num);
    free(v);
    free(list);
    return out;
}

JNIEXPORT void JNICALL JNI_FN(raFetchEntries)(JNIEnv *env, jobject self, jint board) {
    (void)env;
    (void)self;
    if (jni_ra()) ra_fetch_entries(jni_ra(), (uint32_t)board);
}

/* [status (ra_status_t), code, total players, on the board (1/0)]. */
JNIEXPORT jintArray JNICALL JNI_FN(raEntriesStatus)(JNIEnv *env, jobject self) {
    (void)self;
    int code = 0;
    uint32_t total = 0;
    bool on = false;
    jint status = jni_ra() ? (jint)ra_entries_status(jni_ra(), &code, NULL, 0, &total, &on) : RA_NOTHING;
    jint v[4] = {status, code, (jint)total, on ? 1 : 0};
    return ints(env, v, 4);
}

JNIEXPORT jbyteArray JNICALL JNI_FN(raEntriesMessage)(JNIEnv *env, jobject self) {
    (void)self;
    char message[256] = "";
    if (jni_ra()) ra_entries_status(jni_ra(), NULL, message, sizeof message, NULL, NULL);
    return bytes_of(env, message);
}

/* 5 UTF-8 texts per row, best first: rank, the user's (1/0), a gap before it (1/0), name, score. */
JNIEXPORT jobjectArray JNICALL JNI_FN(raEntries)(JNIEnv *env, jobject self) {
    (void)self;
    ra_entry_t rows[RA_ENTRIES_MAX];
    size_t n = jni_ra() ? ra_entries(jni_ra(), rows, RA_ENTRIES_MAX) : 0;
    const char *v[RA_ENTRIES_MAX * 5];
    char num[RA_ENTRIES_MAX][3][16];
    for (size_t i = 0; i < n; i++) {
        snprintf(num[i][0], 16, "%u", (unsigned)rows[i].rank);
        snprintf(num[i][1], 16, "%d", rows[i].user ? 1 : 0);
        snprintf(num[i][2], 16, "%d", rows[i].gap_before ? 1 : 0);
        const char *row[5] = {num[i][0], num[i][1], num[i][2], rows[i].name, rows[i].score};
        memcpy(&v[5 * i], row, sizeof row);
    }
    return byte_arrays(env, v, (int)(n * 5));
}
