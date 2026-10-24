/* RetroAchievements glue (milestone 14): rcheevos' rc_client with a request queue, an event queue and the GBA's
 * memory. See ra.h. */
#define _POSIX_C_SOURCE 200809L
#include "ra.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "md5.h"
#include "rc_client.h"

_Static_assert(RA_NO_REPLY == RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR, "RA_NO_REPLY is rcheevos' retryable error");

typedef struct pending {
    int id;
    bool taken; /* handed out by ra_take_request: waiting for ra_reply */
    char *url, *post, *content_type;
    rc_client_server_callback_t callback;
    void *callback_data;
    struct pending *next;
} pending_t;

struct ra {
    rc_client_t *client;
    pthread_mutex_t lock;  /* guards the fields below (rc_client locks itself) */
    pthread_cond_t queued; /* a request was queued */
    pending_t *pending;    /* oldest first */
    int next_id;
    ra_event_t events[RA_EVENTS_MAX]; /* a ring; the oldest at events[event_start] */
    size_t event_start, event_count;
    ra_status_t login, game;
    int login_code, game_code;
    unsigned game_generation; /* only the newest ra_load_game's answer counts */
    char login_message[256];
    /* the newest ra_fetch_entries (milestone 15), guarded by lock */
    ra_status_t entries_status;
    int entries_code;
    char entries_message[256];
    unsigned entries_generation;
    int entries_waiting;                              /* answers still to come */
    rc_client_leaderboard_entry_list_t *top, *around; /* kept until both are in */
    ra_entry_t entries[RA_ENTRIES_MAX];
    size_t entry_count;
    uint32_t entries_total;
    bool on_board;
    const ra_memory_t *mem;  /* set only inside ra_do_frame / ra_idle / ra_reset_progress */
    uint64_t (*now_ms)(void);
    void (*log)(const char *line);
};

static char *copy_text(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *c = malloc(n);
    if (c) memcpy(c, s, n);
    return c;
}

static void put_text(char *dst, size_t cap, const char *src) {
    if (cap) snprintf(dst, cap, "%s", src ? src : "");
}

static void free_pending(pending_t *p) {
    if (!p) return;
    free(p->url);
    free(p->post);
    free(p->content_type);
    free(p);
}

/* (lock held) The score fetch's two lists, while waiting for both. */
static void drop_lists(ra_t *ra) {
    if (ra->top) rc_client_destroy_leaderboard_entry_list(ra->top);
    if (ra->around) rc_client_destroy_leaderboard_entry_list(ra->around);
    ra->top = ra->around = NULL;
}

/* (lock held) Forgets the score fetch: an answer still on its way no longer counts. */
static void forget_entries(ra_t *ra) {
    ra->entries_generation++;
    ra->entries_status = RA_NOTHING;
    ra->entries_code = 0;
    ra->entries_message[0] = '\0';
    ra->entries_waiting = 0;
    ra->entry_count = 0;
    ra->entries_total = 0;
    ra->on_board = false;
    drop_lists(ra);
}

/* ---- requests ---- */

static void server_call(const rc_api_request_t *request, rc_client_server_callback_t callback, void *callback_data,
                        rc_client_t *client) {
    ra_t *ra = rc_client_get_userdata(client);
    pending_t *p = calloc(1, sizeof *p);
    if (p) {
        p->url = copy_text(request->url);
        p->post = copy_text(request->post_data);
        p->content_type = copy_text(request->content_type);
    }
    if (!p || !p->url || (request->post_data && !p->post) || (request->content_type && !p->content_type)) {
        free_pending(p);
        rc_api_server_response_t none = {"", 0, RC_API_SERVER_RESPONSE_CLIENT_ERROR};
        callback(&none, callback_data); /* no memory: rcheevos gives this request up */
        return;
    }
    p->callback = callback;
    p->callback_data = callback_data;
    pthread_mutex_lock(&ra->lock);
    p->id = ++ra->next_id;
    pending_t **end = &ra->pending;
    while (*end) end = &(*end)->next;
    *end = p;
    pthread_cond_broadcast(&ra->queued);
    pthread_mutex_unlock(&ra->lock);
}

static pending_t *first_waiting(ra_t *ra) {
    pending_t *p = ra->pending;
    while (p && p->taken) p = p->next;
    return p;
}

