#ifndef GBAEMU_RA_H
#define GBAEMU_RA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* RetroAchievements for the app (milestone 14): rcheevos' rc_client with
 *  - a request queue: rcheevos' web requests wait here; whoever does the internet takes them (ra_take_request) and
 *    answers (ra_reply) — on Android a Kotlin thread, on the PC a pretend server (ra_fake.h);
 *  - an event queue: unlocks, progress, challenges… for the game screen (ra_take_event);
 *  - the GBA's memory, read only inside ra_do_frame / ra_idle / ra_reset_progress, which are given it each call.
 * Plain C (PC and Android). Every function may be called from any thread; the ones given memory must not run while
 * something else changes that memory. */
typedef struct ra ra_t;

typedef enum {
    RA_NOTHING, /* signed out / no game */
    RA_BUSY,    /* waiting for the server (or, for a game, for the next ra_idle / ra_do_frame) */
    RA_OK,
    RA_FAILED,  /* the code says why: rcheevos' RC_* (rc_error.h), e.g. RC_INVALID_CREDENTIALS, RC_NO_GAME_LOADED */
} ra_status_t;

ra_t *ra_create(void); /* hardcore on (rcheevos' default); NULL without memory */
void ra_destroy(ra_t *ra);
/* For tests: rcheevos' clock (milliseconds; retries and the pause limit use it). NULL = its own clock. */
void ra_set_clock(ra_t *ra, uint64_t (*now_ms)(void));
/* rcheevos' warnings and errors, one line each (never a password or token). NULL = none. */
void ra_set_log(ra_t *ra, void (*log)(const char *line));
/* "rcheevos/<version>", for the app's User-Agent. */
size_t ra_user_agent_clause(ra_t *ra, char *buf, size_t cap);

/* ---- requests ---- */
#define RA_NO_REPLY (-2) /* the request could not be sent or answered (= RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR) */
typedef struct {
    int id;
    char *url;          /* the whole URL */
    char *post;         /* the POST body (form data), or NULL: a GET */
    char *content_type; /* the POST body's type, or NULL */
} ra_request_t;
/* The oldest request nobody has taken, waiting up to timeout_ms for one (0: no wait): true and *out filled (free it
 * with ra_request_free), or false. */
bool ra_take_request(ra_t *ra, int timeout_ms, ra_request_t *out);
void ra_request_free(ra_request_t *r);
/* The answer to request id: its HTTP status (or RA_NO_REPLY) and body (len bytes, need not end in 0). An unknown id
 * is ignored. */
void ra_reply(ra_t *ra, int id, int status, const char *body, size_t len);

/* ---- signing in ---- */
void ra_login_password(ra_t *ra, const char *user, const char *password);
void ra_login_token(ra_t *ra, const char *user, const char *token);
void ra_logout(ra_t *ra); /* also unloads the game */
/* The last sign-in: RA_NOTHING (signed out), RA_BUSY, RA_OK, RA_FAILED (*code and the server's message). */
ra_status_t ra_login_status(ra_t *ra, int *code, char *message, size_t cap);
/* The signed-in user's name and login token (to keep for next time); false when signed out. */
bool ra_user(ra_t *ra, char *name, size_t name_cap, char *token, size_t token_cap);

/* ---- the game ---- */
#define RA_HASH_MAX (64u << 20) /* rcheevos hashes at most 64 MiB of a cartridge */
/* rcheevos' GBA hash: the MD5 of the ROM file's bytes, as 32 lowercase hex digits. */
void ra_md5_hex(const uint8_t *data, size_t size, char out[33]);
/* Identifies the game by its hash and loads its achievements (the user must be signed in or signing in). It is
 * RA_OK only after the next ra_idle / ra_do_frame that follows the server's answers. */
void ra_load_game(ra_t *ra, const char *md5_hex);
void ra_unload_game(ra_t *ra);
ra_status_t ra_game_status(ra_t *ra, int *code);
bool ra_has_set(ra_t *ra); /* a game is loaded and it has achievements */

/* The game's memory. A Game Boy Advance game: the three pointers, in RetroAchievements' GBA map — 0x00000-0x07FFF
 * IWRAM, 0x08000-0x47FFF EWRAM, 0x48000-0x57FFF save RAM (its first 64 KiB); NULL / 0 for what is not there. Any
 * other console (17c: Game Boy, Game Boy Color): `peek`, which answers in that console's own map (gb_ra_peek). */
#define RA_EWRAM_AT 0x8000u
#define RA_SRAM_AT 0x48000u
#define RA_SRAM_END 0x58000u
typedef struct {
    const uint8_t *iwram; /* GBA_IWRAM_SIZE bytes */
    const uint8_t *ewram; /* GBA_EWRAM_SIZE bytes */
    const uint8_t *sram;  /* the save chip's bytes */
    size_t sram_size;
    /* When set, used instead of the pointers: copies count bytes from address on, returns how many exist there. */
    uint32_t (*peek)(void *ctx, uint32_t address, uint8_t *buffer, uint32_t count);
    void *ctx;
} ra_memory_t;
/* Copies count bytes from address on; returns how many exist there (it stops at the first missing one). */
uint32_t ra_peek(const ra_memory_t *mem, uint32_t address, uint8_t *buffer, uint32_t count);

