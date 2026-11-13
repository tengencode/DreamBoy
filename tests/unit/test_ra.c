/* The RetroAchievements glue (platform/ra, milestone 14) against the pretend server (ra_fake.c). */
#include <stdio.h>
#include <string.h>

#include "ra.h"
#include "ra_fake.h"
#include "rc_api_runtime.h"
#include "rc_error.h"
#include "test.h"

static uint64_t clock_ms = 1000000;
static uint64_t fake_now(void) {
    return clock_ms;
}

static ra_t *new_ra(void) {
    ra_t *ra = ra_create();
    CHECK(ra != NULL);
    ra_set_clock(ra, fake_now);
    return ra;
}

TEST(ra_md5_is_rcheevos_gba_hash) {
    char hex[33];
    ra_md5_hex((const uint8_t *)"", 0, hex);
    CHECK(strcmp(hex, "d41d8cd98f00b204e9800998ecf8427e") == 0);
    ra_md5_hex((const uint8_t *)"abc", 3, hex);
    CHECK(strcmp(hex, "900150983cd24fb0d6963f7d28e17f72") == 0);
}

/* The app's outbox (RaOutbox.kt) re-signs unlocks and scores sent late; these are rcheevos' own signatures it must match. */
TEST(ra_award_and_score_signatures_for_the_outbox) {
    rc_api_request_t req;
    rc_api_award_achievement_request_t award = {0};
    award.username = "Tester";
    award.api_token = "TOKEN";
    award.achievement_id = 1001;
    award.seconds_since_unlock = 90;
    CHECK_EQ(rc_api_init_award_achievement_request(&req, &award), RC_OK);
    CHECK(strstr(req.post_data, "v=e47388663084231e1df43ac2611c6687") != NULL);
    rc_api_destroy_request(&req);
    award.seconds_since_unlock = 0;
    CHECK_EQ(rc_api_init_award_achievement_request(&req, &award), RC_OK);
    CHECK(strstr(req.post_data, "v=ba409acb0b83555ccca0863531b10f6d") != NULL);
    rc_api_destroy_request(&req);
    rc_api_submit_lboard_entry_request_t entry = {0};
    entry.username = "Tester";
    entry.api_token = "TOKEN";
    entry.leaderboard_id = 2001;
    entry.score = 1234;
    entry.seconds_since_completion = 60;
    CHECK_EQ(rc_api_init_submit_lboard_entry_request(&req, &entry), RC_OK);
    CHECK(strstr(req.post_data, "v=725eb09a0efa54931ee41fa8fa759c5e") != NULL);
    rc_api_destroy_request(&req);
}

TEST(ra_requests_wait_in_order_until_answered) {
    ra_t *ra = new_ra();
    ra_login_password(ra, "Tester", "secret");
    ra_request_t r;
    CHECK(ra_take_request(ra, 0, &r));
    CHECK(strstr(r.url, "retroachievements.org") != NULL);
    CHECK(r.post != NULL && strstr(r.post, "r=login2") != NULL);
    ra_request_t none;
    CHECK(!ra_take_request(ra, 0, &none)); /* taken once only */
    CHECK(!ra_take_request(ra, 20, &none)); /* waiting 20 ms finds nothing either */
    CHECK_EQ(ra_login_status(ra, NULL, NULL, 0), RA_BUSY);
    ra_reply(ra, r.id + 1000, 200, "{}", 2); /* an unknown id changes nothing */
    CHECK_EQ(ra_login_status(ra, NULL, NULL, 0), RA_BUSY);
    ra_request_free(&r);
    ra_destroy(ra);
}

TEST(ra_password_sign_in_keeps_the_token_not_the_password) {
    ra_fake_t f = {0};
    ra_t *ra = new_ra();
    ra_login_password(ra, "Tester", "secret");
    CHECK_EQ(ra_fake_serve(&f, ra), 1);
    CHECK(strstr(f.last, "p=secret") != NULL);
    CHECK_EQ(ra_login_status(ra, NULL, NULL, 0), RA_OK);
    char name[64], token[64];
    CHECK(ra_user(ra, name, sizeof name, token, sizeof token));
    CHECK(strcmp(name, "Tester") == 0);
    CHECK(strcmp(token, "TOKEN") == 0);
    ra_destroy(ra);
}

