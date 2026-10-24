# Milestone 15 — RetroAchievements leaderboards Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task, **inline in this session** (CLAUDE.md: never subagent-driven). Steps use checkbox (`- [ ]`) syntax for tracking. Go through all tasks one after another without waiting for the user; stop only at Task 9 (the user's check) or when blocked.

**Goal:** A Leaderboards page in the pause menu (each board's top 10 + the rows around the user) and the live leaderboard parts while playing in hardcore (started / failed notes, live counter pills, the submitted score and its rank), with a Settings switch "Leaderboard notes on screen".

**Architecture:** The milestone 14 glue (`platform/ra/ra.c`) gains rcheevos' leaderboard events in its event queue (a waiting counter update is replaced by a newer one), the board list and a two-part score fetch (top 10 + around the user) answered through the same request queue the Kotlin internet worker serves. The JNI bridge passes them on; the Kotlin side shows them (`RaPopups`, a new `Leaderboards.kt`, `AchievementScreens.kt`). Both pretend servers (`ra_fake.c`, `FakeRaServer.kt`) gain boards, score lists and score answers. rcheevos runs boards **only in hardcore** — the live parts are tested with the pretend servers; reading scores works in either mode.

**Tech Stack:** C11 (glue, JNI), rcheevos 12.5.0 (unchanged), Kotlin + Jetpack Compose (Material 3), JUnit 4 (JVM and on-device).

**Spec:** `docs/superpowers/specs/2026-10-06-milestone-15-leaderboards-design.md`. **Design system:** `DESIGN.md`, `PRODUCT.md` — Tasks 6 and 7 go through the impeccable skill as an extension of the existing Game Pak surface (like milestone 14's Tasks 9 and 11: inherit the world, no concept round, no `DESIGN.md` change unless the finish review's documenter adds components).

## Conventions for every task

- Build and test only through `bash tools/dev.sh …` (Git Bash). `unit [filter]` = PC unit tests (debug + sanitizers), `test` = all CTest, `jvmtest` = Kotlin tests on the PC, `apk`, `install`, `androidtest` = the Pixel 9 phone emulator, `bench …` = the speed check. Never a USB phone.
- Test first: write the test, see it fail, write the code, see it pass.
- **One commit per task**, message ending with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Work on branch `milestone-15`.
- **Every task's last step updates the documents** (CLAUDE.md rule): `docs/PROGRESS.md` (the task's **Completed** entry with the commit hash and any surprise; the M15 task table; **Project status**; **Next** = the next task, its first step and first command; "Last updated"), tick this plan's boxes, and re-read PROGRESS.md's Goal / status / Next / Notes, this plan's task list, the spec's status line and CLAUDE.md's "Key files" for anything stale. The doc changes go in the task's own commit.
- The emulator (Pixel 9 AVD) is restarted only with the user's OK (memory rule). Known flakes on a busy emulator: `SessionTest.runsAtGbaSpeed`, `AudioTest.aStuckStreamIsReopenedByTheWatchdog` — rerun once.
- In Git Bash, `adb shell` paths need `MSYS_NO_PATHCONV=1`; `adb` is `"$LOCALAPPDATA/Android/Sdk/platform-tools/adb.exe"`. Drive the emulator by element text (uiautomator dump), never blind coordinates (M14 note).
- **rcheevos facts this plan relies on** (read in 12.5.0 while planning, 2026-10-06): leaderboards are active only in hardcore (`rc_client_activate_leaderboards`); a board's start / cancel / submit conditions are tested every frame, and an attempt starts only after its start condition has been false once; events `LEADERBOARD_STARTED / FAILED / SUBMITTED` carry `event->leaderboard` (title, `tracker_value`), `LEADERBOARD_SCOREBOARD` carries `event->leaderboard_scoreboard` (submitted and best score as display text, `new_rank`, `num_entries`; valid only inside the callback), `LEADERBOARD_TRACKER_SHOW / UPDATE / HIDE` carry `event->leaderboard_tracker` (`id`, `display`); a refused score raises `SERVER_ERROR` with `api = "submit_lboard_entry"` and `related_id` = the board; the scoreboard event is raised only for an answer after at most one retry. Formats: `FRAMES` 60 → `"0:01.00"`, `SCORE` 7 → `"000007"`, `VALUE` 7 → `"7"`. Score fetches (`r=lbinfo`) need `LeaderboardData` with `LBID, LBFormat, LowerIsBetter (number), LBTitle, LBDesc, LBMem, GameID, LBAuthor, LBCreated/LBUpdated ("YYYY-MM-DD hh:mm:ss"), Entries [{User, Rank, Index, Score, DateSubmitted (unix seconds)}], TotalEntries`; the around-the-user fetch sends `u=<user>` and marks the user's row (`user_index`, -1 when not in the answer). A score answer (`r=submitlbentry`) needs `Response: {Score, BestScore, RankInfo: {Rank, NumEntries (a string)}, TopEntries: [{User, Rank, Score}]}`.
- A game with boards but no achievements (none known) is treated like a game without a set (no hardcore limits); out of scope.

## File map

| File | New / changed | Responsibility |
|---|---|---|
| `platform/ra/ra.h`, `platform/ra/ra.c` | changed | leaderboard events, tracker update replacement, `ra_has_leaderboards`, `ra_leaderboards`, `ra_fetch_entries`, `ra_entries_status`, `ra_entries` |
| `platform/ra/ra_fake.h`, `platform/ra/ra_fake.c` | changed | the PC pretend server's boards, `submitlbentry`, `lbinfo` |
| `tests/unit/test_ra.c` | changed | the glue's leaderboard tests |
| `tools/bench/main.c` | changed | `--ra n` also loads n/10 made-up boards |
| `platform/android/jni_ra.c` | changed | events with rank / entries / best; `raHasLeaderboards`, `raLeaderboards`, `raFetchEntries`, `raEntriesStatus`, `raEntriesMessage`, `raEntries` |
| `app/.../NativeBridge.kt` | changed | the new `external fun`s |
| `app/.../RaPopups.kt` | changed | `RaEvent` (+ rank, entries, best, new kinds), the leaderboard cards, the trackers, `leaderboardNotes` |
| `app/.../Leaderboards.kt` | new | `RaBoard`, `RaEntry`, `BoardScores` |
| `app/.../Settings.kt` | changed | `leaderboardNotes` (key `ra_lb_notes`, on by default) |
| `app/.../FakeRaServer.kt` | changed | the demo boards Sprint and Survival, `lbinfo`, `submitlbentry` |
| `app/.../AchievementScreens.kt` | changed | the counter pills, the leaderboard cards, `LeaderboardsPanel` |
| `app/.../Screens.kt` | changed | `MenuPage.LEADERBOARDS`, the Leaderboards row |
| `app/.../SettingsScreens.kt` | changed | the "Leaderboard notes on screen" switch |
| `app/.../MainActivity.kt` | changed | wiring: the boards of the game, the panel, the polling, the switch |
| `app/src/test/.../RaPopupsTest.kt`, `LeaderboardsTest.kt` (new), `SettingsTest.kt`, `FakeRaServerTest.kt` | changed / new | Kotlin tests |
| `app/src/androidTest/.../AchievementsDeviceTest.kt` | changed | a demo board submitted with its rank; a board's scores loaded |

---

## Task 1: The glue — leaderboard events, tracker updates, the pretend server's boards

**Files:**
- Modify: `platform/ra/ra.h`, `platform/ra/ra.c`, `platform/ra/ra_fake.h`, `platform/ra/ra_fake.c`, `tests/unit/test_ra.c`

- [x] **Step 1: Give the PC tests boards.** In `tests/unit/test_ra.c`, after the `SET` / `HASH` definitions, add the board macro and the test boards:

```c
#define LB(id, title, desc, fmt, lower, mem)                                                                    \
    "{\"ID\":" #id ",\"Title\":\"" title "\",\"Description\":\"" desc "\",\"Mem\":\"" mem "\",\"Format\":\"" fmt \
    "\",\"LowerIsBetter\":" lower ",\"Hidden\":false}"
/* 501: a time (lower is better) started by byte 0x10, cancelled by 0x11, submitted by 0x12, its value byte 0x13;
 * 502: a score, the same with bytes 0x20-0x23. */
static const char BOARDS[] = "[" LB(501, "Speedrun", "Fastest finish", "FRAMES", "true",
                                    "STA:0xH000010=1::CAN:0xH000011=1::SUB:0xH000012=1::VAL:0xH000013") ","
                             LB(502, "High Score", "Most points", "SCORE", "false",
                                    "STA:0xH000020=1::CAN:0xH000021=1::SUB:0xH000022=1::VAL:0xH000023") "]";
```

Replace `with_game` by a loader that takes the set and the boards (and keep `with_game` as the common case):

```c
/* Signed in, the game (these achievements and boards) loaded — one ra_idle activates it — and two quiet frames so
 * every achievement is armed and every board waits for its start. */
static ra_t *load_with(ra_fake_t *f, const char *achievements, const char *boards) {
    memset(iw, 0, sizeof iw);
    memset(ew, 0, sizeof ew);
    memset(sr, 0, sizeof sr);
    f->hash = HASH;
    f->achievements = achievements;
    f->leaderboards = boards;
    ra_t *ra = new_ra();
    ra_login_password(ra, "Tester", "secret");
    ra_fake_serve(f, ra);
    ra_load_game(ra, HASH);
    ra_fake_serve(f, ra);
    CHECK_EQ(ra_game_status(ra, NULL), RA_BUSY); /* not yet: memory is read only in ra_idle / ra_do_frame */
    ra_memory_t m = mem();
    ra_idle(ra, &m);
    CHECK_EQ(ra_game_status(ra, NULL), RA_OK);
    frames(ra, 2);
    drain(ra);
    return ra;
}

static ra_t *with_game(ra_fake_t *f) {
    return load_with(f, SET, BOARDS);
}

/* Every waiting event, taken out (at most max). */
static int take_all(ra_t *ra, ra_event_t *out, int max) {
    int n = 0;
    while (n < max && ra_take_event(ra, &out[n])) n++;
    return n;
}

/* The first of these events of this kind and id (0 = any id), or NULL. */
static const ra_event_t *find(const ra_event_t *list, int n, int kind, uint32_t id) {
    for (int i = 0; i < n; i++)
        if (list[i].kind == kind && (!id || list[i].id == id)) return &list[i];
    return NULL;
}
```

- [x] **Step 2: Write the failing tests** (end of `tests/unit/test_ra.c`):