bool ra_take_request(ra_t *ra, int timeout_ms, ra_request_t *out) {
    memset(out, 0, sizeof *out);
    pthread_mutex_lock(&ra->lock);
    pending_t *p = first_waiting(ra);
    if (!p && timeout_ms > 0) {
        struct timespec until;
        clock_gettime(CLOCK_REALTIME, &until);
        until.tv_sec += timeout_ms / 1000;
        until.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
        if (until.tv_nsec >= 1000000000L) {
            until.tv_sec++;
            until.tv_nsec -= 1000000000L;
        }
        while (!(p = first_waiting(ra))) {
            if (pthread_cond_timedwait(&ra->queued, &ra->lock, &until) == ETIMEDOUT) break;
        }
    }
    if (p) {
        out->url = copy_text(p->url);
        out->post = copy_text(p->post);
        out->content_type = copy_text(p->content_type);
        if (!out->url || (p->post && !out->post) || (p->content_type && !out->content_type)) {
            ra_request_free(out);
            p = NULL; /* no memory: it stays waiting for the next try */
        } else {
            p->taken = true;
            out->id = p->id;
        }
    }
    pthread_mutex_unlock(&ra->lock);
    return p != NULL;
}

void ra_request_free(ra_request_t *r) {
    free(r->url);
    free(r->post);
    free(r->content_type);
    r->url = r->post = r->content_type = NULL;
}

void ra_reply(ra_t *ra, int id, int status, const char *body, size_t len) {
    pthread_mutex_lock(&ra->lock);
    pending_t **link = &ra->pending;
    while (*link && (*link)->id != id) link = &(*link)->next;
    pending_t *p = *link;
    if (p) *link = p->next;
    pthread_mutex_unlock(&ra->lock);
    if (!p) return;
    char *text = malloc(len + 1); /* rcheevos reads the body as text */
    if (text) {
        if (len) memcpy(text, body, len);
        text[len] = '\0';
    }
    rc_api_server_response_t r = {text ? text : "", text ? len : 0, text ? status : RA_NO_REPLY};
    p->callback(&r, p->callback_data);
    free(text);
    free_pending(p);
}

/* ---- signing in ---- */

static void login_done(int result, const char *error_message, rc_client_t *client, void *userdata) {
    (void)userdata;
    ra_t *ra = rc_client_get_userdata(client);
    pthread_mutex_lock(&ra->lock);
    ra->login = result == RC_OK ? RA_OK : RA_FAILED;
    ra->login_code = result;
    put_text(ra->login_message, sizeof ra->login_message, result == RC_OK ? "" : error_message);
    pthread_mutex_unlock(&ra->lock);
}

static void login_starts(ra_t *ra) {
    pthread_mutex_lock(&ra->lock);
    ra->login = RA_BUSY;
    ra->login_code = 0;
    ra->login_message[0] = '\0';
    pthread_mutex_unlock(&ra->lock);
}

void ra_login_password(ra_t *ra, const char *user, const char *password) {
    login_starts(ra);
    rc_client_begin_login_with_password(ra->client, user, password, login_done, NULL);
}

void ra_login_token(ra_t *ra, const char *user, const char *token) {
    login_starts(ra);
    rc_client_begin_login_with_token(ra->client, user, token, login_done, NULL);
}

void ra_logout(ra_t *ra) {
    rc_client_logout(ra->client);
    pthread_mutex_lock(&ra->lock);
    ra->login = RA_NOTHING;
    ra->login_code = 0;
    ra->game = RA_NOTHING;
    ra->game_code = 0;
    ra->game_generation++;
    forget_entries(ra);
    pthread_mutex_unlock(&ra->lock);
}

ra_status_t ra_login_status(ra_t *ra, int *code, char *message, size_t cap) {
    pthread_mutex_lock(&ra->lock);
    ra_status_t status = ra->login;
    if (code) *code = ra->login_code;
    if (message) put_text(message, cap, ra->login_message);
    pthread_mutex_unlock(&ra->lock);
    return status;
}

bool ra_user(ra_t *ra, char *name, size_t name_cap, char *token, size_t token_cap) {
    const rc_client_user_t *u = rc_client_get_user_info(ra->client);
    if (!u) return false;
    put_text(name, name_cap, u->username);
    put_text(token, token_cap, u->token);
    return true;
}