TEST(ra_wrong_password_fails_with_the_servers_message) {
    ra_fake_t f = {0};
    ra_t *ra = new_ra();
    ra_login_password(ra, "Tester", "nope");
    ra_fake_serve(&f, ra);
    int code = 0;
    char message[256];
    CHECK_EQ(ra_login_status(ra, &code, message, sizeof message), RA_FAILED);
    CHECK_EQ((unsigned)code, (unsigned)RC_INVALID_CREDENTIALS);
    CHECK(strstr(message, "Invalid") != NULL);
    char name[64], token[64];
    CHECK(!ra_user(ra, name, sizeof name, token, sizeof token));
    ra_destroy(ra);
}

TEST(ra_token_sign_in_and_an_expired_token) {
    ra_fake_t f = {0};
    ra_t *ra = new_ra();
    ra_login_token(ra, "Tester", "TOKEN");
    ra_fake_serve(&f, ra);
    CHECK(strstr(f.last, "t=TOKEN") != NULL);
    CHECK_EQ(ra_login_status(ra, NULL, NULL, 0), RA_OK);
    ra_logout(ra);
    CHECK_EQ(ra_login_status(ra, NULL, NULL, 0), RA_NOTHING);
    ra_login_token(ra, "Tester", "EXPIRED");
    ra_fake_serve(&f, ra);
    int code = 0;
    CHECK_EQ(ra_login_status(ra, &code, NULL, 0), RA_FAILED);
    CHECK_EQ((unsigned)code, (unsigned)RC_EXPIRED_TOKEN);
    ra_destroy(ra);
}

TEST(ra_sign_in_without_internet_fails_but_not_as_bad_credentials) {
    ra_fake_t f = {0};
    f.offline = true;
    ra_t *ra = new_ra();
    ra_login_token(ra, "Tester", "TOKEN");
    ra_fake_serve(&f, ra);
    int code = 0;
    CHECK_EQ(ra_login_status(ra, &code, NULL, 0), RA_FAILED);
    CHECK(code != RC_INVALID_CREDENTIALS && code != RC_EXPIRED_TOKEN && code != RC_ACCESS_DENIED);
    ra_destroy(ra);
}

TEST(ra_user_agent_clause_names_rcheevos) {
    ra_t *ra = new_ra();
    char clause[64];
    ra_user_agent_clause(ra, clause, sizeof clause);
    CHECK(strncmp(clause, "rcheevos/12.5", 13) == 0);
    ra_destroy(ra);
}

/* ---- a game with five made-up achievements ---- */

#define ACH(id, title, desc, points, mem)                                                                       \
    "{\"ID\":" #id ",\"Title\":\"" title "\",\"Description\":\"" desc "\",\"Flags\":3,\"Points\":" #points \
    ",\"MemAddr\":\"" mem "\",\"Author\":\"Test\",\"BadgeName\":\"0" #id "\",\"Created\":1,\"Modified\":1}"
static const char SET[] = "[" ACH(1001, "First Steps", "Set byte 1 to 3", 5, "0xH000001=3") ","
                          ACH(1002, "Collector", "Collect 10", 10, "M:0xH008000=10") ","
                          ACH(1003, "Daredevil", "Win the challenge", 25, "0xH000002=1_T:0xH000003=1") ","
                          ACH(1004, "Saver", "Save once", 1, "0xH048000=66") ","
                          ACH(1005, "Patient", "Hold on for 3 frames", 2, "0xH000004=1.3.") "]";
static const char HASH[] = "0123456789abcdef0123456789abcdef";

#define LB(id, title, desc, fmt, lower, mem)                                                                    \
    "{\"ID\":" #id ",\"Title\":\"" title "\",\"Description\":\"" desc "\",\"Mem\":\"" mem "\",\"Format\":\"" fmt \
    "\",\"LowerIsBetter\":" lower ",\"Hidden\":false}"
/* 501: a time (lower is better) started by byte 0x10, cancelled by 0x11, submitted by 0x12, its value byte 0x13;
 * 502: a score, the same with bytes 0x20-0x23. */
static const char BOARDS[] = "[" LB(501, "Speedrun", "Fastest finish", "FRAMES", "true",
                                    "STA:0xH000010=1::CAN:0xH000011=1::SUB:0xH000012=1::VAL:0xH000013") ","
                             LB(502, "High Score", "Most points", "SCORE", "false",
                                    "STA:0xH000020=1::CAN:0xH000021=1::SUB:0xH000022=1::VAL:0xH000023") "]";