```c
/* ---- leaderboards (milestone 15) ---- */

TEST(ra_leaderboard_attempt_starts_counts_and_is_submitted) {
    ra_fake_t f = {0};
    ra_t *ra = with_game(&f); /* rcheevos starts in hardcore: boards are active */
    ra_event_t ev[64];
    iw[0x10] = 1;
    frames(ra, 1);
    int n = take_all(ra, ev, 64);
    const ra_event_t *e = find(ev, n, RA_EVENT_LB_STARTED, 501);
    CHECK(e != NULL);
    if (!e) return;
    CHECK(strcmp(e->title, "Speedrun") == 0);
    const ra_event_t *t = find(ev, n, RA_EVENT_TRACKER_SHOW, 0);
    CHECK(t != NULL);
    if (!t) return;
    CHECK(strcmp(t->progress, "0:00.00") == 0);
    uint32_t tracker = t->id;

    iw[0x13] = 60;
    frames(ra, 1);
    n = take_all(ra, ev, 64);
    t = find(ev, n, RA_EVENT_TRACKER_UPDATE, tracker);
    CHECK(t != NULL && strcmp(t->progress, "0:01.00") == 0);

    iw[0x12] = 1;
    frames(ra, 1);
    n = take_all(ra, ev, 64);
    e = find(ev, n, RA_EVENT_LB_SUBMITTED, 501);
    CHECK(e != NULL && strcmp(e->progress, "0:01.00") == 0);
    CHECK(find(ev, n, RA_EVENT_TRACKER_HIDE, tracker) != NULL);

    CHECK_EQ(ra_fake_serve(&f, ra), 1);
    CHECK(strstr(f.last, "r=submitlbentry") != NULL && strstr(f.last, "i=501") != NULL && strstr(f.last, "s=60") != NULL);
    n = take_all(ra, ev, 64);
    e = find(ev, n, RA_EVENT_LB_SCOREBOARD, 501);
    CHECK(e != NULL);
    if (!e) return;
    CHECK(strcmp(e->title, "Speedrun") == 0);
    CHECK(strcmp(e->progress, "0:01.00") == 0);
    CHECK(strcmp(e->best, "0:01.00") == 0);
    CHECK_EQ(e->rank, 3);
    CHECK_EQ(e->entries, 31);
    ra_destroy(ra);
}

TEST(ra_leaderboard_attempt_can_fail) {
    ra_fake_t f = {0};
    ra_t *ra = with_game(&f);
    ra_event_t ev[64];
    iw[0x20] = 1;
    iw[0x23] = 7;
    frames(ra, 1);
    int n = take_all(ra, ev, 64);
    CHECK(find(ev, n, RA_EVENT_LB_STARTED, 502) != NULL);
    const ra_event_t *t = find(ev, n, RA_EVENT_TRACKER_SHOW, 0);
    CHECK(t != NULL);
    if (!t) return;
    CHECK(strcmp(t->progress, "000007") == 0);
    uint32_t tracker = t->id;
    iw[0x21] = 1;
    frames(ra, 1);
    n = take_all(ra, ev, 64);
    const ra_event_t *e = find(ev, n, RA_EVENT_LB_FAILED, 502);
    CHECK(e != NULL && strcmp(e->title, "High Score") == 0);
    CHECK(find(ev, n, RA_EVENT_TRACKER_HIDE, tracker) != NULL);
    CHECK_EQ(ra_fake_serve(&f, ra), 0); /* nothing sent */
    ra_destroy(ra);
}

TEST(ra_refused_score_is_a_leaderboard_error) {
    ra_fake_t f = {0};
    ra_t *ra = with_game(&f);
    f.refuse_board = 501;
    iw[0x10] = 1;
    frames(ra, 1);
    iw[0x12] = 1;
    frames(ra, 1);
    drain(ra);
    ra_fake_serve(&f, ra);
    ra_event_t ev[64];
    int n = take_all(ra, ev, 64);
    const ra_event_t *e = find(ev, n, RA_EVENT_LB_ERROR, 501);
    CHECK(e != NULL);
    if (!e) return;
    CHECK(strcmp(e->title, "Speedrun") == 0);
    CHECK(strstr(e->description, "refused") != NULL);
    CHECK(find(ev, n, RA_EVENT_SERVER_ERROR, 0) == NULL); /* not an achievement's error */
    CHECK(find(ev, n, RA_EVENT_LB_SCOREBOARD, 0) == NULL);
    ra_destroy(ra);
}

TEST(ra_tracker_updates_do_not_push_other_events_out) {
    /* The counter changes every frame; while nobody takes events, only its newest update waits. */
    ra_fake_t f = {0};
    ra_t *ra = with_game(&f);
    iw[0x10] = 1;
    frames(ra, 1);
    for (int i = 1; i <= 2 * RA_EVENTS_MAX; i++) {
        iw[0x13] = (uint8_t)i;
        frames(ra, 1);
    }
    ra_event_t ev[64];
    int n = take_all(ra, ev, 64);
    int started = 0, shown = 0, updates = 0;
    const ra_event_t *last = NULL;
    for (int i = 0; i < n; i++) {
        started += ev[i].kind == RA_EVENT_LB_STARTED;
        shown += ev[i].kind == RA_EVENT_TRACKER_SHOW;
        if (ev[i].kind == RA_EVENT_TRACKER_UPDATE) {
            updates++;
            last = &ev[i];
        }
    }
    CHECK_EQ(started, 1);
    CHECK_EQ(shown, 1);
    CHECK_EQ(updates, 1);
    CHECK(last != NULL && strcmp(last->progress, "0:01.06") == 0); /* 64 frames */
    ra_destroy(ra);
}

TEST(ra_no_leaderboards_in_softcore) {
    ra_fake_t f = {0};
    ra_t *ra = with_game(&f);
    ra_set_hardcore(ra, false);
    drain(ra);
    iw[0x10] = 1;
    frames(ra, 3);
    iw[0x12] = 1;
    frames(ra, 1);
    ra_event_t ev[64];
    int n = take_all(ra, ev, 64);
    CHECK(find(ev, n, RA_EVENT_LB_STARTED, 0) == NULL);
    CHECK(find(ev, n, RA_EVENT_LB_SUBMITTED, 0) == NULL);
    ra_fake_serve(&f, ra);
    CHECK(strstr(f.last, "submitlbentry") == NULL);
    ra_destroy(ra);
}

TEST(ra_a_hit_count_value_counts_frames) {
    /* The app's pretend server (FakeRaServer.kt) builds its demo boards this way: started, counted and submitted by
     * hit counts alone, so they run in any game. */
    static const char B[] = "[" LB(601, "Sprint", "x", "FRAMES", "true",
                                   "STA:0xH000000>=0.10.::CAN:0=1::SUB:0xH000000>=0.70.::VAL:M:0xH000000>=0") "]";
    ra_fake_t f = {0};
    ra_t *ra = load_with(&f, SET, B);
    frames(ra, 80);
    ra_event_t ev[64];
    int n = take_all(ra, ev, 64);
    CHECK(find(ev, n, RA_EVENT_LB_STARTED, 601) != NULL);
    const ra_event_t *e = find(ev, n, RA_EVENT_LB_SUBMITTED, 601);
    CHECK(e != NULL);
    if (e) CHECK(strncmp(e->progress, "0:01.0", 6) == 0); /* about 60 frames counted */
    ra_destroy(ra);
}
```

Note: `load_with` leaves the hit counts of the first two frames in place, so the 601 board starts on frame 10 and submits on frame 70 counting from the first frame — the check allows "0:01.00"–"0:01.09".

- [x] **Step 3: Run them to see them fail**

Run: `bash tools/dev.sh unit ra_`
Expected: the build fails — `RA_EVENT_LB_STARTED`, `leaderboards`, `refuse_board`, `best`, `rank`, `entries` undeclared.

- [x] **Step 4: The header.** In `platform/ra/ra.h`, extend the event kinds (after `RA_EVENT_RESET`) and the event record:

```c
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
```