/* ---- the game ---- */

void ra_md5_hex(const uint8_t *data, size_t size, char out[33]) {
    md5_state_t st;
    md5_byte_t digest[16];
    md5_init(&st);
    if (size > RA_HASH_MAX) size = RA_HASH_MAX;
    md5_append(&st, data, (int)size);
    md5_finish(&st, digest);
    for (int i = 0; i < 16; i++) snprintf(out + 2 * i, 3, "%02x", digest[i]);
}

uint32_t ra_peek(const ra_memory_t *mem, uint32_t address, uint8_t *buffer, uint32_t count) {
    if (!mem) return 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t a = address + i;
        const uint8_t *p = NULL;
        if (a < RA_EWRAM_AT) p = mem->iwram ? mem->iwram + a : NULL;
        else if (a < RA_SRAM_AT) p = mem->ewram ? mem->ewram + (a - RA_EWRAM_AT) : NULL;
        else if (a < RA_SRAM_END && a - RA_SRAM_AT < mem->sram_size) p = mem->sram ? mem->sram + (a - RA_SRAM_AT) : NULL;
        if (!p) return i;
        buffer[i] = *p;
    }
    return count;
}

static uint32_t read_memory(uint32_t address, uint8_t *buffer, uint32_t num_bytes, rc_client_t *client) {
    ra_t *ra = rc_client_get_userdata(client);
    return ra_peek(ra->mem, address, buffer, num_bytes);
}

static void game_done(int result, const char *error_message, rc_client_t *client, void *userdata) {
    (void)error_message;
    ra_t *ra = rc_client_get_userdata(client);
    pthread_mutex_lock(&ra->lock);
    if ((unsigned)(uintptr_t)userdata == ra->game_generation) {
        ra->game = result == RC_OK ? RA_OK : RA_FAILED;
        ra->game_code = result;
    }
    pthread_mutex_unlock(&ra->lock);
}

void ra_load_game(ra_t *ra, const char *md5_hex) {
    pthread_mutex_lock(&ra->lock);
    unsigned generation = ++ra->game_generation;
    ra->game = RA_BUSY;
    ra->game_code = 0;
    pthread_mutex_unlock(&ra->lock);
    rc_client_begin_load_game(ra->client, md5_hex, game_done, (void *)(uintptr_t)generation);
}

void ra_unload_game(ra_t *ra) {
    pthread_mutex_lock(&ra->lock);
    ra->game_generation++; /* a load still on its way no longer counts */
    ra->game = RA_NOTHING;
    ra->game_code = 0;
    forget_entries(ra);
    pthread_mutex_unlock(&ra->lock);
    rc_client_unload_game(ra->client);
}

ra_status_t ra_game_status(ra_t *ra, int *code) {
    pthread_mutex_lock(&ra->lock);
    ra_status_t status = ra->game;
    if (code) *code = ra->game_code;
    pthread_mutex_unlock(&ra->lock);
    return status;
}

bool ra_has_set(ra_t *ra) {
    return ra_game_status(ra, NULL) == RA_OK && rc_client_has_achievements(ra->client);
}

void ra_do_frame(ra_t *ra, const ra_memory_t *mem) {
    ra->mem = mem;
    rc_client_do_frame(ra->client);
    ra->mem = NULL;
}

void ra_idle(ra_t *ra, const ra_memory_t *mem) {
    ra->mem = mem;
    rc_client_idle(ra->client);
    ra->mem = NULL;
}

void ra_reset_progress(ra_t *ra, const ra_memory_t *mem) {
    ra->mem = mem;
    rc_client_deserialize_progress_sized(ra->client, NULL, 0); /* NULL: every achievement back to its start */
    ra->mem = NULL;
}

/* ---- hardcore ---- */

void ra_set_hardcore(ra_t *ra, bool on) {
    rc_client_set_hardcore_enabled(ra->client, on ? 1 : 0);
}

bool ra_hardcore_active(ra_t *ra) {
    return rc_client_get_hardcore_enabled(ra->client) && rc_client_get_user_info(ra->client) && ra_has_set(ra);
}