static uint8_t iw[0x8000], ew[0x40000], sr[0x8000];
static ra_memory_t mem(void) {
    ra_memory_t m = {iw, ew, sr, sizeof sr, NULL, NULL};
    return m;
}

static void frames(ra_t *ra, int n) {
    ra_memory_t m = mem();
    for (int i = 0; i < n; i++) ra_do_frame(ra, &m);
}

static void drain(ra_t *ra) {
    ra_event_t e;
    while (ra_take_event(ra, &e)) {
    }
}

/* The next event of this kind and achievement (0 = any); false when there is none waiting. */
static bool next_event(ra_t *ra, int kind, uint32_t id, ra_event_t *out) {
    ra_event_t e;
    while (ra_take_event(ra, &e)) {
        if (e.kind == kind && (!id || e.id == id)) {
            if (out) *out = e;
            return true;
        }
    }
    return false;
}

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

TEST(ra_peek_maps_iwram_ewram_and_save_ram) {
    memset(iw, 0, sizeof iw);
    memset(ew, 0, sizeof ew);
    iw[0] = 1;
    iw[0x7FFF] = 2;
    ew[0] = 3;
    ew[0x3FFFF] = 4;
    sr[0] = 5;
    ra_memory_t m = {iw, ew, sr, 16, NULL, NULL};
    uint8_t b[4] = {0};
    CHECK_EQ(ra_peek(&m, 0x0, b, 1), 1);
    CHECK_EQ(b[0], 1);
    CHECK_EQ(ra_peek(&m, 0x7FFF, b, 2), 2); /* runs on into EWRAM */
    CHECK_EQ(b[0], 2);
    CHECK_EQ(b[1], 3);
    CHECK_EQ(ra_peek(&m, 0x47FFF, b, 2), 2); /* runs on into save RAM */
    CHECK_EQ(b[0], 4);
    CHECK_EQ(b[1], 5);
    CHECK_EQ(ra_peek(&m, 0x48000 + 15, b, 2), 1); /* the save chip has 16 bytes here */
    CHECK_EQ(ra_peek(&m, RA_SRAM_END, b, 1), 0);
    ra_memory_t none = {NULL, NULL, NULL, 0, NULL, NULL};
    CHECK_EQ(ra_peek(&none, 0, b, 1), 0);
    CHECK_EQ(ra_peek(NULL, 0, b, 1), 0);
}

TEST(ra_known_game_loads_its_set) {
    ra_fake_t f = {0};
    ra_t *ra = with_game(&f);
    CHECK(ra_has_set(ra));
    ra_summary_t s;
    CHECK(ra_summary(ra, &s));
    CHECK_EQ(s.total, 5);
    CHECK_EQ(s.unlocked, 0);
    CHECK_EQ(s.points_total, 43);
    CHECK(strcmp(s.title, "Test Game") == 0);
    ra_unload_game(ra);
    CHECK_EQ(ra_game_status(ra, NULL), RA_NOTHING);
    CHECK(!ra_has_set(ra));
    ra_destroy(ra);
}

TEST(ra_unknown_game_and_signed_out_have_no_set) {
    ra_fake_t f = {0};
    ra_t *ra = new_ra();
    ra_load_game(ra, HASH); /* not signed in */
    int code = 0;
    CHECK_EQ(ra_game_status(ra, &code), RA_FAILED);
    CHECK_EQ((unsigned)code, (unsigned)RC_LOGIN_REQUIRED);
    ra_login_password(ra, "Tester", "secret");
    ra_fake_serve(&f, ra);
    ra_load_game(ra, HASH); /* the server knows no game (f.hash is NULL) */
    ra_fake_serve(&f, ra);
    ra_memory_t m = mem();
    ra_idle(ra, &m);
    CHECK_EQ(ra_game_status(ra, &code), RA_FAILED);
    CHECK_EQ((unsigned)code, (unsigned)RC_NO_GAME_LOADED);
    CHECK(!ra_has_set(ra));
    ra_summary_t s;
    CHECK(!ra_summary(ra, &s));
    ra_destroy(ra);
}

TEST(ra_game_load_without_internet_fails_and_is_not_unknown) {
    ra_fake_t f = {0};
    f.hash = HASH;
    ra_t *ra = new_ra();
    ra_login_password(ra, "Tester", "secret");
    ra_fake_serve(&f, ra);
    f.offline = true;
    ra_load_game(ra, HASH);
    ra_fake_serve(&f, ra);
    ra_memory_t m = mem();
    ra_idle(ra, &m);
    int code = 0;
    CHECK_EQ(ra_game_status(ra, &code), RA_FAILED);
    CHECK(code != RC_NO_GAME_LOADED);
    ra_destroy(ra);
}

