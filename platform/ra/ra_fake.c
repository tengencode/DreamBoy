/* A pretend RetroAchievements server (milestone 14): see ra_fake.h. Its answers copy the real server's, as rcheevos'
 * own tests do (test_rc_client.c in the rcheevos release). */
#include "ra_fake.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char LOGIN_OK[] = "{\"Success\":true,\"User\":\"Tester\",\"Token\":\"TOKEN\",\"Score\":100,\"SoftcoreScore\":5,"
                               "\"Messages\":0,\"Permissions\":1,\"AccountType\":\"Registered\"}";
static const char LOGIN_BAD[] = "{\"Success\":false,\"Error\":\"Invalid User/Password combination. Please try again.\","
                                "\"Status\":401,\"Code\":\"invalid_credentials\"}";
static const char LOGIN_EXPIRED[] = "{\"Success\":false,\"Error\":\"The access token has expired. Please log in again.\","
                                    "\"Status\":401,\"Code\":\"expired_token\"}";
static const char NOT_FOUND[] = "{\"Success\":false,\"Error\":\"Unknown game\",\"Code\":\"not_found\",\"Status\":404}";
static const char SESSION[] = "{\"Success\":true,\"Unlocks\":[],\"HardcoreUnlocks\":[]}";
static const char REFUSED[] = "{\"Success\":false,\"Error\":\"The server refused this unlock.\"}";
static const char REFUSED_SCORE[] = "{\"Success\":false,\"Error\":\"The server refused this score.\"}";
static const char FINE[] = "{\"Success\":true}";

/* The value of key in form data "a=1&b=2" (not decoded), or "". */
static void param(const char *form, const char *key, char *out, size_t cap) {
    size_t k = strlen(key);
    out[0] = '\0';
    for (const char *p = form; p && *p;) {
        if (!strncmp(p, key, k) && p[k] == '=') {
            const char *v = p + k + 1;
            size_t n = strcspn(v, "&");
            if (n >= cap) n = cap - 1;
            memcpy(out, v, n);
            out[n] = '\0';
            return;
        }
        p = strchr(p, '&');
        if (p) p++;
    }
}

static void say(ra_t *ra, int id, int status, const char *body) {
    ra_reply(ra, id, status, body, strlen(body));
}

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

static void answer(ra_fake_t *f, ra_t *ra, const ra_request_t *rq) {
    const char *q = rq->post;
    if (!q) q = strchr(rq->url, '?') ? strchr(rq->url, '?') + 1 : "";
    snprintf(f->last, sizeof f->last, "%s", q);
    f->answered++;
    if (f->offline) {
        ra_reply(ra, rq->id, RA_NO_REPLY, NULL, 0);
        return;
    }
    char r[32], u[64], p[64], t[64], m[40], a[16];
    param(q, "r", r, sizeof r);
    param(q, "u", u, sizeof u);
    param(q, "p", p, sizeof p);
    param(q, "t", t, sizeof t);
    param(q, "m", m, sizeof m);
    param(q, "a", a, sizeof a);
    if (!strcmp(r, "login2")) {
        if (!strcmp(t, "EXPIRED")) say(ra, rq->id, 403, LOGIN_EXPIRED);
        else if (!strcmp(u, "Tester") && (!strcmp(p, "secret") || !strcmp(t, "TOKEN"))) say(ra, rq->id, 200, LOGIN_OK);
        else say(ra, rq->id, 401, LOGIN_BAD);
    } else if (!strcmp(r, "achievementsets")) {
        if (!f->hash || strcmp(m, f->hash)) {
            say(ra, rq->id, 404, NOT_FOUND);
            return;
        }
        const char *list = f->achievements ? f->achievements : "[]";
        const char *boards = f->leaderboards ? f->leaderboards : "[]";
        size_t cap = strlen(list) + strlen(boards) + 1024;
        char *body = malloc(cap);
        if (!body) {
            ra_reply(ra, rq->id, RA_NO_REPLY, NULL, 0);
            return;
        }
        snprintf(body, cap,
                 "{\"Success\":true,\"GameId\":1234,\"Title\":\"Test Game\",\"ConsoleId\":%d,"
                 "\"ImageIconUrl\":\"https://media.retroachievements.org/Images/000001.png\","
                 "\"RichPresenceGameId\":1234,\"RichPresencePatch\":\"\",\"Sets\":[{\"AchievementSetId\":1111,"
                 "\"GameId\":1234,\"Title\":null,\"Type\":\"core\","
                 "\"ImageIconUrl\":\"https://media.retroachievements.org/Images/000001.png\","
                 "\"Achievements\":%s,\"Leaderboards\":%s}]}",
                 f->console ? f->console : 5, list, boards);
        say(ra, rq->id, 200, body);
        free(body);
    } else if (!strcmp(r, "startsession")) {
        f->awarded = 0;
        say(ra, rq->id, 200, SESSION);
    } else if (!strcmp(r, "awardachievement")) {
        unsigned id = (unsigned)strtoul(a, NULL, 10);
        if (f->refuse && id == f->refuse) {
            say(ra, rq->id, 400, REFUSED);
            return;
        }
        int total = 0; /* the set's size: its "ID": keys */
        for (const char *s = f->achievements; s && (s = strstr(s, "\"ID\":")); s++) total++;
        int left = total - ++f->awarded;
        char body[160];
        snprintf(body, sizeof body,
                 "{\"Success\":true,\"Score\":105,\"SoftcoreScore\":5,\"AchievementID\":%u,\"AchievementsRemaining\":%d}", id,
                 left > 0 ? left : 0);
        say(ra, rq->id, 200, body);
    } else if (!strcmp(r, "lbinfo")) {
        lbinfo(f, ra, rq->id, q);
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
    } else {
        say(ra, rq->id, 200, FINE); /* ping and anything else */
    }
}

int ra_fake_serve(ra_fake_t *f, ra_t *ra) {
    int n = 0;
    ra_request_t rq;
    while (n < 1000 && ra_take_request(ra, 0, &rq)) {
        answer(f, ra, &rq);
        ra_request_free(&rq);
        n++;
    }
    return n;
}