void ra_do_frame(ra_t *ra, const ra_memory_t *mem); /* after every emulated frame */
void ra_idle(ra_t *ra, const ra_memory_t *mem);     /* about once a second while paused; and while waiting for a load */
/* Forgets half-done achievement progress (after a state load or a rewind, in softcore): rcheevos' advice. */
void ra_reset_progress(ra_t *ra, const ra_memory_t *mem);

/* ---- hardcore ---- */
void ra_set_hardcore(ra_t *ra, bool on);
/* The hardcore limits apply: hardcore on, signed in, and the game has achievements. */
bool ra_hardcore_active(ra_t *ra);
/* Whether the player may pause now (the hardcore limit on quick repeated pauses); always true when the limits don't
 * apply. When false, *frames_left = frames to play first. */
bool ra_can_pause(ra_t *ra, uint32_t *frames_left);

/* ---- events and the list ---- */
typedef enum {
    RA_EVENT_UNLOCKED = 1,  /* id, title, description, points, badge_url */
    RA_EVENT_PROGRESS,      /* id, title, progress ("3/10"), badge_url: show or update the progress */
    RA_EVENT_PROGRESS_HIDE,
    RA_EVENT_CHALLENGE_SHOW, /* id, badge_url */
    RA_EVENT_CHALLENGE_HIDE, /* id */
    RA_EVENT_MASTERED,      /* title = the game, points = all its points, badge_url = the game's picture */
    RA_EVENT_SERVER_ERROR,  /* id (the achievement, or 0), description = the server's message */
    RA_EVENT_OFFLINE,       /* an unlock is waiting to be sent */
    RA_EVENT_ONLINE,        /* every waiting unlock has been sent */
    RA_EVENT_RESET,         /* rcheevos asks for a restart (hardcore switched on) */
    /* leaderboards (milestone 15; rcheevos runs them only in hardcore) */
    RA_EVENT_LB_STARTED,     /* id (the board), title */
    RA_EVENT_LB_FAILED,      /* id, title */
    RA_EVENT_LB_SUBMITTED,   /* id, title, progress = the score being sent */
    RA_EVENT_LB_SCOREBOARD,  /* id, title, progress = the score sent, best = the user's best, rank of entries */
    RA_EVENT_LB_ERROR,       /* id, title, description = the server's message (a refused score) */
    RA_EVENT_TRACKER_SHOW,   /* id (the counter), progress = its text */
    RA_EVENT_TRACKER_UPDATE, /* id, progress (only the newest waits) */
    RA_EVENT_TRACKER_HIDE,   /* id */
} ra_event_kind_t;
typedef struct {
    int kind;
    uint32_t id, points;
    uint32_t rank, entries; /* RA_EVENT_LB_SCOREBOARD */
    char title[128];
    char description[256];
    char progress[24];
    char best[24];          /* RA_EVENT_LB_SCOREBOARD */
    char badge_url[256];
} ra_event_t;
#define RA_EVENTS_MAX 32 /* a full queue drops its oldest event */
bool ra_take_event(ra_t *ra, ra_event_t *out);

typedef struct {
    uint32_t unlocked, total, points_unlocked, points_total; /* in the current mode (hardcore or softcore) */
    char title[128];                                         /* the game */
} ra_summary_t;
bool ra_summary(ra_t *ra, ra_summary_t *out); /* false: no game with achievements */

typedef struct {
    uint32_t id, points;
    bool unlocked;
    char title[128];
    char description[256];
    char progress[24];   /* "" when it has none */
    char badge_url[256]; /* the coloured picture when unlocked, the grey one when not */
} ra_achievement_t;
#define RA_LIST_MAX 512
/* The game's achievements, unlocked ones first; how many were written (at most max). */
size_t ra_achievements(ra_t *ra, ra_achievement_t *out, size_t max);

/* ---- leaderboards (milestone 15) ---- */
bool ra_has_leaderboards(ra_t *ra); /* a game is loaded and it has (visible) boards */
typedef struct {
    uint32_t id;
    bool lower_is_better;
    char title[128];
    char description[256];
} ra_board_t;
#define RA_BOARDS_MAX 256
size_t ra_leaderboards(ra_t *ra, ra_board_t *out, size_t max); /* the game's boards, in the set's order */

/* A board's scores: the top RA_TOP_ROWS, then the rows around the user that the top doesn't have. Two requests to the
 * server; only the newest ra_fetch_entries counts, and unloading the game or signing out forgets it. Works in hardcore
 * and softcore. */
#define RA_TOP_ROWS 10
#define RA_AROUND_ROWS 5
#define RA_ENTRIES_MAX (RA_TOP_ROWS + RA_AROUND_ROWS)
void ra_fetch_entries(ra_t *ra, uint32_t board);
typedef struct {
    uint32_t rank;
    bool user;       /* the signed-in user's row */
    bool gap_before; /* rows are missing between the one before and this one */
    char name[64];
    char score[24];
} ra_entry_t;
/* RA_NOTHING (nothing asked), RA_BUSY, RA_OK or RA_FAILED (*code: rcheevos' RC_*, RC_NO_RESPONSE without internet;
 * the server's message). With RA_OK: *total = players on the board, *on_board = the user has a score on it. Any
 * pointer may be NULL. */
ra_status_t ra_entries_status(ra_t *ra, int *code, char *message, size_t cap, uint32_t *total, bool *on_board);
size_t ra_entries(ra_t *ra, ra_entry_t *out, size_t max); /* the rows, best first (at most max) */

#endif