bool ra_can_pause(ra_t *ra, uint32_t *frames_left) {
    if (frames_left) *frames_left = 0;
    if (!ra_hardcore_active(ra)) return true;
    return rc_client_can_pause(ra->client, frames_left) != 0;
}

/* ---- events ---- */

/* Events that may come every frame: a waiting one is replaced by a newer one of the same sort, so a run of them never
 * pushes an unlock, a challenge or a leaderboard note out of the queue. */
static bool replaces(const ra_event_t *waiting, const ra_event_t *e) {
    bool progress = e->kind == RA_EVENT_PROGRESS || e->kind == RA_EVENT_PROGRESS_HIDE;
    if (progress) return waiting->kind == RA_EVENT_PROGRESS || waiting->kind == RA_EVENT_PROGRESS_HIDE;
    return e->kind == RA_EVENT_TRACKER_UPDATE && waiting->kind == RA_EVENT_TRACKER_UPDATE && waiting->id == e->id;
}

static void push_event(ra_t *ra, const ra_event_t *e) {
    pthread_mutex_lock(&ra->lock);
    for (size_t i = 0; i < ra->event_count; i++) {
        ra_event_t *q = &ra->events[(ra->event_start + i) % RA_EVENTS_MAX];
        if (replaces(q, e)) {
            *q = *e;
            pthread_mutex_unlock(&ra->lock);
            return;
        }
    }
    if (ra->event_count == RA_EVENTS_MAX) { /* full: the oldest goes */
        ra->event_start = (ra->event_start + 1) % RA_EVENTS_MAX;
        ra->event_count--;
    }
    ra->events[(ra->event_start + ra->event_count) % RA_EVENTS_MAX] = *e;
    ra->event_count++;
    pthread_mutex_unlock(&ra->lock);
}

bool ra_take_event(ra_t *ra, ra_event_t *out) {
    pthread_mutex_lock(&ra->lock);
    bool any = ra->event_count > 0;
    if (any) {
        *out = ra->events[ra->event_start];
        ra->event_start = (ra->event_start + 1) % RA_EVENTS_MAX;
        ra->event_count--;
    }
    pthread_mutex_unlock(&ra->lock);
    return any;
}