TEST(ra_unlock_is_one_event_and_one_award) {
    ra_fake_t f = {0};
    ra_t *ra = with_game(&f);
    iw[1] = 3;
    frames(ra, 1);
    ra_event_t e;
    CHECK(next_event(ra, RA_EVENT_UNLOCKED, 1001, &e));
    CHECK(strcmp(e.title, "First Steps") == 0);
    CHECK(strcmp(e.description, "Set byte 1 to 3") == 0);
    CHECK_EQ(e.points, 5);
    CHECK(strstr(e.badge_url, "01001.png") != NULL);
    CHECK_EQ(ra_fake_serve(&f, ra), 1);
    CHECK(strstr(f.last, "r=awardachievement") != NULL && strstr(f.last, "a=1001") != NULL);
    ra_summary_t s;
    ra_summary(ra, &s);
    CHECK_EQ(s.unlocked, 1);
    CHECK_EQ(s.points_unlocked, 5);
    ra_destroy(ra);
}

TEST(ra_save_ram_achievement_unlocks) {
    ra_fake_t f = {0};
    ra_t *ra = with_game(&f);
    sr[0] = 66;
    frames(ra, 1);
    CHECK(next_event(ra, RA_EVENT_UNLOCKED, 1004, NULL));
    ra_destroy(ra);
}

TEST(ra_progress_shows_n_of_m) {
    ra_fake_t f = {0};
    ra_t *ra = with_game(&f);
    ew[0] = 3;
    frames(ra, 1);
    ra_event_t e;
    CHECK(next_event(ra, RA_EVENT_PROGRESS, 1002, &e));
    CHECK(strcmp(e.progress, "3/10") == 0);
    CHECK(strcmp(e.title, "Collector") == 0);
    ra_destroy(ra);
}

TEST(ra_challenge_shows_then_hides_when_won) {
    ra_fake_t f = {0};
    ra_t *ra = with_game(&f);
    iw[2] = 1;
    frames(ra, 1);
    CHECK(next_event(ra, RA_EVENT_CHALLENGE_SHOW, 1003, NULL));
    iw[3] = 1;
    frames(ra, 1);
    ra_event_t got[RA_EVENTS_MAX];
    int n = 0;
    while (n < RA_EVENTS_MAX && ra_take_event(ra, &got[n])) n++;
    bool unlocked = false, hidden = false;
    for (int i = 0; i < n; i++) {
        unlocked |= got[i].kind == RA_EVENT_UNLOCKED && got[i].id == 1003;
        hidden |= got[i].kind == RA_EVENT_CHALLENGE_HIDE && got[i].id == 1003;
    }
    CHECK(unlocked);
    CHECK(hidden);
    ra_destroy(ra);
}

TEST(ra_last_unlock_masters_the_game) {
    ra_fake_t f = {0};
    ra_t *ra = with_game(&f);
    iw[1] = 3;
    ew[0] = 10;
    iw[2] = 1;
    sr[0] = 66;
    iw[4] = 1;
    frames(ra, 1); /* 1001, 1002, 1004 unlock; 1003 is primed; 1005 has 1 hit */
    iw[3] = 1;
    frames(ra, 2); /* 1003 unlocks; 1005 reaches 3 hits */
    CHECK(!next_event(ra, RA_EVENT_MASTERED, 0, NULL)); /* rcheevos waits for the server: "0 left" */
    CHECK_EQ(ra_fake_serve(&f, ra), 5);
    frames(ra, 1); /* the event comes with the next frame */
    ra_event_t e;
    CHECK(next_event(ra, RA_EVENT_MASTERED, 0, &e));
    CHECK(strcmp(e.title, "Test Game") == 0);
    CHECK_EQ(e.points, 43);
    ra_destroy(ra);
}

TEST(ra_refused_unlock_is_a_server_error) {
    ra_fake_t f = {0};
    f.refuse = 1001;
    ra_t *ra = with_game(&f);
    iw[1] = 3;
    frames(ra, 1);
    drain(ra);
    ra_fake_serve(&f, ra);
    ra_event_t e;
    CHECK(next_event(ra, RA_EVENT_SERVER_ERROR, 0, &e));
    CHECK(strstr(e.description, "refused") != NULL);
    ra_destroy(ra);
}