(The kinds' numbers: `RA_EVENT_RESET` = 10, so `LB_STARTED` = 11 … `TRACKER_HIDE` = 18; the Kotlin side uses these numbers.)

- [x] **Step 5: The events in `platform/ra/ra.c`.** Replace `is_progress` and the replacement loop in `push_event` with a general "only the newest waits" rule:

```c
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
```

In `on_event`, add `const rc_client_leaderboard_t *lb = NULL;` after `const rc_client_achievement_t *a = event->achievement;`, change the `SERVER_ERROR` case and add the leaderboard cases before `default`:

```c
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
```

(Leaderboard events have no `event->achievement`, so `a` is NULL for them; the existing `if (a) { … }` block after the switch stays as it is.)

- [x] **Step 6: The PC pretend server.** In `platform/ra/ra_fake.h`, add to `ra_fake_t` (after `achievements`) and update the comment:

```c
    const char *leaderboards; /* the game's boards (a JSON array, rcheevos' format); NULL = none */
    uint32_t refuse_board;    /* a board whose scores are refused (0 = none) */
    uint32_t user_rank;       /* Tester's rank on board 501, which has 30 players (0 = not on it); every other board is empty */
```

Comment addition (end of the header comment): ` Scores (milestone 15): a submitted score is ranked 3rd of 31, unless its board is \`refuse_board\`; a score list (r=lbinfo) of board 501 has 30 players "P01"… with Tester at \`user_rank\`, each scoring 100 + 10 × rank frames.`

In `platform/ra/ra_fake.c`: add the refusal text after `REFUSED`:

```c
static const char REFUSED_SCORE[] = "{\"Success\":false,\"Error\":\"The server refused this score.\"}";
```

Put the boards into the game's answer — in the `achievementsets` branch, size the buffer for both lists and use a second `%s`:

```c
        const char *list = f->achievements ? f->achievements : "[]";
        const char *boards = f->leaderboards ? f->leaderboards : "[]";
        size_t cap = strlen(list) + strlen(boards) + 1024;
```

and in the format string replace `\"Leaderboards\":[]}]}"` by `\"Leaderboards\":%s}]}"`, with the arguments `list, boards`.

Add the score answer before the final `else` of `answer()` (the `lbinfo` branch comes in Task 2):

```c
    } else if (!strcmp(r, "submitlbentry")) {
        char i[16], s[24];
        param(q, "i", i, sizeof i);
        param(q, "s", s, sizeof s);
        unsigned board = (unsigned)strtoul(i, NULL, 10);
        if (f->refuse_board && board == f->refuse_board) {
            say(ra, rq->id, 400, REFUSED_SCORE);
            return;
        }
        char body[640];
        snprintf(body, sizeof body,
                 "{\"Success\":true,\"Response\":{\"Score\":%s,\"BestScore\":%s,\"LBData\":{\"Format\":\"FRAMES\","
                 "\"LeaderboardID\":%u,\"GameID\":1234,\"Title\":\"Board\",\"LowerIsBetter\":1},"
                 "\"RankInfo\":{\"Rank\":3,\"NumEntries\":\"31\",\"LowerIsBetter\":1,\"UserRank\":3},"
                 "\"TopEntries\":[{\"User\":\"P01\",\"Rank\":1,\"Score\":110},{\"User\":\"P02\",\"Rank\":2,\"Score\":120},"
                 "{\"User\":\"Tester\",\"Rank\":3,\"Score\":%s}]}}",
                 s, s, board, s);
        say(ra, rq->id, 200, body);
```

- [x] **Step 7: Run the tests to see them pass**

Run: `bash tools/dev.sh unit ra_`
Expected: all `ra_` tests pass (24 old + 6 new). If `ra_a_hit_count_value_counts_frames` fails because rcheevos rejects the value `M:0xH000000>=0` (game load fails or the value stays 0/1), use `VAL:M:0xH000000>=0.99999.` (a hit target it never reaches) in this test and note it for Task 5's `FakeRaServer` boards. If a tracker's HIDE comes one frame after the submit, take one more frame before looking for it (and note it).

- [x] **Step 8: The whole suite**

Run: `bash tools/dev.sh unit` then `bash tools/dev.sh test`
Expected: unit 467/467 (461 + 6), CTest 121/121, no warnings, no sanitizer reports.

- [x] **Step 9: Commit (with the documents updated as in the conventions)**

```bash
git add platform/ra tests/unit/test_ra.c docs/PROGRESS.md docs/superpowers/plans/2026-10-06-milestone-15-leaderboards.md
git commit -m "feat(ra): leaderboard events and counter updates in the glue; the pretend server's boards and scores

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

## Task 2: The glue — the board list and the score fetch

**Files:**
- Modify: `platform/ra/ra.h`, `platform/ra/ra.c`, `platform/ra/ra_fake.c`, `tests/unit/test_ra.c`

- [x] **Step 1: Write the failing tests** (end of `tests/unit/test_ra.c`):

```c
TEST(ra_board_list_has_the_games_boards) {
    ra_fake_t f = {0};
    ra_t *ra = with_game(&f);
    CHECK(ra_has_leaderboards(ra));
    ra_board_t b[8];
    CHECK_EQ(ra_leaderboards(ra, b, 8), 2);
    CHECK_EQ(b[0].id, 501);
    CHECK(strcmp(b[0].title, "Speedrun") == 0);
    CHECK(strcmp(b[0].description, "Fastest finish") == 0);
    CHECK(b[0].lower_is_better);
    CHECK_EQ(b[1].id, 502);
    CHECK(!b[1].lower_is_better);
    CHECK_EQ(ra_leaderboards(ra, b, 1), 1); /* at most max */
    ra_unload_game(ra);
    CHECK(!ra_has_leaderboards(ra));
    CHECK_EQ(ra_leaderboards(ra, b, 8), 0);
    ra_destroy(ra);

    ra_fake_t g = {0};
    ra = load_with(&g, SET, NULL); /* a game without boards */
    CHECK(!ra_has_leaderboards(ra));
    ra_destroy(ra);
}

/* Fetches board's scores from the pretend server; the status. */
static ra_status_t fetch(ra_fake_t *f, ra_t *ra, uint32_t board, uint32_t *total, bool *on_board) {
    ra_fetch_entries(ra, board);
    CHECK_EQ(ra_entries_status(ra, NULL, NULL, 0, NULL, NULL), RA_BUSY);
    ra_fake_serve(f, ra);
    return ra_entries_status(ra, NULL, NULL, 0, total, on_board);
}

TEST(ra_scores_are_the_top_ten_and_the_rows_around_the_user) {
    ra_fake_t f = {0};
    ra_t *ra = with_game(&f);
    f.user_rank = 17;
    uint32_t total = 0;
    bool on = false;
    CHECK_EQ(fetch(&f, ra, 501, &total, &on), RA_OK);
    CHECK_EQ(total, 30);
    CHECK(on);
    ra_entry_t r[RA_ENTRIES_MAX];
    size_t n = ra_entries(ra, r, RA_ENTRIES_MAX);
    CHECK_EQ(n, 15);
    if (n != 15) return;
    CHECK_EQ(r[0].rank, 1);
    CHECK(strcmp(r[0].name, "P01") == 0);
    CHECK(strcmp(r[0].score, "0:01.83") == 0); /* 110 frames */
    CHECK_EQ(r[9].rank, 10);
    CHECK_EQ(r[10].rank, 15);
    CHECK(r[10].gap_before);
    CHECK_EQ(r[12].rank, 17);
    CHECK(r[12].user);
    CHECK(strcmp(r[12].name, "Tester") == 0);
    CHECK(strcmp(r[12].score, "0:04.50") == 0); /* 270 frames */
    CHECK_EQ(r[14].rank, 19);
    int gaps = 0, users = 0;
    for (size_t i = 0; i < n; i++) {
        gaps += r[i].gap_before;
        users += r[i].user;
    }
    CHECK_EQ(gaps, 1);
    CHECK_EQ(users, 1);
    ra_destroy(ra);
}

TEST(ra_scores_user_in_the_top_ten_has_no_gap) {
    ra_fake_t f = {0};
    ra_t *ra = with_game(&f);
    f.user_rank = 4;
    CHECK_EQ(fetch(&f, ra, 501, NULL, NULL), RA_OK);
    ra_entry_t r[RA_ENTRIES_MAX];
    size_t n = ra_entries(ra, r, RA_ENTRIES_MAX);
    CHECK_EQ(n, 10);
    for (size_t i = 0; i < n; i++) {
        CHECK(!r[i].gap_before);
        CHECK_EQ(r[i].user, i == 3);
    }
    ra_destroy(ra);
}

TEST(ra_scores_when_the_user_is_not_on_the_board) {
    ra_fake_t f = {0}; /* user_rank 0 */
    ra_t *ra = with_game(&f);
    uint32_t total = 0;
    bool on = true;
    CHECK_EQ(fetch(&f, ra, 501, &total, &on), RA_OK);
    CHECK_EQ(total, 30);
    CHECK(!on);
    ra_entry_t r[RA_ENTRIES_MAX];
    size_t n = ra_entries(ra, r, RA_ENTRIES_MAX);
    CHECK_EQ(n, 10);
    for (size_t i = 0; i < n; i++) CHECK(!r[i].user && !r[i].gap_before);
    ra_destroy(ra);
}

TEST(ra_scores_of_an_empty_board) {
    ra_fake_t f = {0};
    ra_t *ra = with_game(&f);
    uint32_t total = 9;
    bool on = true;
    CHECK_EQ(fetch(&f, ra, 502, &total, &on), RA_OK);
    CHECK_EQ(total, 0);
    CHECK(!on);
    ra_entry_t r[RA_ENTRIES_MAX];
    CHECK_EQ(ra_entries(ra, r, RA_ENTRIES_MAX), 0);
    ra_destroy(ra);
}

TEST(ra_scores_offline_fail_as_no_connection) {
    ra_fake_t f = {0};
    ra_t *ra = with_game(&f);
    f.offline = true;
    ra_fetch_entries(ra, 501);
    ra_fake_serve(&f, ra);
    int code = 0;
    CHECK_EQ(ra_entries_status(ra, &code, NULL, 0, NULL, NULL), RA_FAILED);
    CHECK_EQ((unsigned)code, (unsigned)RC_NO_RESPONSE);
    ra_destroy(ra);
}

TEST(ra_only_the_newest_fetch_counts) {
    ra_fake_t f = {0};
    ra_t *ra = with_game(&f);
    f.user_rank = 17;
    ra_fetch_entries(ra, 501);
    ra_fetch_entries(ra, 502); /* before 501's answers */
    ra_fake_serve(&f, ra);
    uint32_t total = 9;
    CHECK_EQ(ra_entries_status(ra, NULL, NULL, 0, &total, NULL), RA_OK);
    CHECK_EQ(total, 0); /* 502's */
    ra_entry_t r[RA_ENTRIES_MAX];
    CHECK_EQ(ra_entries(ra, r, RA_ENTRIES_MAX), 0);
    ra_fetch_entries(ra, 501);
    ra_unload_game(ra); /* the game closes while it loads */
    ra_fake_serve(&f, ra);
    CHECK_EQ(ra_entries_status(ra, NULL, NULL, 0, NULL, NULL), RA_NOTHING);
    CHECK_EQ(ra_entries(ra, r, RA_ENTRIES_MAX), 0);
    ra_destroy(ra);
}
```

- [x] **Step 2: Run them to see them fail**

Run: `bash tools/dev.sh unit ra_`
Expected: the build fails — `ra_has_leaderboards`, `ra_board_t`, `ra_entry_t`, `ra_fetch_entries`, `ra_entries_status`, `ra_entries`, `RA_ENTRIES_MAX` undeclared.

- [x] **Step 3: The header** — append to `platform/ra/ra.h` before `#endif`:

```c
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
```

- [x] **Step 4: The code** — in `platform/ra/ra.c`, add to `struct ra` (after `char login_message[256];`):

```c
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
```

Add a section before `/* ---- set-up ---- */`:

```c
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

/* (lock held) */
static void drop_lists(ra_t *ra) {
    if (ra->top) rc_client_destroy_leaderboard_entry_list(ra->top);
    if (ra->around) rc_client_destroy_leaderboard_entry_list(ra->around);
    ra->top = ra->around = NULL;
}

/* (lock held) Forgets the fetch: an answer still on its way no longer counts. */
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
    memcpy(out, ra->entries, n * sizeof *out);
    pthread_mutex_unlock(&ra->lock);
    return n;
}
```

Call `forget_entries(ra);` inside the locked parts of `ra_logout` and `ra_unload_game` (next to `ra->game_generation++`), and in `ra_destroy` call `drop_lists(ra);` before `rc_client_destroy(ra->client);`. `drop_lists` and `forget_entries` must be defined before `ra_logout` — put the two `static` functions (and `put_entry` stays below) right after `free_pending`, or add forward declarations at the top: `static void forget_entries(ra_t *ra);` `static void drop_lists(ra_t *ra);`.

- [x] **Step 5: The pretend server's score lists** — in `platform/ra/ra_fake.c`, a function before `answer()`:

```c
/* r=lbinfo: board 501 has 30 players "P01"… (Tester at f->user_rank), each 100 + 10 × rank frames; others are empty.
 * With u= (around the user) the rows are centred on the user's rank — the top rows when the user isn't on it. */
static void lbinfo(ra_fake_t *f, ra_t *ra, int id, const char *q) {
    char i[16], u[64], o[16], c[16];
    param(q, "i", i, sizeof i);
    param(q, "u", u, sizeof u);
    param(q, "o", o, sizeof o);
    param(q, "c", c, sizeof c);
    unsigned board = (unsigned)strtoul(i, NULL, 10), count = (unsigned)strtoul(c, NULL, 10);
    unsigned total = board == 501 ? 30 : 0, first = 1;
    if (u[0]) first = f->user_rank > count / 2 ? f->user_rank - count / 2 : 1;
    else first = (unsigned)strtoul(o, NULL, 10) + 1;
    if (total && first + count > total + 1) first = total >= count ? total - count + 1 : 1;
    char body[4096];
    int n = snprintf(body, sizeof body,
                     "{\"Success\":true,\"LeaderboardData\":{\"LBID\":%u,\"LBFormat\":\"FRAMES\",\"LowerIsBetter\":1,"
                     "\"LBTitle\":\"Board\",\"LBDesc\":\"-\",\"LBMem\":\"-\",\"GameID\":1234,\"LBAuthor\":\"Test\","
                     "\"LBCreated\":\"2026-01-01 00:00:00\",\"LBUpdated\":\"2026-01-01 00:00:00\",\"Entries\":[",
                     board);
    for (unsigned r = first; r < first + count && r <= total; r++) {
        char name[16];
        if (r == f->user_rank) snprintf(name, sizeof name, "Tester");
        else snprintf(name, sizeof name, "P%02u", r);
        n += snprintf(body + n, sizeof body - (size_t)n,
                      "%s{\"User\":\"%s\",\"Rank\":%u,\"Index\":%u,\"Score\":%u,\"DateSubmitted\":1767225600}",
                      r == first ? "" : ",", name, r, r, 100 + 10 * r);
    }
    snprintf(body + n, sizeof body - (size_t)n, "],\"TotalEntries\":%u}}", total);
    say(ra, id, 200, body);
}
```

and in `answer()`, a branch before `submitlbentry`: `} else if (!strcmp(r, "lbinfo")) { lbinfo(f, ra, rq->id, q);`.

- [x] **Step 6: Run the tests to see them pass**

Run: `bash tools/dev.sh unit ra_`
Expected: all `ra_` tests pass (37). If the offline fetch's code isn't `RC_NO_RESPONSE`, look at what `rc_client_server_error_message` gives a retryable answer in `rc_client.c`, use that code in the test and in Task 5's `BoardScores.of` (and note it).

- [x] **Step 7: The whole suite**

Run: `bash tools/dev.sh unit` then `bash tools/dev.sh test`
Expected: unit 474/474, CTest 121/121, no warnings, no sanitizer reports.

- [x] **Step 8: Commit (with the documents)**

```bash
git add platform/ra tests/unit/test_ra.c docs/PROGRESS.md docs/superpowers/plans/2026-10-06-milestone-15-leaderboards.md
git commit -m "feat(ra): a game's leaderboards and a board's scores (top 10 + around the user) in the glue

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

## Task 3: The speed check with boards — `gbabench --ra <n>`

**Files:**
- Modify: `tools/bench/main.c`

- [x] **Step 1: Made-up boards.** In `tools/bench/main.c`, after `made_up_set`, add:

```c
/* n made-up leaderboards whose start (a 32-bit compare) is practically never true: their conditions are tested every
 * frame, as in a game with boards (rcheevos runs boards in hardcore, its default here). */
static char *made_up_boards(int n) {
    char *json = malloc((size_t)n * 240 + 3);
    if (!json) return NULL;
    char *p = json;
    *p++ = '[';
    uint32_t x = 54321;
    for (int i = 0; i < n; i++) {
        x = x * 1103515245u + 12345u;
        uint32_t a = (x >> 8) % 0x47FFCu, b = (x >> 4) % 0x47FFCu;
        p += sprintf(p, "%s{\"ID\":%d,\"Title\":\"L%d\",\"Description\":\"-\","
                        "\"Mem\":\"STA:0xX%06x=%u::CAN:0=1::SUB:0=1::VAL:0xX%06x\",\"Format\":\"VALUE\","
                        "\"LowerIsBetter\":false,\"Hidden\":false}",
                     i ? "," : "", 200000 + i, i, (unsigned)a, (unsigned)(x | 0x80000000u), (unsigned)b);
    }
    *p++ = ']';
    *p = '\0';
    return json;
}
```

In `bench_ra_start`, make the boards (n / 10, at least 1) and pass them:

```c
    char *set = made_up_set(n);
    char *boards = made_up_boards(n / 10 > 0 ? n / 10 : 1);
    ra_fake_t f = {0};
    f.hash = "00000000000000000000000000000000";
    f.achievements = set;
    f.leaderboards = boards;
    bench_ra = ra_create();
    if (!set || !boards || !bench_ra) return false;
```

free `boards` next to `free(set);`, and after the summary check add:

```c
    if (!ra_has_leaderboards(bench_ra)) {
        fprintf(stderr, "--ra: the made-up boards did not load\n");
        return false;
    }
```

Update the header comment's `--ra n` line: `--ra n (milestones 14-15): n made-up RetroAchievements (2 conditions each, never met) and n/10 made-up leaderboards (never started) checked after every frame, as the app does.`

- [x] **Step 2: Build and run it once**

Run: `bash tools/dev.sh bench --runs 1 --ra 300`
Expected: every scene runs, every picture and sound hash `ok`, no "did not load" message.

- [x] **Step 3: Measure A/B against the floors.** The phone emulator may be running (it slows the PC ~40 %): measure alternately, 2 rounds of `bash tools/dev.sh bench --runs 3` then `bash tools/dev.sh bench --runs 3 --ra 300`, and take each scene's best. Expected: with `--ra 300` every scene ≥ its 97 % floor (`docs/PROGRESS.md` Notes: game_a 1161, game_b 921, game_c 1733, game_d 935, m7_demo 2202, snd1_demo 5836 fps), all hashes `ok`. A scene below its floor is measured again before it counts; if it stays below with the emulator closed, stop and look at what the boards cost (`rc_evaluate_lboard`).

- [x] **Step 4: Commit (with the documents: the numbers into the task's Completed entry and Notes)**

```bash
git add tools/bench/main.c docs/PROGRESS.md docs/superpowers/plans/2026-10-06-milestone-15-leaderboards.md
git commit -m "feat(bench): --ra also loads made-up leaderboards

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

## Task 4: The JNI bridge

**Files:**
- Modify: `platform/android/jni_ra.c`, `app/src/main/java/com/tengen/gbaemu/NativeBridge.kt`, `app/src/main/java/com/tengen/gbaemu/RaPopups.kt`, `app/src/test/java/com/tengen/gbaemu/RaPopupsTest.kt`

- [x] **Step 1: The failing test** — in `RaPopupsTest`, replace `anEventIsReadFromItsUtf8Parts` with:

```kotlin
    @Test
    fun anEventIsReadFromItsUtf8Parts() {
        val parts = listOf("14", "11", "0", "Première ★", "", "0:10.00", "", "4", "1204", "0:09.50").map { it.toByteArray() }
        assertEquals(RaEvent(RaEvent.LB_SCOREBOARD, 11, 0, "Première ★", "", "0:10.00", "", rank = 4, entries = 1204, best = "0:09.50"),
            RaEvent.of(parts.toTypedArray()))
        assertNull(RaEvent.of(null))
        assertNull(RaEvent.of(arrayOf("x".toByteArray())))
    }
```

- [x] **Step 2: Run it to see it fail**

Run: `bash tools/dev.sh jvmtest`
Expected: compile errors — `LB_SCOREBOARD`, `rank`, `entries`, `best` unknown.

- [x] **Step 3: `RaEvent`** in `RaPopups.kt`:

```kotlin
/** One of the glue's events (platform/ra/ra.h ra_event_t), from NativeBridge.raTakeEvent. */
data class RaEvent(val kind: Int, val id: Int, val points: Int, val title: String, val description: String,
                   val progress: String, val badgeUrl: String, val rank: Int = 0, val entries: Int = 0, val best: String = "") {
    companion object {
        const val UNLOCKED = 1
        const val PROGRESS = 2
        const val PROGRESS_HIDE = 3
        const val CHALLENGE_SHOW = 4
        const val CHALLENGE_HIDE = 5
        const val MASTERED = 6
        const val SERVER_ERROR = 7
        const val OFFLINE = 8
        const val ONLINE = 9
        const val RESET = 10
        // leaderboards (15)
        const val LB_STARTED = 11
        const val LB_FAILED = 12
        const val LB_SUBMITTED = 13
        const val LB_SCOREBOARD = 14
        const val LB_ERROR = 15
        const val TRACKER_SHOW = 16
        const val TRACKER_UPDATE = 17
        const val TRACKER_HIDE = 18

        /** raTakeEvent's 10 texts: kind, id, points, title, description, progress, badge url, rank, entries, best. */
        fun of(parts: Array<ByteArray>?): RaEvent? {
            if (parts == null || parts.size != 10) return null
            val p = parts.map { String(it, Charsets.UTF_8) }
            val kind = p[0].toIntOrNull() ?: return null
            return RaEvent(kind, p[1].toIntOrNull() ?: 0, p[2].toIntOrNull() ?: 0, p[3], p[4], p[5], p[6],
                p[7].toIntOrNull() ?: 0, p[8].toIntOrNull() ?: 0, p[9])
        }
    }
}
```

- [x] **Step 4: The C side** — in `platform/android/jni_ra.c`, `raTakeEvent` sends 10 texts:

```c
/* [kind, id, points, title, description, progress, badge url, rank, entries, best] as UTF-8, or null when none is waiting. */
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
```

and the new functions at the end of the file:

```c
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
```

- [x] **Step 5: `NativeBridge.kt`** — change the `raTakeEvent` comment to `/** [kind, id, points, title, description, progress, badge url, rank, entries, best], or null. */` and add after `raUserAgentClause`:

```kotlin
    // Leaderboards (15)
    external fun raHasLeaderboards(): Boolean
    /** 4 texts per board: id, lower is better 1/0, title, description. */
    external fun raLeaderboards(): Array<ByteArray>
    /** Asks for a board's scores (the top 10 + the rows around the user); only the newest counts. */
    external fun raFetchEntries(board: Int)
    /** [status (RA_*), rcheevos' code, total players, on the board 1/0]. */
    external fun raEntriesStatus(): IntArray
    external fun raEntriesMessage(): ByteArray
    /** 5 texts per row, best first: rank, the user's 1/0, a gap before it 1/0, name, score. */
    external fun raEntries(): Array<ByteArray>
```

- [x] **Step 6: Run the tests**

Run: `bash tools/dev.sh jvmtest`, then `bash tools/dev.sh apk`, then `bash tools/dev.sh androidtest`
Expected: Kotlin 222/222; the APK builds with no C warnings; on-device 83/83 (the existing `AchievementsDeviceTest` reads events through the new 10-part `RaEvent.of`).

- [x] **Step 7: Commit (with the documents)**

```bash
git add platform/android/jni_ra.c app/src/main/java/com/tengen/gbaemu/NativeBridge.kt app/src/main/java/com/tengen/gbaemu/RaPopups.kt app/src/test/java/com/tengen/gbaemu/RaPopupsTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-06-milestone-15-leaderboards.md
git commit -m "feat(jni): leaderboard events, boards and scores for the app

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

## Task 5: Kotlin — the pop-up queue, the boards and scores, the setting, the pretend server; on-device tests

**Files:**
- Create: `app/src/main/java/com/tengen/gbaemu/Leaderboards.kt`, `app/src/test/java/com/tengen/gbaemu/LeaderboardsTest.kt`
- Modify: `RaPopups.kt`, `Settings.kt`, `FakeRaServer.kt`, `RaPopupsTest.kt`, `SettingsTest.kt`, `FakeRaServerTest.kt`, `app/src/androidTest/.../AchievementsDeviceTest.kt`

- [x] **Step 1: The failing JVM tests.**

`RaPopupsTest` — add:

```kotlin
    private fun lb(kind: Int, title: String = "Sprint", progress: String = "", description: String = "", rank: Int = 0,
                   entries: Int = 0, best: String = "", id: Int = 11) =
        RaEvent(kind, id, 0, title, description, progress, "", rank, entries, best)

    @Test
    fun leaderboardNotesAndScoresQueueLikeTheOtherCards() {
        val p = RaPopups()
        p.add(lb(RaEvent.LB_STARTED), 0)
        assertEquals("Sprint", p.view().card!!.title)
        assertEquals("Leaderboard started", p.view().card!!.text)
        p.add(lb(RaEvent.LB_SUBMITTED, progress = "0:10.00"), 0)
        p.add(lb(RaEvent.LB_SCOREBOARD, progress = "0:10.00", rank = 4, entries = 1204, best = "0:09.50"), 0)
        p.tick(RaPopups.NOTE_MS)
        assertEquals("Score 0:10.00 · sending", p.view().card!!.text)
        p.tick(2 * RaPopups.NOTE_MS)
        val score = p.view().card!!
        assertEquals("Rank 4 of 1,204 · 0:10.00 · best 0:09.50", score.text)
        assertEquals(4, score.rank)
        assertFalse(p.tick(2 * RaPopups.NOTE_MS + RaPopups.UNLOCK_MS - 1)) // a score shows as long as an unlock
        assertTrue(p.tick(2 * RaPopups.NOTE_MS + RaPopups.UNLOCK_MS))
    }

    @Test
    fun aBestScoreIsLeftOutWhenItIsThisOne() {
        val p = RaPopups()
        p.add(lb(RaEvent.LB_SCOREBOARD, progress = "0:10.00", rank = 1, entries = 2, best = "0:10.00"), 0)
        assertEquals("Rank 1 of 2 · 0:10.00", p.view().card!!.text)
    }

    @Test
    fun aRefusedScoreSaysSo() {
        val p = RaPopups()
        p.add(lb(RaEvent.LB_ERROR, description = "Hardcore unlocks cannot be earned using this emulator."), 0)
        val c = p.view().card!!
        assertEquals(RaEvent.LB_ERROR, c.kind)
        assertEquals("Couldn't send a score", c.title)
        assertEquals("Sprint: Hardcore unlocks cannot be earned using this emulator.", c.text)
    }

    @Test
    fun countersShowUpdateHideAndAtMostThreeShow() {
        val p = RaPopups()
        p.add(lb(RaEvent.TRACKER_SHOW, progress = "0:00.00", id = 1), 0)
        p.add(lb(RaEvent.TRACKER_UPDATE, progress = "0:01.00", id = 1), 0)
        assertEquals(listOf(RaPopups.Tracker(1, "0:01.00")), p.view().trackers)
        for (i in 2..5) p.add(lb(RaEvent.TRACKER_SHOW, progress = "$i", id = i), 0)
        assertEquals(listOf(1, 2, 3), p.view().trackers.map { it.id })
        p.add(lb(RaEvent.TRACKER_HIDE, id = 1), 0)
        assertEquals(listOf(2, 3, 4), p.view().trackers.map { it.id })
        p.clear()
        assertTrue(p.view().trackers.isEmpty())
    }

    @Test
    fun theSwitchHidesNotesAndCountersButNotScores() {
        val p = RaPopups()
        p.leaderboardNotes = false
        p.add(lb(RaEvent.LB_STARTED), 0)
        p.add(lb(RaEvent.LB_FAILED), 0)
        p.add(lb(RaEvent.TRACKER_SHOW, progress = "7", id = 1), 0)
        assertNull(p.view().card)
        assertTrue(p.view().trackers.isEmpty())
        p.add(lb(RaEvent.LB_SUBMITTED, progress = "7"), 0)
        assertEquals("Score 7 · sending", p.view().card!!.text)
        p.leaderboardNotes = true // switched on mid-game: the counter going on shows
        assertEquals(listOf(RaPopups.Tracker(1, "7")), p.view().trackers)
    }
```

New `app/src/test/java/com/tengen/gbaemu/LeaderboardsTest.kt`:

```kotlin
package com.tengen.gbaemu

import org.junit.Assert.assertEquals
import org.junit.Test

class LeaderboardsTest {
    private fun parts(vararg s: String) = s.map { it.toByteArray() }.toTypedArray()

    @Test
    fun boardsAreReadFourTextsEach() {
        assertEquals(listOf(RaBoard(11, true, "Sprint", "Finish fast"), RaBoard(12, false, "Survival", "Last long")),
            RaBoard.list(parts("11", "1", "Sprint", "Finish fast", "12", "0", "Survival", "Last long")))
    }

    @Test
    fun rowsAreReadFiveTextsEach() {
        assertEquals(listOf(RaEntry(1, false, false, "Ace", "0:05.00"), RaEntry(17, true, true, "Tester", "0:12.00")),
            RaEntry.list(parts("1", "0", "0", "Ace", "0:05.00", "17", "1", "1", "Tester", "0:12.00")))
    }

    @Test
    fun scoresAreLoadingFailedOrLoaded() {
        val rows = listOf(RaEntry(1, true, false, "Tester", "1"))
        fun of(vararg status: Int, message: String = "") = BoardScores.of(status, { message }, { rows })
        assertEquals(BoardScores.Loading, of(NativeBridge.RA_BUSY, 0, 0, 0))
        assertEquals(BoardScores.Loading, of(NativeBridge.RA_NOTHING, 0, 0, 0))
        assertEquals(BoardScores.Failed("Couldn't load scores — no connection"), of(NativeBridge.RA_FAILED, BoardScores.RC_NO_RESPONSE, 0, 0))
        assertEquals(BoardScores.Failed("Couldn't load scores: Unknown leaderboard"),
            of(NativeBridge.RA_FAILED, -1, 0, 0, message = "Unknown leaderboard"))
        assertEquals(BoardScores.Failed("Couldn't load scores"), of(NativeBridge.RA_FAILED, -1, 0, 0))
        assertEquals(BoardScores.Loaded(rows, 30, true), of(NativeBridge.RA_OK, 0, 30, 1))
    }
}
```

`SettingsTest` — in the defaults test add `assertEquals(true, s.leaderboardNotes)`; in `everySettingIsSavedAndReadBack` add `leaderboardNotes = false` to the `Settings(…)`.

`FakeRaServerTest` — in `everyGameHasTheDemoSetUnlessMadeUnknown` change the count to 7 (5 achievements + 2 boards) and add `assertTrue("\"Title\":\"Sprint\"" in text(set))`; add:

```kotlin
    @Test
    fun sprintHasThirtyPlayersWithTesterSeventeenthAndSurvivalNone() {
        val top = text(ask("r=lbinfo&i=${FakeRaServer.SPRINT_ID}&c=10"))
        assertTrue(top, "\"TotalEntries\":30" in top)
        assertEquals(10, Regex("\"Rank\":").findAll(top).count())
        assertTrue("\"Rank\":1," in top && "\"Rank\":10," in top)
        val around = text(ask("r=lbinfo&i=${FakeRaServer.SPRINT_ID}&u=Tester&c=5"))
        assertEquals((15..19).toList(), Regex("\"Rank\":(\\d+)").findAll(around).map { it.groupValues[1].toInt() }.toList())
        assertTrue("\"User\":\"Tester\",\"Rank\":17" in around)
        assertTrue("\"TotalEntries\":0" in text(ask("r=lbinfo&i=${FakeRaServer.SURVIVAL_ID}&c=10")))
    }

    @Test
    fun aScoreIsRankedFourthOfThirtyOne() {
        val r = text(ask("r=submitlbentry&u=Tester&t=TOKEN&i=${FakeRaServer.SPRINT_ID}&s=600&m=abc&v=x"))
        assertTrue(r, "\"Score\":600" in r && "\"Rank\":4" in r && "\"NumEntries\":\"31\"" in r)
    }
```

- [x] **Step 2: Run them to see them fail**

Run: `bash tools/dev.sh jvmtest`
Expected: compile errors (`leaderboardNotes`, `Tracker`, `trackers`, `rank` on `Card`, `RaBoard`, `RaEntry`, `BoardScores`, `SPRINT_ID`, `SURVIVAL_ID`).

- [x] **Step 3: `RaPopups`** — in `RaPopups.kt`:

```kotlin
class RaPopups {
    data class Card(val kind: Int, val title: String, val text: String, val points: Int, val badgeUrl: String, val rank: Int = 0)
    data class Challenge(val id: Int, val badgeUrl: String)
    /** A leaderboard's live counter (15): rcheevos' text, e.g. a time "1:23.45" or a score. */
    data class Tracker(val id: Int, val text: String)
    data class View(val card: Card? = null, val progress: Card? = null, val challenges: List<Challenge> = emptyList(),
                    val trackers: List<Tracker> = emptyList())

    /** Settings → "Leaderboard notes on screen" (15): the started / failed notes and the counters; scores always show. */
    var leaderboardNotes = true

    private val waiting = ArrayDeque<Card>()
    private var card: Card? = null
    private var cardUntil = 0L
    private var progress: Card? = null
    private var progressUntil = 0L
    private val challenges = mutableListOf<Challenge>()
    private val trackers = mutableListOf<Tracker>() // every counter going on, shown or not (the switch may change)
```

In `add`, before the closing brace of `when`, add:

```kotlin
            RaEvent.LB_STARTED -> if (leaderboardNotes) waiting += Card(e.kind, e.title, "Leaderboard started", 0, "")
            RaEvent.LB_FAILED -> if (leaderboardNotes) waiting += Card(e.kind, e.title, "Leaderboard failed", 0, "")
            RaEvent.LB_SUBMITTED -> waiting += Card(e.kind, e.title, "Score ${e.progress} · sending", 0, "")
            RaEvent.LB_SCOREBOARD -> waiting += Card(e.kind, e.title, scoreLine(e), 0, "", rank = e.rank)
            RaEvent.LB_ERROR -> waiting += Card(e.kind, "Couldn't send a score",
                listOf(e.title, e.description).filter { it.isNotEmpty() }.joinToString(": "), 0, "")
            RaEvent.TRACKER_SHOW, RaEvent.TRACKER_UPDATE -> {
                val t = Tracker(e.id, e.progress)
                val i = trackers.indexOfFirst { it.id == e.id }
                if (i >= 0) trackers[i] = t else trackers += t
            }
            RaEvent.TRACKER_HIDE -> trackers.removeAll { it.id == e.id }
```

In `tick`, the card's time: `RaEvent.UNLOCKED, RaEvent.LB_SCOREBOARD -> UNLOCK_MS`. Then:

```kotlin
    fun view() = View(card, progress, challenges.toList(), if (leaderboardNotes) trackers.take(MAX_TRACKERS) else emptyList())

    fun clear() {
        waiting.clear()
        card = null
        progress = null
        challenges.clear()
        trackers.clear()
    }
```

and in the companion:

```kotlin
        const val MAX_TRACKERS = 3

        /** "Rank 4 of 1,204 · 0:10.00 · best 0:09.50" (the best only when it isn't this score). */
        fun scoreLine(e: RaEvent): String {
            fun n(v: Int) = String.format(java.util.Locale.US, "%,d", v)
            return "Rank ${n(e.rank)} of ${n(e.entries)} · ${e.progress}" +
                if (e.best.isNotEmpty() && e.best != e.progress) " · best ${e.best}" else ""
        }
```

- [x] **Step 4: `Leaderboards.kt`** (new):

```kotlin
package com.tengen.gbaemu

/** One of the game's leaderboards (15), from NativeBridge.raLeaderboards. */
data class RaBoard(val id: Int, val lowerIsBetter: Boolean, val title: String, val description: String) {
    companion object {
        fun list(parts: Array<ByteArray>): List<RaBoard> = parts.map { String(it, Charsets.UTF_8) }.chunked(4)
            .filter { it.size == 4 }
            .map { p -> RaBoard(p[0].toIntOrNull() ?: 0, p[1] == "1", p[2], p[3]) }
    }
}

/** A row of a board's scores (NativeBridge.raEntries). [gapBefore]: places are missing above it. */
data class RaEntry(val rank: Int, val you: Boolean, val gapBefore: Boolean, val name: String, val score: String) {
    companion object {
        fun list(parts: Array<ByteArray>): List<RaEntry> = parts.map { String(it, Charsets.UTF_8) }.chunked(5)
            .filter { it.size == 5 }
            .map { p -> RaEntry(p[0].toIntOrNull() ?: 0, p[1] == "1", p[2] == "1", p[3], p[4]) }
    }
}

/** What a board's page shows (15). */
sealed interface BoardScores {
    data object Loading : BoardScores
    data class Failed(val message: String) : BoardScores
    data class Loaded(val rows: List<RaEntry>, val total: Int, val onBoard: Boolean) : BoardScores

    companion object {
        const val RC_NO_RESPONSE = -32 // rcheevos: no answer (no internet)

        /** From NativeBridge.raEntriesStatus' [status, code, total, on the board], the message and the rows (read only when needed). */
        fun of(status: IntArray, message: () -> String, rows: () -> List<RaEntry>): BoardScores = when (status[0]) {
            NativeBridge.RA_OK -> Loaded(rows(), status[2], status[3] == 1)
            NativeBridge.RA_FAILED ->
                if (status[1] == RC_NO_RESPONSE) Failed("Couldn't load scores — no connection")
                else Failed(message().let { if (it.isEmpty()) "Couldn't load scores" else "Couldn't load scores: $it" })
            else -> Loading
        }
    }
}
```

(If Task 2 found a different offline code, use it for `RC_NO_RESPONSE`.)

- [x] **Step 5: The setting** — `Settings.kt`: after `hardcore`, add

```kotlin
    // Leaderboards (15): the started / failed notes and the live counters over the game; scores always show.
    val leaderboardNotes: Boolean = true,
```

`save`: `store.put("ra_lb_notes", flag(leaderboardNotes))`; `load`: `leaderboardNotes = bool(store, "ra_lb_notes", d.leaderboardNotes),`.

- [x] **Step 6: The app's pretend server** — `FakeRaServer.kt`: update the class comment (add: "two demo boards, run only in hardcore (rcheevos' rule): Sprint starts at 2 s, counts the frames and is sent at 12 s (ranked 4th of 31); Survival starts at 4 s and fails at 15 s. Scores: Sprint has 30 players with Tester 17th, Survival none.") and add:

```kotlin
    const val SPRINT_ID = 11
    const val SURVIVAL_ID = 12
    private const val USER_RANK = 17

    private fun board(id: Int, title: String, desc: String, format: String, lower: Boolean, mem: String) =
        "{\"ID\":$id,\"Title\":\"$title\",\"Description\":\"$desc\",\"Mem\":\"$mem\",\"Format\":\"$format\"," +
            "\"LowerIsBetter\":$lower,\"Hidden\":false}"

    // Started, counted and submitted by hit counts alone, so they run in any game (checked on the PC by
    // ra_a_hit_count_value_counts_frames).
    private val DEMO_BOARDS = listOf(
        board(SPRINT_ID, "Sprint", "Finish as fast as you can", "FRAMES", true,
            "STA:0xH000000>=0.120.::CAN:0=1::SUB:0xH000000>=0.720.::VAL:M:0xH000000>=0"),
        board(SURVIVAL_ID, "Survival", "Last as long as you can", "VALUE", false,
            "STA:0xH000000>=0.240.::CAN:0xH000000>=0.900.::SUB:0=1::VAL:M:0xH000000>=0"),
    ).joinToString(",", "[", "]")
```

(Use the value form Task 1 settled on.) In the `achievementsets` answer replace `\"Leaderboards\":[]` by `\"Leaderboards\":$DEMO_BOARDS`. Add two branches before `else`:

```kotlin
            "lbinfo" -> {
                val id = p["i"]?.toIntOrNull() ?: 0
                val count = p["c"]?.toIntOrNull() ?: 10
                val total = if (id == SPRINT_ID) 30 else 0
                val first = if (p["u"] != null) (USER_RANK - count / 2).coerceIn(1, maxOf(1, total - count + 1))
                    else (p["o"]?.toIntOrNull() ?: 0) + 1
                val rows = (first until first + count).filter { it <= total }.joinToString(",") { r ->
                    val name = if (r == USER_RANK) "Tester" else String.format(java.util.Locale.US, "Player%02d", r)
                    "{\"User\":\"$name\",\"Rank\":$r,\"Index\":$r,\"Score\":${540 + 30 * r},\"DateSubmitted\":1767225600}"
                }
                say(200, "{\"Success\":true,\"LeaderboardData\":{\"LBID\":$id,\"LBFormat\":\"FRAMES\",\"LowerIsBetter\":1," +
                    "\"LBTitle\":\"Sprint\",\"LBDesc\":\"-\",\"LBMem\":\"-\",\"GameID\":1234,\"LBAuthor\":\"Test\"," +
                    "\"LBCreated\":\"2026-01-01 00:00:00\",\"LBUpdated\":\"2026-01-01 00:00:00\",\"Entries\":[$rows]," +
                    "\"TotalEntries\":$total}}")
            }
            "submitlbentry" -> {
                val s = p["s"] ?: "0"
                say(200, "{\"Success\":true,\"Response\":{\"Score\":$s,\"BestScore\":$s,\"LBData\":{\"Format\":\"FRAMES\"," +
                    "\"LeaderboardID\":${p["i"]},\"GameID\":1234,\"Title\":\"Sprint\",\"LowerIsBetter\":1}," +
                    "\"RankInfo\":{\"Rank\":4,\"NumEntries\":\"31\",\"LowerIsBetter\":1,\"UserRank\":4}," +
                    "\"TopEntries\":[{\"User\":\"Player01\",\"Rank\":1,\"Score\":570}]}}")
            }
```

- [x] **Step 7: Run the JVM tests to see them pass**

Run: `bash tools/dev.sh jvmtest`
Expected: Kotlin 232/232 (222 + 5 RaPopups + 3 Leaderboards + 2 FakeRaServer; the changed ones keep their count).

- [x] **Step 8: The on-device tests** — in `AchievementsDeviceTest`, add:

```kotlin
    @Test
    fun aDemoBoardIsSentAndRankedInHardcore() {
        NativeBridge.raSetHardcore(true) // rcheevos runs boards only in hardcore
        gameWithSet()
        assertTrue(NativeBridge.raHasLeaderboards())
        NativeBridge.start()
        val seen = mutableListOf<RaEvent>()
        assertTrue(waitFor(25000) {
            generateSequence { RaEvent.of(NativeBridge.raTakeEvent()) }.forEach { seen += it }
            seen.any { it.kind == RaEvent.LB_SCOREBOARD }
        })
        NativeBridge.pause()
        assertTrue(seen.any { it.kind == RaEvent.LB_STARTED && it.id == FakeRaServer.SPRINT_ID })
        assertTrue(seen.any { it.kind == RaEvent.TRACKER_SHOW })
        val score = seen.first { it.kind == RaEvent.LB_SCOREBOARD }
        assertEquals("Sprint", score.title)
        assertEquals(4, score.rank)
        assertEquals(31, score.entries)
        assertTrue(score.progress, score.progress.startsWith("0:10."))
    }

    @Test
    fun aBoardsScoresLoadInSoftcoreToo() {
        NativeBridge.raSetHardcore(false)
        gameWithSet()
        assertEquals(listOf("Sprint", "Survival"), RaBoard.list(NativeBridge.raLeaderboards()).map { it.title })
        NativeBridge.raFetchEntries(FakeRaServer.SPRINT_ID)
        assertTrue(waitFor(5000) { NativeBridge.raEntriesStatus()[0] == NativeBridge.RA_OK })
        assertEquals(listOf(NativeBridge.RA_OK, 0, 30, 1), NativeBridge.raEntriesStatus().toList())
        val rows = RaEntry.list(NativeBridge.raEntries())
        assertEquals(15, rows.size)
        assertTrue(rows[10].gapBefore)
        assertEquals(RaEntry(17, true, false, "Tester", rows[12].score), rows[12])
        NativeBridge.raFetchEntries(FakeRaServer.SURVIVAL_ID)
        assertTrue(waitFor(5000) { NativeBridge.raEntriesStatus()[0] == NativeBridge.RA_OK })
        assertEquals(0, NativeBridge.raEntriesStatus()[2])
    }
```

(Sprint: started at frame 120, sent at frame 720 → 600 frames counted ≈ "0:10.0x". The `@After` already puts hardcore back on.)

- [x] **Step 9: Run them**

Run: `bash tools/dev.sh androidtest`
Expected: on-device 85/85.

- [x] **Step 10: Commit (with the documents)**

```bash
git add app/src/main/java/com/tengen/gbaemu/{RaPopups,Leaderboards,Settings,FakeRaServer}.kt app/src/test/java/com/tengen/gbaemu/{RaPopupsTest,LeaderboardsTest,SettingsTest,FakeRaServerTest}.kt app/src/androidTest/java/com/tengen/gbaemu/AchievementsDeviceTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-06-milestone-15-leaderboards.md
git commit -m "feat(app): leaderboard cards and counters, boards and scores, the notes setting, the pretend server's demo boards

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

## Task 6: On the game screen — counter pills, leaderboard cards, the Settings switch (impeccable)

**Files:**
- Modify: `app/src/main/java/com/tengen/gbaemu/AchievementScreens.kt`, `SettingsScreens.kt`, `MainActivity.kt`

Through the **impeccable** skill as an extension of the Game Pak surface (read `DESIGN.md`, the direction contract `impeccable surface-brief read app/src/main/java/com/tengen/gbaemu/Screens.kt`, and milestone 14's notes in PROGRESS.md: pills on the scrim like the speed label, cards as little moulded cartridges, the accent only for what can be pressed, label lettering for printed names, label red for problems). The code below is the starting point; the skill may change the look within those rules — record what changed in the task's Completed entry.

- [x] **Step 1: The counter pills** — in `RaPopupLayer`, after the Row of challenge / progress pills, add a row of counter pills:

```kotlin
        // Leaderboard counters (15): one pill each, on the scrim like the speed label; tabular figures so they don't jitter.
        if (view.trackers.isNotEmpty()) {
            Row(Modifier.padding(top = 6.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                view.trackers.forEach { t ->
                    Row(Modifier.background(Scrim, RoundedCornerShape(50)).padding(horizontal = 10.dp, vertical = 6.dp)
                        .clearAndSetSemantics { contentDescription = "Leaderboard: ${t.text}" },
                        verticalAlignment = Alignment.CenterVertically) {
                        Icon(Icons.Filled.Timer, contentDescription = null, tint = Color.White, modifier = Modifier.size(16.dp))
                        Text(t.text, color = Color.White, style = MaterialTheme.typography.labelLarge.merge(Tabular),
                            modifier = Modifier.padding(start = 6.dp))
                    }
                }
            }
        }
```

(import `androidx.compose.material.icons.filled.Timer`.)

- [x] **Step 2: The leaderboard cards** — in `RaCard`: the spoken text for a score: `RaEvent.LB_SCOREBOARD -> "${card.title}: ${card.text}"` (the `else` covers the rest); the problem colour also for `LB_ERROR` (`card.kind == RaEvent.SERVER_ERROR || card.kind == RaEvent.LB_ERROR`); and for a score, the rank printed at the right like the points:

```kotlin
            if (card.points > 0) Points(card.points, unlocked = true)
            if (card.kind == RaEvent.LB_SCOREBOARD && card.rank > 0) RankStamp(card.rank)
```

```kotlin
/** A score's place, printed like the points: "#4" in label lettering with a small "rank" tag under it. */
@Composable
private fun RankStamp(rank: Int) {
    Column(Modifier.padding(start = 10.dp).widthIn(min = 32.dp), horizontalAlignment = Alignment.CenterHorizontally) {
        Text("#$rank", style = MaterialTheme.typography.titleMedium.merge(Tabular), color = MaterialTheme.colorScheme.onSurface)
        Text("rank", style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
    }
}
```

The started / failed / sending cards use the existing `else` branch (title in body medium, the text under it).

- [x] **Step 3: The switch** — `SettingsScreens.kt`, in the RetroAchievements page's last `Group`, after the Hardcore `SwitchRow`:

```kotlin
        SwitchRow("Leaderboard notes on screen", s.leaderboardNotes, { onChange(s.copy(leaderboardNotes = it)) },
            help = "When a leaderboard attempt starts or fails, and its live counter. Your score and rank always show. Leaderboards run only in Hardcore mode.")
```

- [x] **Step 4: Wiring** — `MainActivity.applySettings`: after `NativeBridge.raSetHardcore(s.hardcore)` add

```kotlin
        raPopups.leaderboardNotes = s.leaderboardNotes
        raView = raPopups.view()
```

- [x] **Step 5: Build and check on the emulator.** `bash tools/dev.sh jvmtest` (232/232), `bash tools/dev.sh install`; switch on the pretend server (`MSYS_NO_PATHCONV=1 adb shell run-as com.tengen.gbaemu touch files/ra-fake`, force-stop, launch), sign in as `Tester` / `secret`, switch Hardcore on, open m7_demo (copied in `/sdcard/Download/GBA/` if missing: `bash tools/dev.sh push tests/roms/tonc/m7_demo.gba`), and take screenshots every 1-2 s for 20 s: the "Sprint · Leaderboard started" card at ~2 s, the counter pill(s) (Sprint's time and Survival's value), "Score 0:10.0x · sending" then "Rank 4 of 31" with `#4` at ~12 s, "Survival · Leaderboard failed" at ~15 s. Then switch "Leaderboard notes on screen" off in Settings and play again: no started / failed cards, no pills, the score card still shows. Light and dark. Screenshots in `.impeccable/review/lb-*.png` (gitignored). Fix what the impeccable rules flag.

- [x] **Step 6: Commit (with the documents)**

```bash
git add app/src/main/java/com/tengen/gbaemu/{AchievementScreens,SettingsScreens,MainActivity}.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-06-milestone-15-leaderboards.md
git commit -m "feat(app): leaderboard counters and score cards over the game; the Leaderboard notes switch

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

## Task 7: The Leaderboards page in the pause menu (impeccable)

**Files:**
- Modify: `app/src/main/java/com/tengen/gbaemu/AchievementScreens.kt`, `Screens.kt`, `MainActivity.kt`, `app/src/androidTest/.../ScreensTest.kt`

Through the **impeccable** skill, as in Task 6 (the Achievements panel is the model: `MenuLayer`, `ScreenHeader`, rows with dividers, the list cut partway through a row when it scrolls — `listCut`).

- [x] **Step 1: The failing screen test** — in `ScreensTest` (on-device Compose tests; follow the file's existing pattern of `setContent { AppTheme { … } }` and `onNodeWithText`), add:

```kotlin
    @Test
    fun theLeaderboardsPageListsBoardsThenAScoreTableWithTheUsersRowMarked() {
        var open by mutableStateOf<RaBoard?>(null)
        val boards = listOf(RaBoard(11, true, "Sprint", "Finish fast"), RaBoard(12, false, "Survival", "Last long"))
        val rows = (1..10).map { RaEntry(it, false, false, "Player$it", "0:0$it.00".take(7)) } +
            RaEntry(17, true, true, "Tester", "0:12.00")
        compose.setContent {
            AppTheme {
                LeaderboardsPanel("Demo Set", boards, hardcore = false, open = open,
                    scores = if (open == null) null else BoardScores.Loaded(rows, 30, true),
                    onOpen = { open = it }, onRetry = {}, onBack = { open = null })
            }
        }
        compose.onNodeWithText("Scores are only sent in Hardcore mode").assertExists()
        compose.onNodeWithText("Sprint").performClick()
        compose.onNodeWithText("Tester").assertExists()
        compose.onNodeWithContentDescription("You: rank 17, Tester, 0:12.00").assertExists()
        compose.onNodeWithText("30 players").assertExists()
    }

    @Test
    fun aBoardThatFailedOffersTryAgainAndAnEmptyOneSaysNoScoresYet() {
        var tries = 0
        var scores by mutableStateOf<BoardScores>(BoardScores.Failed("Couldn't load scores — no connection"))
        val board = RaBoard(12, false, "Survival", "Last long")
        compose.setContent {
            AppTheme {
                LeaderboardsPanel("Demo Set", listOf(board), hardcore = true, open = board, scores = scores,
                    onOpen = {}, onRetry = { tries++ }, onBack = {})
            }
        }
        compose.onNodeWithText("Couldn't load scores — no connection").assertExists()
        compose.onNodeWithText("Try again").performClick()
        assertEquals(1, tries)
        scores = BoardScores.Loaded(emptyList(), 0, false)
        compose.onNodeWithText("No scores yet").assertExists()
    }
```

(Check `ScreensTest`'s existing imports and rule name — use the same `compose` rule name it uses; `AppTheme`'s parameters as in the other tests there.)

- [x] **Step 2: Run it to see it fail**

Run: `bash tools/dev.sh androidtest`
Expected: compile error — `LeaderboardsPanel` unknown.

- [x] **Step 3: The panel** — in `AchievementScreens.kt` (imports as needed: `clickable`, `CircularProgressIndicator`, `OutlinedButton`, `Icons.AutoMirrored.Filled.KeyboardArrowRight` or `Icons.Filled.ChevronRight`, `semantics`):

```kotlin
/**
 * Pause menu → Leaderboards (15): the game's boards; an opened board shows its scores — the top 10, then (when the
 * user is further down) a gap and the rows around the user, the user's row marked. [scores]: null while no board is open.
 */
@Composable
fun LeaderboardsPanel(game: String, boards: List<RaBoard>, hardcore: Boolean, open: RaBoard?, scores: BoardScores?,
                      onOpen: (RaBoard) -> Unit, onRetry: () -> Unit, onBack: () -> Unit) {
    MenuLayer {
        Column(Modifier.padding(bottom = 16.dp).widthIn(min = 300.dp)) {
            if (open == null) {
                ScreenHeader("Leaderboards", onBack = onBack, subtitle = game)
                if (!hardcore) Text("Scores are only sent in Hardcore mode", style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant, modifier = Modifier.padding(top = 4.dp, bottom = 6.dp))
                Column(Modifier.heightIn(max = 420.dp).verticalScroll(rememberScrollState())) {
                    boards.forEachIndexed { i, b ->
                        Row(Modifier.fillMaxWidth().heightIn(min = 56.dp).clickable { onOpen(b) }.padding(vertical = 10.dp),
                            verticalAlignment = Alignment.CenterVertically) {
                            Column(Modifier.weight(1f)) {
                                Text(b.title, style = MaterialTheme.typography.titleSmall)
                                if (b.description.isNotEmpty()) Text(b.description, style = MaterialTheme.typography.bodySmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant, maxLines = 2, overflow = TextOverflow.Ellipsis)
                            }
                            Icon(Icons.AutoMirrored.Filled.KeyboardArrowRight, contentDescription = null,
                                tint = MaterialTheme.colorScheme.onSurfaceVariant)
                        }
                        if (i < boards.lastIndex) HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
                    }
                }
            } else {
                ScreenHeader(open.title, onBack = onBack, subtitle = open.description.ifEmpty { null })
                when (scores) {
                    null, BoardScores.Loading -> Box(Modifier.fillMaxWidth().padding(vertical = 32.dp), contentAlignment = Alignment.Center) {
                        CircularProgressIndicator()
                    }
                    is BoardScores.Failed -> Column(Modifier.padding(vertical = 12.dp)) {
                        Text(scores.message, style = MaterialTheme.typography.bodyMedium)
                        Spacer(Modifier.height(12.dp))
                        OutlinedButton(onClick = onRetry) { Text("Try again") }
                    }
                    is BoardScores.Loaded -> ScoreTable(scores)
                }
            }
        }
    }
}

@Composable
private fun ScoreTable(s: BoardScores.Loaded) {
    if (s.rows.isEmpty()) {
        Text("No scores yet", style = MaterialTheme.typography.bodyMedium, modifier = Modifier.padding(vertical = 16.dp))
        return
    }
    Text((if (s.total == 1) "1 player" else String.format(java.util.Locale.US, "%,d players", s.total)) +
        if (!s.onBoard) " · You haven't set a score yet" else "",
        style = MaterialTheme.typography.bodyMedium.merge(Tabular), color = MaterialTheme.colorScheme.onSurfaceVariant,
        modifier = Modifier.padding(top = 4.dp, bottom = 6.dp))
    Column(Modifier.heightIn(max = 420.dp).verticalScroll(rememberScrollState())) {
        s.rows.forEachIndexed { i, r ->
            if (r.gapBefore) Text("⋮", style = MaterialTheme.typography.titleMedium, color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.fillMaxWidth().padding(vertical = 2.dp).clearAndSetSemantics { contentDescription = "Further down" },
                textAlign = TextAlign.Center)
            else if (i > 0) HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
            val pak = LocalPak.current
            Row(Modifier.fillMaxWidth()
                .then(if (r.you) Modifier.background(pak.cartHigh, RoundedCornerShape(4.dp)) else Modifier)
                .padding(horizontal = 8.dp, vertical = 10.dp)
                .clearAndSetSemantics { contentDescription = (if (r.you) "You: " else "") + "rank ${r.rank}, ${r.name}, ${r.score}" },
                verticalAlignment = Alignment.CenterVertically) {
                Text("${r.rank}", style = MaterialTheme.typography.titleSmall.merge(Tabular), modifier = Modifier.widthIn(min = 40.dp))
                Text(r.name, style = MaterialTheme.typography.bodyMedium, fontWeight = if (r.you) FontWeight.Bold else null,
                    modifier = Modifier.weight(1f), maxLines = 1, overflow = TextOverflow.Ellipsis)
                Text(r.score, style = MaterialTheme.typography.bodyMedium.merge(Tabular))
            }
        }
    }
}
```

(The vertical-ellipsis "⋮" is a symbol, not a label "…"; the impeccable pass may draw the gap differently. Use the scroll cut of the Achievements panel (`listCut`) if the table scrolls.)

- [x] **Step 4: The menu row** — `Screens.kt`: `enum class MenuPage { MAIN, SAVE, LOAD, CHEATS, ACHIEVEMENTS, LEADERBOARDS }`; `GameMenu` and `MenuTiles` gain `onLeaderboards: (() -> Unit)? = null` (passed through like `onAchievements`), and in `MenuTiles` after the Achievements row:

```kotlin
        if (onLeaderboards != null) {
            MenuRow(Icons.Filled.Leaderboard, "Leaderboards", onLeaderboards)
            HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
        }
```

(import `androidx.compose.material.icons.filled.Leaderboard`.)

- [x] **Step 5: Wiring** — `MainActivity.kt`:

State (next to `raList`):

```kotlin
    private var raBoards by mutableStateOf(emptyList<RaBoard>()) // the game's leaderboards (15); empty: no Leaderboards row
    private var openBoard by mutableStateOf<RaBoard?>(null)
    private var boardScores by mutableStateOf<BoardScores?>(null)
    private var boardAsk by mutableStateOf(0) // a new number asks the server again (Try again)
```

In `useAchievements`, after the `when` (before `raView = …`): `raBoards = if (game is Achievements.Game.Offline) emptyList() else RaBoard.list(NativeBridge.raLeaderboards())`. Where `raSet = false` is set (opening a game, quitting, signing out): also `raBoards = emptyList(); openBoard = null`.

`GameMenu(…)`: `onLeaderboards = if (raBoards.isNotEmpty()) ({ openBoard = null; menu = MenuPage.LEADERBOARDS }) else null`.

In the `when (menu)`:

```kotlin
                    MenuPage.LEADERBOARDS -> {
                        LeaderboardsPanel(String(NativeBridge.raGameTitle(), Charsets.UTF_8).ifEmpty { currentTitle }, raBoards,
                            hardcore = hardcoreOn, open = openBoard, scores = boardScores,
                            onOpen = { openBoard = it; boardAsk++ }, onRetry = { boardAsk++ },
                            onBack = { if (openBoard != null) openBoard = null else menu = MenuPage.MAIN })
                        val b = openBoard
                        if (b != null) LaunchedEffect(b.id, boardAsk) { // the board's scores, asked for and waited on
                            boardScores = BoardScores.Loading
                            NativeBridge.raFetchEntries(b.id)
                            while (true) {
                                val s = BoardScores.of(NativeBridge.raEntriesStatus(),
                                    { String(NativeBridge.raEntriesMessage(), Charsets.UTF_8) },
                                    { RaEntry.list(NativeBridge.raEntries()) })
                                boardScores = s
                                if (s !is BoardScores.Loading) break
                                delay(100)
                            }
                        }
                    }
```

The Back handler for the menu: `if (menu == MenuPage.MAIN) resumeGame() else if (menu == MenuPage.LEADERBOARDS && openBoard != null) openBoard = null else menu = MenuPage.MAIN`.

(`hardcore = hardcoreOn` is right: the "only sent in Hardcore" line shows whenever the limits — and so the boards — aren't running.)

- [x] **Step 6: Run the tests**

Run: `bash tools/dev.sh jvmtest`, `bash tools/dev.sh androidtest`
Expected: Kotlin 232/232, on-device 87/87.

- [x] **Step 7: Check on the emulator** with the pretend server, signed in, m7_demo: pause → Leaderboards → the two boards and the Hardcore line (Hardcore off) → Sprint: the top 10, the gap, Tester's row marked, "30 players" → Back → Survival: "No scores yet". The pretend server never fails, so the failed state ("Couldn't load scores — no connection", Try again) is looked at through a temporary capture test (not committed, as in M12) that draws `LeaderboardsPanel` with `BoardScores.Failed`. Light and dark, upright and sideways, font 1.3×. Screenshots in `.impeccable/review/lb-*.png`. Put the emulator's settings back (font 1.0, light, upright).

- [x] **Step 8: Commit (with the documents)**

```bash
git add app/src/main/java/com/tengen/gbaemu/{AchievementScreens,Screens,MainActivity}.kt app/src/androidTest/java/com/tengen/gbaemu/ScreensTest.kt docs/PROGRESS.md docs/superpowers/plans/2026-10-06-milestone-15-leaderboards.md
git commit -m "feat(app): the Leaderboards page in the pause menu

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

## Task 8: Claude's emulator check and the finish review

- [x] **Step 1: The whole suite** — `bash tools/dev.sh unit` (474), `bash tools/dev.sh test` (CTest 121), `bash tools/dev.sh jvmtest` (232), `bash tools/dev.sh androidtest` (87), `bash tools/dev.sh games --check` (31/31 ok), `python tools/fetch_rcheevos.py --check` (66 of 66 match). Note any count that differs from these (the plan's guesses) in the Completed entry.

- [x] **Step 2: The sweep** on the phone emulator with the pretend server (Hardcore on and off; light and dark; upright and sideways; font 1.3×): every card (started, failed, sending, rank; "Couldn't send a score" through a temporary capture test, not committed, that feeds `RaPopups` an `LB_ERROR` and draws `RaPopupLayer`), the counter pills (two at once), the switch off, the Leaderboards page (list, a board, the gap and the user's row, empty board, the failed state via the capture test), the pause menu with both rows. Screenshots in `.impeccable/review/lb-*.png`.

- [x] **Step 3: The impeccable finish review** — a fresh `impeccable:impeccable-finish-reviewer` agent with the direction contract, `DESIGN.md` and the screenshots; apply its fixes and repeat until it says **`ship`**. Then the `impeccable:impeccable-documenter` agent updates `DESIGN.md` + `.impeccable/design.json` with the new components (counter pill, score card with rank stamp, Leaderboards page, score table, the user's row).

- [x] **Step 4: Clean the emulator** — remove `files/ra-fake`, `files/ra.json` and `files/ra/` (`MSYS_NO_PATHCONV=1 adb shell run-as com.tengen.gbaemu rm -r files/ra-fake files/ra.json files/ra`), airplane mode off, font scale 1.0, upright, light mode, Show speed off, Hardcore off (the default), Leaderboard notes on.

- [x] **Step 5: Commit (with the documents)**

```bash
git add -A
git commit -m "test: milestone 15 emulator check and finish review

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

## Task 9: The user's check, then the milestone exit

- [x] **Step 1: Prepare** — make sure a game the user owns that has RetroAchievements leaderboards is in the emulator's games folder (`/sdcard/Download/GBA/`; game D is there from milestone 14 — check on retroachievements.org's game page, or after signing in, whether it shows the Leaderboards row; if it has none, ask the user which of their games to copy, e.g. game C or a game A game from `~/Downloads`). The app uses the real server (no `files/ra-fake`).

- [x] **Step 2: Stop and ask the user** to sign in with their own account on the phone emulator (Claude never types the password), open that game, pause → Leaderboards, open a board and see the real scores (and their own row if they have a score). Wait for their answer.

- [x] **Step 3: Fix what the user finds** (each fix test-first, its own commit, documents updated).

- [x] **Step 4: The exit** — when the user is satisfied: the spec's status line → implemented (with the finish line met), the parent spec's status line and §10 row 15 → done, `CLAUDE.md` "Key files" (current milestone: none, last finished: 15; current plan: none, last finished: this plan), `docs/PROGRESS.md` (Goal, status table row 15 ✅ with the final numbers, Completed, Next = ask the user what's next, Notes), tick this plan; commit; tag `milestone-15`; ask the user whether to merge into `main`.

```bash
git add -A
git commit -m "docs: milestone 15 done

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git tag milestone-15
```