static void on_event(const rc_client_event_t *event, rc_client_t *client) {
    ra_t *ra = rc_client_get_userdata(client);
    ra_event_t e;
    memset(&e, 0, sizeof e);
    const rc_client_achievement_t *a = event->achievement;
    const rc_client_leaderboard_t *lb = NULL;
    switch (event->type) {
    case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED: e.kind = RA_EVENT_UNLOCKED; break;
    case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_SHOW:
    case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_UPDATE: e.kind = RA_EVENT_PROGRESS; break;
    case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_HIDE:
        e.kind = RA_EVENT_PROGRESS_HIDE;
        a = NULL;
        break;
    case RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_SHOW: e.kind = RA_EVENT_CHALLENGE_SHOW; break;
    case RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_HIDE: e.kind = RA_EVENT_CHALLENGE_HIDE; break;
    case RC_CLIENT_EVENT_GAME_COMPLETED: {
        e.kind = RA_EVENT_MASTERED;
        const rc_client_game_t *g = rc_client_get_game_info(client);
        rc_client_user_game_summary_t s;
        rc_client_get_user_game_summary(client, &s);
        e.points = s.points_core;
        put_text(e.title, sizeof e.title, g ? g->title : "");
        put_text(e.badge_url, sizeof e.badge_url, g ? g->badge_url : "");
        a = NULL;
        break;
    }
    case RC_CLIENT_EVENT_SERVER_ERROR:
        e.kind = RA_EVENT_SERVER_ERROR;
        if (event->server_error) {
            e.id = event->server_error->related_id;
            put_text(e.description, sizeof e.description, event->server_error->error_message);
            if (event->server_error->api && !strcmp(event->server_error->api, "submit_lboard_entry")) {
                e.kind = RA_EVENT_LB_ERROR; /* a refused score: the board's title */
                lb = rc_client_get_leaderboard_info(client, e.id);
            }
        }
        a = NULL;
        break;
    case RC_CLIENT_EVENT_DISCONNECTED: e.kind = RA_EVENT_OFFLINE; break;
    case RC_CLIENT_EVENT_RECONNECTED: e.kind = RA_EVENT_ONLINE; break;
    case RC_CLIENT_EVENT_RESET: e.kind = RA_EVENT_RESET; break;
    case RC_CLIENT_EVENT_LEADERBOARD_STARTED: e.kind = RA_EVENT_LB_STARTED; lb = event->leaderboard; break;
    case RC_CLIENT_EVENT_LEADERBOARD_FAILED: e.kind = RA_EVENT_LB_FAILED; lb = event->leaderboard; break;
    case RC_CLIENT_EVENT_LEADERBOARD_SUBMITTED:
        e.kind = RA_EVENT_LB_SUBMITTED;
        lb = event->leaderboard;
        if (lb) put_text(e.progress, sizeof e.progress, lb->tracker_value);
        break;
    case RC_CLIENT_EVENT_LEADERBOARD_SCOREBOARD: {
        e.kind = RA_EVENT_LB_SCOREBOARD;
        lb = event->leaderboard;
        const rc_client_leaderboard_scoreboard_t *s = event->leaderboard_scoreboard; /* valid only in here */
        if (s) {
            put_text(e.progress, sizeof e.progress, s->submitted_score);
            put_text(e.best, sizeof e.best, s->best_score);
            e.rank = s->new_rank;
            e.entries = s->num_entries;
        }
        break;
    }
    case RC_CLIENT_EVENT_LEADERBOARD_TRACKER_SHOW:
    case RC_CLIENT_EVENT_LEADERBOARD_TRACKER_UPDATE:
    case RC_CLIENT_EVENT_LEADERBOARD_TRACKER_HIDE:
        e.kind = event->type == RC_CLIENT_EVENT_LEADERBOARD_TRACKER_SHOW     ? RA_EVENT_TRACKER_SHOW
                 : event->type == RC_CLIENT_EVENT_LEADERBOARD_TRACKER_UPDATE ? RA_EVENT_TRACKER_UPDATE
                                                                             : RA_EVENT_TRACKER_HIDE;
        if (event->leaderboard_tracker) {
            e.id = event->leaderboard_tracker->id;
            put_text(e.progress, sizeof e.progress, event->leaderboard_tracker->display);
        }
        break;
    default: return; /* subsets: not used */
    }
    if (lb) {
        e.id = lb->id;
        put_text(e.title, sizeof e.title, lb->title);
    }
    if (a) {
        e.id = a->id;
        e.points = a->points;
        put_text(e.title, sizeof e.title, a->title);
        put_text(e.description, sizeof e.description, a->description);
        put_text(e.progress, sizeof e.progress, a->measured_progress);
        put_text(e.badge_url, sizeof e.badge_url, a->badge_url);
    }
    push_event(ra, &e);
}

/* ---- the summary and the list ---- */

bool ra_summary(ra_t *ra, ra_summary_t *out) {
    memset(out, 0, sizeof *out);
    if (!ra_has_set(ra)) return false;
    rc_client_user_game_summary_t s;
    rc_client_get_user_game_summary(ra->client, &s);
    out->unlocked = s.num_unlocked_achievements;
    out->total = s.num_core_achievements;
    out->points_unlocked = s.points_unlocked;
    out->points_total = s.points_core;
    const rc_client_game_t *g = rc_client_get_game_info(ra->client);
    put_text(out->title, sizeof out->title, g ? g->title : "");
    return true;
}

size_t ra_achievements(ra_t *ra, ra_achievement_t *out, size_t max) {
    if (!ra_has_set(ra)) return 0;
    rc_client_achievement_list_t *list = rc_client_create_achievement_list(
        ra->client, RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE, RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_LOCK_STATE);
    if (!list) return 0;
    size_t n = 0;
    for (int pass = 0; pass < 2; pass++) { /* unlocked ones first, then the rest */
        for (uint32_t b = 0; b < list->num_buckets; b++) {
            for (uint32_t i = 0; i < list->buckets[b].num_achievements; i++) {
                const rc_client_achievement_t *a = list->buckets[b].achievements[i];
                bool unlocked = a->state == RC_CLIENT_ACHIEVEMENT_STATE_UNLOCKED;
                if (unlocked != (pass == 0) || n == max) continue;
                ra_achievement_t *o = &out[n++];
                memset(o, 0, sizeof *o);
                o->id = a->id;
                o->points = a->points;
                o->unlocked = unlocked;
                put_text(o->title, sizeof o->title, a->title);
                put_text(o->description, sizeof o->description, a->description);
                put_text(o->progress, sizeof o->progress, a->measured_progress);
                put_text(o->badge_url, sizeof o->badge_url, unlocked ? a->badge_url : a->badge_locked_url);
            }
        }
    }
    rc_client_destroy_achievement_list(list);
    return n;
}