TEST(ra_unlock_without_internet_waits_and_is_sent_later) {
    ra_fake_t f = {0};
    ra_t *ra = with_game(&f);
    f.offline = true;
    iw[1] = 3;
    frames(ra, 1);
    CHECK(next_event(ra, RA_EVENT_UNLOCKED, 1001, NULL)); /* shown at once */
    CHECK(ra_fake_serve(&f, ra) >= 2);                    /* tried, and tried again at once */
    ra_memory_t m = mem();
    ra_idle(ra, &m);
    CHECK(next_event(ra, RA_EVENT_OFFLINE, 0, NULL));
    f.offline = false;
    clock_ms += 1500; /* the next try is 1 s later */
    ra_idle(ra, &m);
    CHECK_EQ(ra_fake_serve(&f, ra), 1);
    CHECK(strstr(f.last, "a=1001") != NULL);
    ra_idle(ra, &m);
    CHECK(next_event(ra, RA_EVENT_ONLINE, 0, NULL));
    ra_destroy(ra);
}

TEST(ra_reset_progress_forgets_hit_counts) {
    ra_fake_t f = {0};
    ra_t *ra = with_game(&f);
    iw[4] = 1;
    frames(ra, 2); /* 2 of 3 hits */
    ra_memory_t m = mem();
    ra_reset_progress(ra, &m);
    frames(ra, 2); /* counting starts again: 2 of 3 */
    CHECK(!next_event(ra, RA_EVENT_UNLOCKED, 1005, NULL));
    frames(ra, 1);
    CHECK(next_event(ra, RA_EVENT_UNLOCKED, 1005, NULL));
    ra_destroy(ra);
}

TEST(ra_hardcore_limits_need_hardcore_a_user_and_a_set) {
    ra_fake_t f = {0};
    ra_t *ra = new_ra();
    CHECK(!ra_hardcore_active(ra)); /* nobody signed in, no game */
    ra_destroy(ra);
    ra = with_game(&f);
    CHECK(ra_hardcore_active(ra)); /* hardcore is rcheevos' default */
    ra_set_hardcore(ra, false);
    CHECK(!ra_hardcore_active(ra));
    ra_set_hardcore(ra, true);
    drain(ra);
    CHECK(ra_hardcore_active(ra));
    ra_unload_game(ra);
    CHECK(!ra_hardcore_active(ra));
    ra_destroy(ra);
}

TEST(ra_quick_repeated_pauses_wait_in_hardcore_only) {
    ra_fake_t f = {0};
    ra_t *ra = with_game(&f);
    uint32_t left = 99;
    CHECK(ra_can_pause(ra, &left));
    CHECK_EQ(left, 0);
    frames(ra, 5);
    CHECK(!ra_can_pause(ra, &left)); /* paused again after only 5 frames */
    CHECK(left > 0);
    ra_set_hardcore(ra, false);
    CHECK(ra_can_pause(ra, &left));
    ra_destroy(ra);
}

TEST(ra_list_has_unlocked_ones_first_with_their_pictures) {
    ra_fake_t f = {0};
    ra_t *ra = with_game(&f);
    sr[0] = 66;
    frames(ra, 1);
    ew[0] = 4;
    frames(ra, 1);
    ra_achievement_t list[RA_LIST_MAX];
    size_t n = ra_achievements(ra, list, RA_LIST_MAX);
    CHECK_EQ(n, 5);
    CHECK_EQ(list[0].id, 1004);
    CHECK(list[0].unlocked);
    CHECK(strstr(list[0].badge_url, "01004.png") != NULL);
    for (size_t i = 1; i < n; i++) CHECK(!list[i].unlocked);
    bool collector = false;
    for (size_t i = 1; i < n; i++) {
        if (list[i].id != 1002) continue;
        collector = true;
        CHECK(strcmp(list[i].progress, "4/10") == 0);
        CHECK(strstr(list[i].badge_url, "_lock") != NULL); /* the grey picture */
    }
    CHECK(collector);
    CHECK_EQ(ra_achievements(ra, list, 2), 2);
    ra_destroy(ra);
}

