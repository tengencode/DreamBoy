#ifndef GBAEMU_RA_FAKE_H
#define GBAEMU_RA_FAKE_H

#include <stdbool.h>
#include <stdint.h>

#include "ra.h"

/* A pretend RetroAchievements server for the PC tests and gbabench (milestone 14). It knows one user: "Tester" with
 * the password "secret" and the login token "TOKEN" (the token "EXPIRED" has expired); and one game: the hash `hash`
 * with the achievements `achievements` (a JSON array, rcheevos' format). Unlocks succeed, except for achievement
 * `refuse` (0 = none); each answer says how many achievements are left, as the real server does (rcheevos shows
 * "mastered" only when that reaches 0). `offline` answers everything with RA_NO_REPLY. Scores (milestone 15): a
 * submitted score is ranked 3rd of 31, unless its board is `refuse_board`; a score list (r=lbinfo) of board 501 has
 * 30 players "P01"… with Tester at `user_rank`, each scoring 100 + 10 × rank frames. */
typedef struct {
    const char *hash;
    const char *achievements;
    const char *leaderboards; /* the game's boards (a JSON array, rcheevos' format); NULL = none */
    uint32_t refuse_board;    /* a board whose scores are refused (0 = none) */
    uint32_t user_rank;       /* Tester's rank on board 501, which has 30 players (0 = not on it); every other board is empty */
    uint32_t refuse;
    bool offline;
    int console;      /* the game's RetroAchievements console number; 0 = 5, Game Boy Advance (4 Game Boy, 6 Color) */
    int answered;     /* requests answered so far */
    int awarded;      /* unlocks accepted since the last session start */
    char last[1024];  /* the last request's form data (r=…&u=…), for checks */
} ra_fake_t;

/* Answers every waiting request (and the ones rcheevos makes while being answered); returns how many. */
int ra_fake_serve(ra_fake_t *f, ra_t *ra);

#endif