/* ---- leaderboards ---- */

bool ra_has_leaderboards(ra_t *ra) {
    return ra_game_status(ra, NULL) == RA_OK && rc_client_has_leaderboards(ra->client);
}

size_t ra_leaderboards(ra_t *ra, ra_board_t *out, size_t max) {
    if (!ra_has_leaderboards(ra)) return 0;
    rc_client_leaderboard_list_t *list = rc_client_create_leaderboard_list(ra->client, RC_CLIENT_LEADERBOARD_LIST_GROUPING_NONE);
    if (!list) return 0;
    size_t n = 0;
    for (uint32_t b = 0; b < list->num_buckets; b++) {
        for (uint32_t i = 0; i < list->buckets[b].num_leaderboards && n < max; i++) {
            const rc_client_leaderboard_t *l = list->buckets[b].leaderboards[i];
            ra_board_t *o = &out[n++];
            memset(o, 0, sizeof *o);
            o->id = l->id;
            o->lower_is_better = l->lower_is_better != 0;
            put_text(o->title, sizeof o->title, l->title);
            put_text(o->description, sizeof o->description, l->description);
        }
    }
    rc_client_destroy_leaderboard_list(list);
    return n;
}

static void put_entry(ra_t *ra, const rc_client_leaderboard_entry_t *src, bool user, bool gap) {
    if (ra->entry_count == RA_ENTRIES_MAX) return;
    ra_entry_t *o = &ra->entries[ra->entry_count++];
    memset(o, 0, sizeof *o);
    o->rank = src->rank;
    o->user = user;
    o->gap_before = gap;
    put_text(o->name, sizeof o->name, src->user);
    put_text(o->score, sizeof o->score, src->display);
}

/* (lock held) The top rows, then the rows around the user that the top doesn't have (by their place on the board). */
static void merge_entries(ra_t *ra) {
    ra->entry_count = 0;
    ra->entries_total = 0;
    ra->on_board = false;
    uint32_t last = 0; /* the place (index, from 1) of the last row kept */
    const rc_client_leaderboard_entry_list_t *t = ra->top, *a = ra->around;
    if (t) {
        ra->entries_total = t->total_entries;
        ra->on_board = t->user_index >= 0;
        for (uint32_t i = 0; i < t->num_entries; i++) {
            put_entry(ra, &t->entries[i], (int)i == t->user_index, false);
            last = t->entries[i].index;
        }
    }
    if (a && a->user_index >= 0) { /* -1: the user has no score there (the server sent other rows) */
        ra->on_board = true;
        if (a->total_entries > ra->entries_total) ra->entries_total = a->total_entries;
        bool first = true;
        for (uint32_t i = 0; i < a->num_entries; i++) {
            if (a->entries[i].index <= last) continue;
            put_entry(ra, &a->entries[i], (int)i == a->user_index, first && a->entries[i].index > last + 1);
            first = false;
            last = a->entries[i].index;
        }
    }
}

/* One of the two answers; userdata = generation × 2 + (1 for the rows around the user). */
static void entries_done(int result, const char *error_message, rc_client_leaderboard_entry_list_t *list,
                         rc_client_t *client, void *userdata) {
    ra_t *ra = rc_client_get_userdata(client);
    uintptr_t tag = (uintptr_t)userdata;
    pthread_mutex_lock(&ra->lock);
    if ((unsigned)(tag >> 1) == ra->entries_generation && ra->entries_status == RA_BUSY) {
        if (result != RC_OK) {
            ra->entries_status = RA_FAILED;
            ra->entries_code = result;
            put_text(ra->entries_message, sizeof ra->entries_message, error_message);
            drop_lists(ra);
        } else {
            if (tag & 1) ra->around = list;
            else ra->top = list;
            list = NULL;
            if (--ra->entries_waiting == 0) {
                merge_entries(ra);
                drop_lists(ra);
                ra->entries_status = RA_OK;
            }
        }
    }
    pthread_mutex_unlock(&ra->lock);
    if (list) rc_client_destroy_leaderboard_entry_list(list); /* an older fetch's, or the other half failed */
}