TEST(ra_full_event_queue_drops_the_oldest) {
    ra_fake_t f = {0};
    ra_t *ra = with_game(&f);
    for (int i = 1; i <= RA_EVENTS_MAX + 5; i++) { /* the challenge comes and goes: an event every frame */
        iw[2] = (uint8_t)(i & 1);
        frames(ra, 1);
    }
    int n = 0;
    ra_event_t e;
    while (ra_take_event(ra, &e)) n++;
    CHECK_EQ(n, RA_EVENTS_MAX);
    ra_destroy(ra);
}

TEST(ra_progress_updates_do_not_push_other_events_out) {
    /* A counter that changes every frame sends a progress update every frame; while nobody takes events (a slow
     * start), they must not fill the queue and push out the challenge shown on the first frame. */
    static const char SET2[] = "[" ACH(2001, "Now", "x", 1, "0xH000000>=0_T:0xH000000>=0.1800.") ","
                               ACH(2002, "Count", "x", 1, "M:0xH000000>=0.1200.") "]";
    ra_fake_t f = {0};
    memset(iw, 0, sizeof iw);
    f.hash = HASH;
    f.achievements = SET2;
    ra_t *ra = new_ra();
    ra_login_password(ra, "Tester", "secret");
    ra_fake_serve(&f, ra);
    ra_load_game(ra, HASH);
    ra_fake_serve(&f, ra);
    ra_memory_t m = mem();
    ra_idle(ra, &m);
    frames(ra, 2 * RA_EVENTS_MAX);
    ra_event_t e, last = {0};
    int challenges = 0, progress = 0;
    while (ra_take_event(ra, &e)) {
        challenges += e.kind == RA_EVENT_CHALLENGE_SHOW && e.id == 2001;
        if (e.kind == RA_EVENT_PROGRESS) {
            progress++;
            last = e;
        }
    }
    CHECK_EQ(challenges, 1);
    CHECK_EQ(progress, 1); /* only the newest */
    CHECK(strcmp(last.progress, "64/1,200") == 0);
    ra_destroy(ra);
}

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

/* ---- a Game Boy game (17c): the pretend server says console 4, the memory is the core's own view ---- */

#include <stdlib.h>

#include "gbtestutil.h"

static uint32_t gb_view(void *ctx, uint32_t address, uint8_t *buffer, uint32_t count) {
    return gb_ra_peek(ctx, address, buffer, count);
}

TEST(ra_a_game_boy_game_unlocks_from_its_own_memory) {
    static const char GB_SET[] = "[" ACH(2001, "Work", "Byte 0xC123 is 7", 5, "0xH00c123=7") ","
                                 ACH(2002, "Banked", "Work RAM bank 3, byte 9 is 1", 5, "0xH011009=1") ","
                                 ACH(2003, "Saved", "Cartridge RAM byte 5 is 2", 5, "0xH00a005=2") "]";
    for (int console = 4; console <= 6; console += 2) { /* a Game Boy game, then a Game Boy Color one */
        ra_fake_t f = {0};
        f.hash = HASH;
        f.achievements = GB_SET;
        f.console = console;
        uint8_t *rom = gbtu_rom(0x10000, 0x1B, 3, console == 6 ? 0x80 : 0x00);
        gb_t *g = gb_create();
        CHECK_EQ(gb_load_rom(g, rom, 0x10000), GB_OK);
        free(rom);
        ra_memory_t m = {.peek = gb_view, .ctx = g};
        ra_t *ra = new_ra();
        ra_login_password(ra, "Tester", "secret");
        ra_fake_serve(&f, ra);
        ra_load_game(ra, HASH);
        ra_fake_serve(&f, ra);
        ra_idle(ra, &m);
        CHECK_EQ(ra_game_status(ra, NULL), RA_OK);
        ra_do_frame(ra, &m);
        ra_do_frame(ra, &m);
        drain(ra);
        g->mem.wram[0x123] = 7;
        g->mem.wram[3 * 0x1000 + 9] = 1; /* (a plain Game Boy has no such address: that achievement never fires) */
        g->cart_ram[5] = 2;
        ra_do_frame(ra, &m);
        ra_event_t list[8];
        int n = take_all(ra, list, 8);
        CHECK(find(list, n, RA_EVENT_UNLOCKED, 2001) != NULL);
        CHECK(find(list, n, RA_EVENT_UNLOCKED, 2003) != NULL);
        CHECK_EQ(find(list, n, RA_EVENT_UNLOCKED, 2002) != NULL, console == 6);
        ra_destroy(ra);
        gb_destroy(g);
    }
}