void ra_fetch_entries(ra_t *ra, uint32_t board) {
    pthread_mutex_lock(&ra->lock);
    forget_entries(ra);
    unsigned generation = ra->entries_generation;
    ra->entries_status = RA_BUSY;
    ra->entries_waiting = 2;
    pthread_mutex_unlock(&ra->lock);
    /* Not under the lock: rcheevos queues the requests through server_call (which takes it), and may answer at once
     * (signed out: RC_LOGIN_REQUIRED). */
    rc_client_begin_fetch_leaderboard_entries(ra->client, board, 1, RA_TOP_ROWS, entries_done,
                                              (void *)(uintptr_t)(generation << 1));
    rc_client_begin_fetch_leaderboard_entries_around_user(ra->client, board, RA_AROUND_ROWS, entries_done,
                                                          (void *)(uintptr_t)((generation << 1) | 1));
}

ra_status_t ra_entries_status(ra_t *ra, int *code, char *message, size_t cap, uint32_t *total, bool *on_board) {
    pthread_mutex_lock(&ra->lock);
    ra_status_t status = ra->entries_status;
    if (code) *code = ra->entries_code;
    if (message) put_text(message, cap, ra->entries_message);
    if (total) *total = status == RA_OK ? ra->entries_total : 0;
    if (on_board) *on_board = status == RA_OK && ra->on_board;
    pthread_mutex_unlock(&ra->lock);
    return status;
}

size_t ra_entries(ra_t *ra, ra_entry_t *out, size_t max) {
    pthread_mutex_lock(&ra->lock);
    size_t n = ra->entries_status == RA_OK ? ra->entry_count : 0;
    if (n > max) n = max;
    if (n) memcpy(out, ra->entries, n * sizeof *out);
    pthread_mutex_unlock(&ra->lock);
    return n;
}

/* ---- set-up ---- */

static rc_clock_t clock_now(const rc_client_t *client) {
    ra_t *ra = rc_client_get_userdata(client);
    return ra->now_ms();
}

void ra_set_clock(ra_t *ra, uint64_t (*now_ms)(void)) {
    ra->now_ms = now_ms;
    rc_client_set_get_time_millisecs_function(ra->client, now_ms ? clock_now : NULL);
}

static void log_message(const char *message, const rc_client_t *client) {
    ra_t *ra = rc_client_get_userdata(client);
    if (ra->log) ra->log(message);
}

void ra_set_log(ra_t *ra, void (*log)(const char *line)) {
    ra->log = log;
    rc_client_enable_logging(ra->client, log ? RC_CLIENT_LOG_LEVEL_WARN : RC_CLIENT_LOG_LEVEL_NONE, log ? log_message : NULL);
}

size_t ra_user_agent_clause(ra_t *ra, char *buf, size_t cap) {
    return rc_client_get_user_agent_clause(ra->client, buf, cap);
}

ra_t *ra_create(void) {
    ra_t *ra = calloc(1, sizeof *ra);
    if (!ra) return NULL;
    ra->client = rc_client_create(read_memory, server_call);
    if (!ra->client) {
        free(ra);
        return NULL;
    }
    pthread_mutex_init(&ra->lock, NULL);
    pthread_cond_init(&ra->queued, NULL);
    rc_client_set_userdata(ra->client, ra);
    rc_client_set_allow_background_memory_reads(ra->client, 0); /* memory only inside ra_do_frame / ra_idle */
    rc_client_set_event_handler(ra->client, on_event);
    return ra;
}

void ra_destroy(ra_t *ra) {
    if (!ra) return;
    drop_lists(ra);
    rc_client_destroy(ra->client);
    for (pending_t *p = ra->pending, *next; p; p = next) {
        next = p->next;
        free_pending(p);
    }
    pthread_cond_destroy(&ra->queued);
    pthread_mutex_destroy(&ra->lock);
    free(ra);
}
