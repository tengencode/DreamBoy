# Milestone 14 — RetroAchievements Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task, **inline in this session** (CLAUDE.md: never subagent-driven). Steps use checkbox (`- [ ]`) syntax for tracking. Go through all tasks one after another without waiting for the user; stop only at Task 13 (the user's try) or when blocked.

**Goal:** The user signs in with their own retroachievements.org account and earns achievements while playing: unlock pop-ups, progress, challenge badges, "mastered", a per-game list in the pause menu, and a Hardcore switch (on by default) that blocks state loading, rewind and cheats in games that have a set.

**Architecture:** rcheevos v12.5.0 (`third_party/rcheevos/`, MIT) runs inside a small C glue (`platform/ra/ra.c`) that owns one `rc_client`, keeps a queue of web requests (a Kotlin thread does the internet and answers them), keeps a queue of events (the Kotlin game screen shows them) and reads the GBA's IWRAM / EWRAM / save RAM — only inside `ra_do_frame` / `ra_idle`, which the emulation thread calls every frame (and while paused), and which the app calls while it waits for a game to be identified before it starts. The glue builds on the PC too and is tested there with a pretend server (`platform/ra/ra_fake.c`); the app has its own pretend server (`FakeRaServer.kt`) for on-device tests and Claude's emulator check.

**Tech Stack:** C11 (core, glue, JNI), rcheevos 12.5.0, CMake (PC: llvm-mingw clang; Android: NDK), Kotlin + Jetpack Compose (Material 3), JUnit 4 (JVM and on-device).

**Spec:** `docs/superpowers/specs/2026-10-06-milestone-14-retroachievements-design.md`. **Design system:** `DESIGN.md`, `PRODUCT.md` — Tasks 9 and 11 go through the impeccable skill.

## Conventions for every task

- Build and test only through `bash tools/dev.sh …` (Git Bash). `unit [filter]` = PC unit tests (debug + sanitizers), `test` = all CTest, `jvmtest` = Kotlin tests on the PC, `apk`, `install`, `androidtest` = the Pixel 9 phone emulator. Never a USB phone.
- Test first: write the test, see it fail, write the code, see it pass.
- **One commit per task**, message ending with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- **Every task's last step updates the documents** (CLAUDE.md rule): `docs/PROGRESS.md` (the task's **Completed** entry with the commit hash and any surprise; the M14 task table; **Project status**; **Next** = the next task, its first step and first command; "Last updated"), tick this plan's boxes, and re-read PROGRESS.md's Goal / status / Next / Notes, this plan's task list, the spec's status line and CLAUDE.md's "Key files" for anything stale. The doc changes go in the task's own commit.
- The emulator (Pixel 9 AVD) is restarted only with the user's OK (memory rule). Known flakes on a busy emulator: `SessionTest.runsAtGbaSpeed`, `AudioTest.aStuckStreamIsReopenedByTheWatchdog` — rerun once.
- In Git Bash, `adb shell` paths need `MSYS_NO_PATHCONV=1`; `adb` is `"$LOCALAPPDATA/Android/Sdk/platform-tools/adb.exe"`.

## File map

| File | New / changed | Responsibility |
|---|---|---|
| `third_party/rcheevos/` (LICENSE, include/, src/) | new | rcheevos 12.5.0 as released (committed, never edited) |
| `third_party/rcheevos.cmake` | new | the `rcheevos` static library: only what `rc_client` needs |
| `third_party/README.md` | new | what is in `third_party/`, where it came from, its licence |
| `tools/fetch_rcheevos.py` | new | fetches the release again, checks the pinned SHA-256 (`--check`: compare only) |
| `core/include/gba.h`, `core/src/gba.c` | changed | `gba_iwram`, `gba_ewram` (read-only RAM views) |
| `tests/unit/test_ram_view.c` | new | tests of the RAM views |
| `platform/ra/ra.h`, `platform/ra/ra.c` | new | the glue (requests, sign-in, game, memory, events, hardcore, list) |
| `platform/ra/ra_fake.h`, `platform/ra/ra_fake.c` | new | a pretend RetroAchievements server for PC tests and the speed check |
| `tests/unit/test_ra.c` | new | the glue's tests |
| `tests/unit/CMakeLists.txt`, `tools/bench/CMakeLists.txt`, `platform/android/CMakeLists.txt` | changed | build the glue and rcheevos |
| `tools/bench/main.c` | changed | `--ra <n>`: n made-up achievements checked every frame |
| `platform/android/emu_thread.h/.c` | changed | `ra_do_frame` each frame, `ra_idle` while paused, the core lock, the hardcore limits, the progress reset |
| `platform/android/jni_shared.h` | new | what the two JNI files share |
| `platform/android/jni_ra.c` | new | `NativeBridge.ra*` |
| `platform/android/jni_bridge.c` | changed | gives the session the `ra_t`, unloads the game on stop |
| `app/.../Net.kt` | changed | `Net.send` (GET or POST, status + body) |
| `app/.../RaNet.kt` | new | `RaRequest`, the User-Agent, `RaWorker` + `RaService` (the request thread) |
| `app/.../RaAccount.kt` | new | `files/ra.json`: user name + token |
| `app/.../Achievements.kt` | new | `RaNative` (+ `NativeRa`), the sign-in / game-load controller |
| `app/.../RaPopups.kt` | new | `RaEvent`, `RaAchievement`, what the game screen shows and for how long |
| `app/.../BadgeCache.kt` | new | badge pictures kept in `files/ra/badges/` |
| `app/.../FakeRaServer.kt`, `app/.../FakeBadges.kt` | new | the app's pretend server and badge pictures |
| `app/.../AchievementScreens.kt` | new | the pop-up layer, the badge picture, the Achievements panel |
| `app/.../NativeBridge.kt`, `Settings.kt`, `SettingsScreens.kt`, `Screens.kt`, `AppTheme.kt`, `MainActivity.kt` | changed | wiring and screens |
| `app/src/test/.../` `NetTest`, `RaNetTest`, `RaAccountTest`, `AchievementsTest`, `RaPopupsTest`, `BadgeCacheTest`, `FakeRaServerTest`, `SettingsTest`, `BackupTest`, `RestoreTest` | new / changed | Kotlin tests |
| `app/src/androidTest/.../AchievementsDeviceTest.kt` | new | on-device tests with the pretend server |

---

## Task 1: rcheevos into third_party and both builds

**Files:**
- Create: `tools/fetch_rcheevos.py`, `third_party/rcheevos/**` (by the script), `third_party/rcheevos.cmake`, `third_party/README.md`, `tests/unit/test_rcheevos_build.c`
- Modify: `tests/unit/CMakeLists.txt`, `platform/android/CMakeLists.txt`, `.gitattributes`

The user allowed the download on 2026-10-06 (`rcheevos-12.5.0.tar.gz`, 449,321 bytes, SHA-256 `e6df83de4e18f0a19206e711e1e589624dcfcf6bf529c14f6dd2bb2d1ced983f`).

- [x] **Step 1: Write the fetch script**

`tools/fetch_rcheevos.py`:

```python
"""Fetches rcheevos (RetroAchievements' client library, MIT) into third_party/rcheevos: LICENSE, include/ and src/.

The files are committed (milestone 14); run this to fetch them again or, with --check, to compare the committed files
with the release. The download is checked against a pinned SHA-256.
Usage: python tools/fetch_rcheevos.py [--check]
"""
import argparse
import hashlib
import io
import os
import shutil
import sys
import tarfile
import urllib.request

VERSION = "12.5.0"
URL = f"https://github.com/RetroAchievements/rcheevos/archive/refs/tags/v{VERSION}.tar.gz"
SHA256 = "e6df83de4e18f0a19206e711e1e589624dcfcf6bf529c14f6dd2bb2d1ced983f"
ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
DEST = os.path.join(ROOT, "third_party", "rcheevos")


def download():
    req = urllib.request.Request(URL, headers={"User-Agent": "gba-emulator-fetch"})
    with urllib.request.urlopen(req, timeout=60) as r:
        data = r.read()
    got = hashlib.sha256(data).hexdigest()
    if got != SHA256:
        sys.exit(f"rcheevos {VERSION}: SHA-256 {got} is not the pinned {SHA256}")
    return data


def kept_files(data):
    """{path inside rcheevos/: bytes} for LICENSE, include/ and src/."""
    out = {}
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as tar:
        for m in tar.getmembers():
            if not m.isfile() or "/" not in m.name:
                continue
            rel = m.name.split("/", 1)[1]
            if rel == "LICENSE" or rel.startswith(("include/", "src/")):
                out[rel] = tar.extractfile(m).read()
    return out


def lf(b):
    return b.replace(b"\r\n", b"\n")


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--check", action="store_true", help="compare with the committed files; change nothing")
    args = p.parse_args()
    files = kept_files(download())
    if args.check:
        bad = []
        for rel, b in sorted(files.items()):
            path = os.path.join(DEST, rel)
            if not os.path.isfile(path) or lf(open(path, "rb").read()) != lf(b):
                bad.append(rel)
        for rel in bad:
            print(f"differs: {rel}")
        print(f"rcheevos {VERSION}: {len(files) - len(bad)} of {len(files)} files match")
        sys.exit(1 if bad else 0)
    shutil.rmtree(DEST, ignore_errors=True)
    for rel, b in files.items():
        path = os.path.join(DEST, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(b)
    print(f"rcheevos {VERSION}: {len(files)} files in third_party/rcheevos")


if __name__ == "__main__":
    main()
```

- [x] **Step 2: Run it**

Run: `python tools/fetch_rcheevos.py`
Expected: `rcheevos 12.5.0: <n> files in third_party/rcheevos` (n ≈ 75; it was 66). Then `python tools/fetch_rcheevos.py --check` → `… <n> of <n> files match`, exit 0.

- [x] **Step 3: Keep its bytes as released**

Append to `.gitattributes`:

```
# rcheevos as released (milestone 14): committed byte for byte
third_party/rcheevos/** -text
```

- [x] **Step 4: The library's CMake file**

`third_party/rcheevos.cmake`:

```cmake
# rcheevos (RetroAchievements' client library, MIT: third_party/rcheevos/LICENSE) as a static library, with only what
# rc_client needs: the client, the web API, the runtime and md5.c. Not its file hashing (the glue hashes the ROM itself),
# RAIntegration or external clients. Included by the unit tests, gbabench and the Android library.
if(NOT TARGET rcheevos)
    set(RC_DIR ${CMAKE_CURRENT_LIST_DIR}/rcheevos)
    file(GLOB RC_RUNTIME_SOURCES ${RC_DIR}/src/rcheevos/*.c)
    add_library(rcheevos STATIC
        ${RC_DIR}/src/rc_client.c ${RC_DIR}/src/rc_compat.c ${RC_DIR}/src/rc_util.c ${RC_DIR}/src/rc_version.c
        ${RC_DIR}/src/rapi/rc_api_common.c ${RC_DIR}/src/rapi/rc_api_info.c ${RC_DIR}/src/rapi/rc_api_runtime.c
        ${RC_DIR}/src/rapi/rc_api_user.c ${RC_RUNTIME_SOURCES} ${RC_DIR}/src/rhash/md5.c)
    target_include_directories(rcheevos PUBLIC ${RC_DIR}/include ${RC_DIR}/src/rhash PRIVATE ${RC_DIR}/src)
    find_package(Threads REQUIRED)
    target_link_libraries(rcheevos PUBLIC Threads::Threads)
    target_compile_options(rcheevos PRIVATE -w) # their code: their warnings are not ours to fix
endif()
```

`third_party/README.md`:

```markdown
# Third-party code

| Folder | What | Version | Licence | From |
|---|---|---|---|---|
| `rcheevos/` | RetroAchievements' client library (`rc_client`), used by the RetroAchievements glue `platform/ra/` (milestone 14) | 12.5.0 (2026-09-14) | MIT (`rcheevos/LICENSE`) | https://github.com/RetroAchievements/rcheevos, tag `v12.5.0`; `python tools/fetch_rcheevos.py --check` compares the committed files with the release (SHA-256 pinned in the script) |

Only `LICENSE`, `include/` and `src/` are kept; `third_party/rcheevos.cmake` compiles the parts the app uses. The files are never edited here.
```

- [x] **Step 5: Write a failing build test**

`tests/unit/test_rcheevos_build.c`:

```c
/* rcheevos (third_party/rcheevos) builds and links into the PC tests (milestone 14). */
#include <string.h>

#include "rc_client.h"
#include "test.h"

static uint32_t no_memory(uint32_t address, uint8_t *buffer, uint32_t num_bytes, rc_client_t *client) {
    (void)address;
    (void)buffer;
    (void)num_bytes;
    (void)client;
    return 0;
}

static void no_server(const rc_api_request_t *request, rc_client_server_callback_t callback, void *callback_data,
                      rc_client_t *client) {
    (void)request;
    (void)callback;
    (void)callback_data;
    (void)client;
}

TEST(rcheevos_client_is_created_with_hardcore_on_and_says_its_version) {
    rc_client_t *c = rc_client_create(no_memory, no_server);
    CHECK(c != NULL);
    CHECK_EQ(rc_client_get_hardcore_enabled(c), 1);
    char clause[64];
    rc_client_get_user_agent_clause(c, clause, sizeof clause);
    CHECK(strncmp(clause, "rcheevos/12.5", 13) == 0);
    rc_client_destroy(c);
}
```

- [x] **Step 6: Run it to see it fail**

Run: `bash tools/dev.sh unit rcheevos`
Expected: build error — `rc_client.h` not found.

- [x] **Step 7: Link rcheevos into the unit tests and the Android library**

Append to `tests/unit/CMakeLists.txt`:

```cmake
# RetroAchievements (milestone 14): rcheevos, the glue and its pretend server.
include(${PROJECT_SOURCE_DIR}/third_party/rcheevos.cmake)
target_link_libraries(unit_tests PRIVATE rcheevos)
```

Append to `platform/android/CMakeLists.txt`:

```cmake
# RetroAchievements (milestone 14): rcheevos and the glue (platform/ra).
include(${REPO_ROOT}/third_party/rcheevos.cmake)
target_link_libraries(gbaemu PRIVATE rcheevos)
```

- [x] **Step 8: Run the tests and the app build**

Run: `bash tools/dev.sh unit rcheevos` → the new test passes. `bash tools/dev.sh unit` → all pass. `bash tools/dev.sh apk` → BUILD SUCCESSFUL (rcheevos compiles for arm64 and x86_64).

- [x] **Step 9: Docs and commit**

PROGRESS.md: add a "Milestone 14 tasks" table (plan: this file; rows 1–13 from this plan, Task 1 ✅), mark 14 "🔄 In progress — Task 1 done" in Project status, Completed entry, Next = Task 2. CLAUDE.md "Current plan" → this plan. Then:

```bash
git add tools/fetch_rcheevos.py third_party .gitattributes tests/unit/test_rcheevos_build.c tests/unit/CMakeLists.txt platform/android/CMakeLists.txt docs CLAUDE.md
git commit -m "build: rcheevos 12.5.0 in third_party, linked into the unit tests and the app

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

## Task 2: The core's RAM views

**Files:**
- Modify: `core/include/gba.h`, `core/src/gba.c`
- Test: `tests/unit/test_ram_view.c`

- [x] **Step 1: Write the failing test**

`tests/unit/test_ram_view.c`:

```c
/* The work-RAM views (milestone 14, for RetroAchievements): what the game wrote, read straight. */
#include <stdlib.h>

#include "gba.h"
#include "mem/bus.h"
#include "test.h"
#include "testutil.h"

static gba_t *machine(void) {
    static const uint32_t spin[] = {0xEAFFFFFE}; /* b . : the program never touches memory */
    return tu_arm(spin, TU_LEN(spin));
}

TEST(ram_views_show_what_the_game_wrote_at_both_ends) {
    gba_t *g = machine();
    bus_write8(g, 0x03000005, 0x77, false);
    bus_write8(g, 0x03007FFF, 0x12, false);
    bus_write8(g, 0x02000000, 0x34, false);
    bus_write8(g, 0x0203FFFF, 0x56, false);
    CHECK_EQ(gba_iwram(g)[5], 0x77);
    CHECK_EQ(gba_iwram(g)[GBA_IWRAM_SIZE - 1], 0x12);
    CHECK_EQ(gba_ewram(g)[0], 0x34);
    CHECK_EQ(gba_ewram(g)[GBA_EWRAM_SIZE - 1], 0x56);
    gba_destroy(g);
}

TEST(ram_views_follow_mirrors_and_state_loads) {
    gba_t *g = machine();
    bus_write8(g, 0x03008005, 0x99, false); /* IWRAM repeats every 32 KiB */
    CHECK_EQ(gba_iwram(g)[5], 0x99);
    size_t n = gba_state_size(g);
    uint8_t *state = malloc(n);
    CHECK_EQ(gba_state_save(g, state, n), n);
    bus_write8(g, 0x03000005, 0x01, false);
    CHECK_EQ(gba_iwram(g)[5], 0x01);
    CHECK_EQ(gba_state_load(g, state, n), GBA_OK);
    CHECK_EQ(gba_iwram(g)[5], 0x99);
    free(state);
    gba_destroy(g);
}
```

- [x] **Step 2: Run it to see it fail**

Run: `bash tools/dev.sh unit ram_views`
Expected: build error — `gba_iwram` / `GBA_IWRAM_SIZE` undeclared.

- [x] **Step 3: Add the views**

In `core/include/gba.h`, after `uint32_t gba_rom_crc32(const gba_t *g);`:

```c
/* The game's work RAM, read straight (no cycles, no side effects), for tools that watch it (RetroAchievements,
 * milestone 14). Valid while the machine lives; it changes as the game runs, so read it between gba_run_frame calls. */
#define GBA_IWRAM_SIZE 0x8000u  /* internal work RAM, at 0x03000000 */
#define GBA_EWRAM_SIZE 0x40000u /* external work RAM, at 0x02000000 */
const uint8_t *gba_iwram(const gba_t *g);
const uint8_t *gba_ewram(const gba_t *g);
```

In `core/src/gba.c` (with the other small getters such as `gba_rom_crc32`):

```c
const uint8_t *gba_iwram(const gba_t *g) {
    return g->bus.iwram;
}

const uint8_t *gba_ewram(const gba_t *g) {
    return g->bus.ewram;
}
```

and at the top of `core/src/gba.c`, next to the other includes:

```c
_Static_assert(GBA_IWRAM_SIZE == IWRAM_SIZE && GBA_EWRAM_SIZE == EWRAM_SIZE, "gba.h's RAM sizes match the bus");
```

(`IWRAM_SIZE` / `EWRAM_SIZE` come from `mem/bus.h`, which `gba_internal.h` includes; add `#include "mem/bus.h"` if the build says they are unknown.)

- [x] **Step 4: Run the tests**

Run: `bash tools/dev.sh unit ram_views` → 2 pass. `bash tools/dev.sh test` → all CTest pass (nothing else changed: fingerprints, states, suite).

- [x] **Step 5: Docs and commit**

```bash
git add core/include/gba.h core/src/gba.c tests/unit/test_ram_view.c docs
git commit -m "feat(core): read-only views of IWRAM and EWRAM for RetroAchievements

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

## Task 3: The glue — requests, sign-in, the pretend server

**Files:**
- Create: `platform/ra/ra.h`, `platform/ra/ra.c`, `platform/ra/ra_fake.h`, `platform/ra/ra_fake.c`
- Test: `tests/unit/test_ra.c`
- Modify: `tests/unit/CMakeLists.txt`

- [x] **Step 1: The header (all of the glue's API; Task 4 implements the game half)**

`platform/ra/ra.h`:

```c
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

/* The game's memory in RetroAchievements' GBA map: 0x00000-0x07FFF IWRAM, 0x08000-0x47FFF EWRAM,
 * 0x48000-0x57FFF save RAM (its first 64 KiB). NULL / 0 for what is not there. */
#define RA_EWRAM_AT 0x8000u
#define RA_SRAM_AT 0x48000u
#define RA_SRAM_END 0x58000u
typedef struct {
    const uint8_t *iwram; /* GBA_IWRAM_SIZE bytes */
    const uint8_t *ewram; /* GBA_EWRAM_SIZE bytes */
    const uint8_t *sram;  /* the save chip's bytes */
    size_t sram_size;
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
} ra_event_kind_t;
typedef struct {
    int kind;
    uint32_t id, points;
    char title[128];
    char description[256];
    char progress[24];
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

#endif
```

- [x] **Step 2: The pretend server's header**

`platform/ra/ra_fake.h`:

```c
#ifndef GBAEMU_RA_FAKE_H
#define GBAEMU_RA_FAKE_H

#include <stdbool.h>
#include <stdint.h>

#include "ra.h"

/* A pretend RetroAchievements server for the PC tests and gbabench (milestone 14). It knows one user: "Tester" with
 * the password "secret" and the login token "TOKEN" (the token "EXPIRED" has expired); and one game: the hash `hash`
 * with the achievements `achievements` (a JSON array, rcheevos' format). Unlocks succeed, except for achievement
 * `refuse` (0 = none); each answer says how many achievements are left, as the real server does (rcheevos shows
 * "mastered" only when that reaches 0). `offline` answers everything with RA_NO_REPLY. */
typedef struct {
    const char *hash;
    const char *achievements;
    uint32_t refuse;
    bool offline;
    int answered;     /* requests answered so far */
    int awarded;      /* unlocks accepted since the last session start */
    char last[1024];  /* the last request's form data (r=…&u=…), for checks */
} ra_fake_t;

/* Answers every waiting request (and the ones rcheevos makes while being answered); returns how many. */
int ra_fake_serve(ra_fake_t *f, ra_t *ra);

#endif
```

- [x] **Step 3: Write the failing tests (requests and sign-in)**

`tests/unit/test_ra.c`:

```c
/* The RetroAchievements glue (platform/ra, milestone 14) against the pretend server (ra_fake.c). */
#include <stdio.h>
#include <string.h>

#include "ra.h"
#include "ra_fake.h"
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
```

Add the glue to `tests/unit/CMakeLists.txt` (after the rcheevos lines from Task 1):

```cmake
target_sources(unit_tests PRIVATE ${PROJECT_SOURCE_DIR}/platform/ra/ra.c ${PROJECT_SOURCE_DIR}/platform/ra/ra_fake.c)
target_include_directories(unit_tests PRIVATE ${PROJECT_SOURCE_DIR}/platform/ra)
```

- [x] **Step 4: Run them to see them fail**

Run: `bash tools/dev.sh unit ra_`
Expected: build errors — `ra.c` / `ra_fake.c` do not exist.

- [x] **Step 5: Write the glue's first half**

`platform/ra/ra.c`:

```c
/* RetroAchievements glue (milestone 14): rcheevos' rc_client with a request queue, an event queue and the GBA's
 * memory. See ra.h. */
#define _POSIX_C_SOURCE 200809L
#include "ra.h"

#include <errno.h>
#include <pthread.h>
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
    const ra_memory_t *mem;   /* set only inside ra_do_frame / ra_idle / ra_reset_progress */
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

/* ---- the game (Task 4) ---- */

void ra_md5_hex(const uint8_t *data, size_t size, char out[33]) {
    md5_state_t st;
    md5_byte_t digest[16];
    md5_init(&st);
    if (size > RA_HASH_MAX) size = RA_HASH_MAX;
    md5_append(&st, data, (int)size);
    md5_finish(&st, digest);
    for (int i = 0; i < 16; i++) snprintf(out + 2 * i, 3, "%02x", digest[i]);
}

static uint32_t read_memory(uint32_t address, uint8_t *buffer, uint32_t num_bytes, rc_client_t *client) {
    (void)address;
    (void)buffer;
    (void)num_bytes;
    (void)client;
    return 0; /* Task 4 */
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
    return ra;
}

void ra_destroy(ra_t *ra) {
    if (!ra) return;
    rc_client_destroy(ra->client);
    for (pending_t *p = ra->pending, *next; p; p = next) {
        next = p->next;
        free_pending(p);
    }
    pthread_cond_destroy(&ra->queued);
    pthread_mutex_destroy(&ra->lock);
    free(ra);
}
```

`platform/ra/ra_fake.c`:

```c
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
        size_t cap = strlen(list) + 1024;
        char *body = malloc(cap);
        if (!body) {
            ra_reply(ra, rq->id, RA_NO_REPLY, NULL, 0);
            return;
        }
        snprintf(body, cap,
                 "{\"Success\":true,\"GameId\":1234,\"Title\":\"Test Game\",\"ConsoleId\":5,"
                 "\"ImageIconUrl\":\"https://media.retroachievements.org/Images/000001.png\","
                 "\"RichPresenceGameId\":1234,\"RichPresencePatch\":\"\",\"Sets\":[{\"AchievementSetId\":1111,"
                 "\"GameId\":1234,\"Title\":null,\"Type\":\"core\","
                 "\"ImageIconUrl\":\"https://media.retroachievements.org/Images/000001.png\","
                 "\"Achievements\":%s,\"Leaderboards\":[]}]}",
                 list);
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
```

- [x] **Step 6: Run the tests**

Run: `bash tools/dev.sh unit ra_`
Expected: the 7 `ra_` tests pass. Then `bash tools/dev.sh unit` → all pass, no warnings from `ra.c` / `ra_fake.c`.

If `ra_sign_in_without_internet…` gives a code you did not expect, print it (`fprintf(stderr, "%d\n", code)`) and record which `RC_*` it is in PROGRESS.md's Completed note (Task 8's Kotlin code only needs "not a credential error").

- [x] **Step 7: Docs and commit**

```bash
git add platform/ra tests/unit/test_ra.c tests/unit/CMakeLists.txt docs
git commit -m "feat(ra): RetroAchievements glue - request queue, sign-in, pretend server

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

## Task 4: The glue — games, memory, events, hardcore, the list

**Files:**
- Modify: `platform/ra/ra.c`
- Test: `tests/unit/test_ra.c` (append)

- [x] **Step 1: Write the failing tests**

Append to `tests/unit/test_ra.c`:

```c
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

static uint8_t iw[0x8000], ew[0x40000], sr[0x8000];
static ra_memory_t mem(void) {
    ra_memory_t m = {iw, ew, sr, sizeof sr};
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

/* Signed in, the game loaded (one ra_idle activates it), two quiet frames so every achievement is armed. */
static ra_t *with_game(ra_fake_t *f) {
    memset(iw, 0, sizeof iw);
    memset(ew, 0, sizeof ew);
    memset(sr, 0, sizeof sr);
    f->hash = HASH;
    f->achievements = SET;
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

TEST(ra_peek_maps_iwram_ewram_and_save_ram) {
    memset(iw, 0, sizeof iw);
    memset(ew, 0, sizeof ew);
    iw[0] = 1;
    iw[0x7FFF] = 2;
    ew[0] = 3;
    ew[0x3FFFF] = 4;
    sr[0] = 5;
    ra_memory_t m = {iw, ew, sr, 16};
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
    ra_memory_t none = {NULL, NULL, NULL, 0};
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
```

- [x] **Step 2: Run them to see them fail**

Run: `bash tools/dev.sh unit ra_`
Expected: link errors (`ra_load_game`, `ra_peek`, `ra_do_frame`, … undefined).

- [x] **Step 3: Write the game half of the glue**

In `platform/ra/ra.c`, replace the stub `read_memory` (the `/* Task 4 */` one) with:

```c
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

static void push_event(ra_t *ra, const ra_event_t *e) {
    pthread_mutex_lock(&ra->lock);
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
        }
        a = NULL;
        break;
    case RC_CLIENT_EVENT_DISCONNECTED: e.kind = RA_EVENT_OFFLINE; break;
    case RC_CLIENT_EVENT_RECONNECTED: e.kind = RA_EVENT_ONLINE; break;
    case RC_CLIENT_EVENT_RESET: e.kind = RA_EVENT_RESET; break;
    default: return; /* leaderboards and subsets: not in milestone 14 */
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
```

Also: move `ra_peek`'s and `read_memory`'s place above `ra_create` if the compiler needs it (it does: `ra_create` uses `read_memory`); add `#include <stdint.h>` (for `uintptr_t`) to the includes; and in `ra_create`, after `rc_client_set_allow_background_memory_reads(...)`, add:

```c
    rc_client_set_event_handler(ra->client, on_event);
```

- [x] **Step 4: Run the tests**

Run: `bash tools/dev.sh unit ra_`
Expected: all `ra_` tests pass. If one fails, read rcheevos' code for that path before changing the test (the tests encode behaviour read in rcheevos 12.5.0 on 2026-10-06: activation in `rc_client_idle` when background reads are off; the first retry of an unlock at once, then after 1 s; DISCONNECTED/RECONNECTED raised in `rc_client_idle`; a WAITING achievement with hits arms itself; "mastered" only after an award answer says 0 left, raised on the next frame). Record any surprise in PROGRESS.md. Then `bash tools/dev.sh unit` → all pass.

- [x] **Step 5: Docs and commit**

```bash
git add platform/ra/ra.c tests/unit/test_ra.c docs
git commit -m "feat(ra): games, memory map, events, hardcore limits and the achievement list

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

## Task 5: The speed check — `gbabench --ra <n>`

**Files:**
- Modify: `tools/bench/main.c`, `tools/bench/CMakeLists.txt`, `tools/dev.sh` (comment only)

- [x] **Step 1: Build the glue into gbabench**

Append to `tools/bench/CMakeLists.txt`:

```cmake
# --ra <n> (milestone 14): made-up RetroAchievements checked every frame, through the glue and its pretend server.
include(${PROJECT_SOURCE_DIR}/third_party/rcheevos.cmake)
target_sources(gbabench PRIVATE ${PROJECT_SOURCE_DIR}/platform/ra/ra.c ${PROJECT_SOURCE_DIR}/platform/ra/ra_fake.c)
target_include_directories(gbabench PRIVATE ${PROJECT_SOURCE_DIR}/platform/ra)
target_link_libraries(gbabench PRIVATE rcheevos)
```

- [x] **Step 2: The option**

In `tools/bench/main.c`:

1. Header comment: add a line ` *            [--ra <n>]` to the usage and a sentence: ` * --ra n (milestone 14): n made-up RetroAchievements (2 conditions each, never met) checked after every frame, as the app does.`
2. Includes: `#include "ra.h"` and `#include "ra_fake.h"`.
3. Globals and helpers (above `play`):

```c
/* --ra n: one RetroAchievements client with n made-up achievements, signed in to the pretend server. */
static ra_t *bench_ra;

static char *made_up_set(int n) {
    char *json = malloc((size_t)n * 200 + 3);
    if (!json) return NULL;
    char *p = json;
    *p++ = '[';
    uint32_t x = 12345;
    for (int i = 0; i < n; i++) {
        x = x * 1103515245u + 12345u;
        uint32_t a = (x >> 8) % 0x47FFCu, b = (x >> 4) % 0x47FFCu;
        p += sprintf(p, "%s{\"ID\":%d,\"Title\":\"A%d\",\"Description\":\"-\",\"Flags\":3,\"Points\":1,"
                        "\"MemAddr\":\"0xX%06x=%u_0xX%06x=%u\",\"Author\":\"-\",\"BadgeName\":\"00001\","
                        "\"Created\":1,\"Modified\":1}",
                     i ? "," : "", 100000 + i, i, (unsigned)a, (unsigned)(x | 0x80000000u), (unsigned)b,
                     (unsigned)((x * 7u) | 0x80000000u));
    }
    *p++ = ']';
    *p = '\0';
    return json;
}

static bool bench_ra_start(int n) {
    char *set = made_up_set(n);
    ra_fake_t f = {0};
    f.hash = "00000000000000000000000000000000";
    f.achievements = set;
    bench_ra = ra_create();
    if (!set || !bench_ra) return false;
    ra_login_password(bench_ra, "Tester", "secret");
    ra_fake_serve(&f, bench_ra);
    ra_load_game(bench_ra, f.hash);
    ra_fake_serve(&f, bench_ra);
    static uint8_t zeros[0x40000];
    ra_memory_t m = {zeros, zeros, NULL, 0};
    ra_idle(bench_ra, &m);
    free(set);
    ra_summary_t s;
    if (!ra_summary(bench_ra, &s) || s.total != (uint32_t)n) {
        fprintf(stderr, "--ra: the made-up set did not load (%u of %d)\n", s.total, n);
        return false;
    }
    return true;
}
```

(Two 32-bit compares with random values whose top bit is set: practically never true, and valid for rcheevos — a comparison it can tell is impossible, like a byte = 300, might be rejected and then never checked, which would measure nothing.)

4. In `play`, after `gba_run_frame(g);`:

```c
        if (bench_ra) {
            ra_memory_t m = {gba_iwram(g), gba_ewram(g), gba_saveram(g), gba_saveram_size(g)};
            ra_do_frame(bench_ra, &m);
        }
```

5. In `main`'s argument loop: `else if (!strcmp(argv[i], "--ra") && i + 1 < argc) ra_count = atoi(argv[++i]);` with `int ra_count = 0;` declared beside the other options; after the loop: `if (ra_count > 0 && !bench_ra_start(ra_count)) return 3;`; add `[--ra <n>]` to `usage()`'s text. At the end of `main`: `ra_destroy(bench_ra);`.

6. `tools/dev.sh`'s header comment for `bench`: add `bench --ra 300` (300 made-up achievements, milestone 14).

- [x] **Step 3: Measure**

Run: `bash tools/dev.sh bench --runs 3` and then `bash tools/dev.sh bench --runs 3 --ra 300` (close the phone emulator first, or the PC numbers drop ~40 %).
Expected: every scene's picture and sound hash `ok` in both runs (the set never changes the game); with `--ra 300` every scene ≥ its 97 % floor (PROGRESS.md: game_a 1161, game_b 921, game_c 1733, game_d 935, m7_demo 2202, snd1_demo 5836). A scene under its floor is measured again (PC noise); if it stays under, profile before going on (rcheevos' cost per frame is what is measured; 300 achievements is a large set).

- [x] **Step 4: Docs and commit**

Record both runs' numbers in PROGRESS.md (Completed + the Notes speed line).

```bash
git add tools/bench tools/dev.sh docs
git commit -m "tools(bench): --ra n checks n made-up achievements every frame; speed with 300 recorded

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

## Task 6: The game loop and the JNI bridge

**Files:**
- Modify: `platform/android/emu_thread.h`, `platform/android/emu_thread.c`, `platform/android/jni_bridge.c`, `platform/android/CMakeLists.txt`, `app/src/main/java/com/tengen/gbaemu/NativeBridge.kt`
- Create: `platform/android/jni_shared.h`, `platform/android/jni_ra.c`
- Test: `app/src/androidTest/java/com/tengen/gbaemu/NativeLibraryTest.kt` (append)

The emulation thread is not built on the PC; this task is checked by building the app and running the on-device tests (Task 8 adds the RetroAchievements ones).

- [x] **Step 1: Write the failing on-device test**

Append to `NativeLibraryTest.kt` (inside the class):

```kotlin
    @Test
    fun retroAchievementsIsBuiltIn() {
        assertTrue(NativeBridge.raUserAgentClause().startsWith("rcheevos/12.5"))
        assertEquals(0, NativeBridge.raLoginStatus()[0]) // nobody signed in
        assertFalse(NativeBridge.raHardcoreActive())
    }
```

(add `import org.junit.Assert.assertFalse` / `assertTrue` / `assertEquals` if missing.)

- [x] **Step 2: The glue in the Android build**

In `platform/android/CMakeLists.txt`, extend the RetroAchievements lines from Task 1:

```cmake
target_sources(gbaemu PRIVATE ${REPO_ROOT}/platform/ra/ra.c)
target_include_directories(gbaemu PRIVATE ${REPO_ROOT}/platform/ra)
```

- [x] **Step 3: The emulation thread**

`emu_thread.h`:
- `#include "ra.h"`.
- In `emu_options_t`, last field: `ra_t *ra; /* RetroAchievements (14): checked after every frame and its hardcore limits obeyed; NULL = none */`.
- After `emu_state_load`'s declaration:

```c
/* RetroAchievements while the game waits to start or is paused (any thread): ra_idle with the game's memory, when no
 * one else is using the core. */
void emu_ra_idle(emu_t *e);
```

`emu_thread.c`:

1. Struct fields (after `rw_steps`):

```c
    ra_t *ra;                      /* RetroAchievements, or NULL (14) */
    atomic_bool ra_reset;          /* forget half-done achievement progress before the next frame (a state load, a rewind) */
    pthread_mutex_t core_lock;     /* held by whoever uses a parked or not-started core: the UI thread's state
                                    * save / load, cheats, frame copy, peeks, emu_ra_idle; and the paused thread's ra_idle */
```

2. Helpers (after `keep_rewind_stats`):

```c
static ra_memory_t memory_of(emu_t *e) {
    ra_memory_t m = {gba_iwram(e->g), gba_ewram(e->g), gba_saveram(e->g), gba_saveram_size(e->g)};
    return m;
}

/* The hardcore limits apply (a signed-in user, hardcore on, a game with achievements). */
static bool hardcore(emu_t *e) {
    return e->ra && ra_hardcore_active(e->ra);
}

static void check_achievements(emu_t *e) {
    if (!e->ra) return;
    ra_memory_t m = memory_of(e);
    if (atomic_exchange(&e->ra_reset, false)) ra_reset_progress(e->ra, &m);
    ra_do_frame(e->ra, &m);
}
```

3. `play_frame`: after `gba_run_frame(e->g);` add `check_achievements(e);`.

4. `emu_main`:
   - The pause wait: replace `while (e->paused && !e->quit) pthread_cond_wait(&e->wake, &e->lock);` with:

```c
            while (e->paused && !e->quit) { /* RetroAchievements' upkeep about once a second (14) */
                struct timespec until;
                clock_gettime(CLOCK_MONOTONIC, &until);
                until.tv_sec += 1;
                if (pthread_cond_timedwait(&e->wake, &e->lock, &until) == ETIMEDOUT && e->paused && !e->quit && e->ra) {
                    pthread_mutex_unlock(&e->lock);
                    pthread_mutex_lock(&e->core_lock);
                    ra_memory_t m = memory_of(e);
                    ra_idle(e->ra, &m);
                    pthread_mutex_unlock(&e->core_lock);
                    pthread_mutex_lock(&e->lock);
                }
            }
```

   - Rewind: `if (atomic_load(&e->rewinding)) {` → `if (atomic_load(&e->rewinding) && !hardcore(e)) {`.
   - When rewinding ends (`if (was_rewinding) {` block): add `atomic_store(&e->ra_reset, true); /* achievements: the game went back */`.

5. `emu_options_default`: `emu_options_t o = {NULL, 0, EMU_AUTO, EMU_AUTO, EMU_REWIND_DEFAULT, true, 2, NULL};`

6. `emu_create`: after `pthread_cond_init(&e->wake, &attr); …destroy(&attr);` add `pthread_mutex_init(&e->core_lock, NULL);`; with the other `atomic_init`s add `atomic_init(&e->ra_reset, false);` and `e->ra = o.ra;`.

7. Replace `core_free` and its users:

```c
/* The core may be used from another thread only while the emulation thread is parked or not started; core_take holds
 * the core lock (so the paused thread's ra_idle waits) and says whether it is free. Give it back with core_give. */
static bool core_take(emu_t *e) {
    pthread_mutex_lock(&e->core_lock);
    pthread_mutex_lock(&e->lock);
    bool free_now = !e->started || e->parked;
    pthread_mutex_unlock(&e->lock);
    if (!free_now) pthread_mutex_unlock(&e->core_lock);
    return free_now;
}

static void core_give(emu_t *e) {
    pthread_mutex_unlock(&e->core_lock);
}
```

and rewrite the users:

```c
bool emu_state_save(emu_t *e, const char *path) {
    if (!core_take(e)) return false;
    size_t size = gba_state_size(e->g);
    uint8_t *buf = malloc(size);
    bool ok = buf && gba_state_save(e->g, buf, size) == size && save_file_replace(path, buf, size);
    free(buf);
    core_give(e);
    return ok;
}

gba_err_t emu_state_load(emu_t *e, const char *path) {
    if (hardcore(e) || !core_take(e)) return GBA_ERR_STATE; /* hardcore: no state loads (14) */
    size_t size = 0;
    uint8_t *buf = save_file_load(path, &size);
    gba_err_t err = buf ? gba_state_load(e->g, buf, size) : GBA_ERR_STATE;
    free(buf);
    if (err == GBA_OK) {
        save_timer_frame(&e->save_timer, true); /* the loaded save chip → the save file */
        atomic_store(&e->ra_reset, true);        /* achievements: the game jumped */
    }
    core_give(e);
    return err;
}

gba_err_t emu_cheats_set(emu_t *e, const char *const *texts, const gba_cheat_type_t *types, size_t count) {
    if (!core_take(e)) return GBA_ERR_CHEAT;
    gba_err_t err;
    if (hardcore(e)) { /* hardcore: no cheats (14) */
        gba_cheats_set(e->g, NULL, NULL, 0);
        err = count ? GBA_ERR_CHEAT : GBA_OK;
    } else {
        err = gba_cheats_set(e->g, texts, types, count);
    }
    core_give(e);
    return err;
}

bool emu_frame(emu_t *e, uint16_t out[240 * 160]) {
    if (!core_take(e)) return false;
    memcpy(out, gba_framebuffer(e->g), sizeof(uint16_t) * 240 * 160);
    core_give(e);
    return true;
}

uint32_t emu_debug_peek32(emu_t *e, uint32_t addr) {
    if (!core_take(e)) return 0xDEADDEADu;
    uint32_t v = gba_debug_peek32(e->g, addr);
    core_give(e);
    return v;
}

void emu_ra_idle(emu_t *e) {
    if (!e->ra || !core_take(e)) return;
    ra_memory_t m = memory_of(e);
    ra_idle(e->ra, &m);
    core_give(e);
}
```

8. `emu_destroy`: `pthread_mutex_destroy(&e->core_lock);` beside the other destroys.

- [x] **Step 4: The JNI files**

`platform/android/jni_shared.h`:

```c
#ifndef GBAEMU_JNI_SHARED_H
#define GBAEMU_JNI_SHARED_H

#include "emu_thread.h"
#include "ra.h"

/* What the two JNI files share (milestone 14). */
emu_t *jni_emu(void); /* the game session, or NULL (jni_bridge.c) */
ra_t *jni_ra(void);   /* RetroAchievements for the app's whole life, made on first use; NULL without memory (jni_ra.c) */

#endif
```

`jni_bridge.c`:
- `#include "jni_shared.h"`.
- `emu_t *jni_emu(void) { return s_emu; }` after the statics.
- `stop_session`: after `s_emu = NULL;` add `if (jni_ra()) ra_unload_game(jni_ra()); /* after the thread has stopped: no more frames */`.
- `loadRom`: `opt.ra = jni_ra();` beside the other `opt.` lines.

`platform/android/jni_ra.c`:

```c
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

/* [kind, id, points, title, description, progress, badge url] as UTF-8, or null when none is waiting. */
JNIEXPORT jobjectArray JNICALL JNI_FN(raTakeEvent)(JNIEnv *env, jobject self) {
    (void)self;
    ra_event_t e;
    if (!jni_ra() || !ra_take_event(jni_ra(), &e)) return NULL;
    char kind[16], id[16], points[16];
    snprintf(kind, sizeof kind, "%d", e.kind);
    snprintf(id, sizeof id, "%u", (unsigned)e.id);
    snprintf(points, sizeof points, "%u", (unsigned)e.points);
    const char *v[7] = {kind, id, points, e.title, e.description, e.progress, e.badge_url};
    return byte_arrays(env, v, 7);
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
```

- [x] **Step 5: The Kotlin declarations**

In `NativeBridge.kt`, before the debug functions:

```kotlin
    // RetroAchievements (14): platform/ra/ra.h through platform/android/jni_ra.c. Text is UTF-8 bytes.
    const val RA_NOTHING = 0
    const val RA_BUSY = 1
    const val RA_OK = 2
    const val RA_FAILED = 3

    /** The next request of rcheevos' (id, url, post or null, content type or null), or null after [timeoutMs]. */
    external fun raWaitRequest(timeoutMs: Int): Array<String?>?
    external fun raReply(id: Int, status: Int, body: ByteArray?)
    external fun raLoginPassword(user: ByteArray, password: ByteArray)
    external fun raLoginToken(user: ByteArray, token: ByteArray)
    external fun raLogout()
    /** [status (RA_*), rcheevos' code]. */
    external fun raLoginStatus(): IntArray
    external fun raLoginMessage(): ByteArray
    /** [name, token], or null when signed out. */
    external fun raUser(): Array<ByteArray>?
    /** The ROM given to [loadRom]: its MD5 identifies the game. */
    external fun raLoadGame(rom: ByteBuffer)
    external fun raUnloadGame()
    /** While waiting for [raLoadGame]: lets the load finish. */
    external fun raPoll()
    external fun raGameStatus(): IntArray
    external fun raSetHardcore(on: Boolean)
    /** The hardcore limits apply now (hardcore on, signed in, the game has achievements). */
    external fun raHardcoreActive(): Boolean
    /** 0: may pause; else frames to play first. */
    external fun raCanPause(): Int
    /** [kind, id, points, title, description, progress, badge url], or null. */
    external fun raTakeEvent(): Array<ByteArray>?
    /** [unlocked, total, points unlocked, points total], or null. */
    external fun raSummary(): IntArray?
    external fun raGameTitle(): ByteArray
    /** 7 texts per achievement, unlocked first: id, points, unlocked 1/0, title, description, progress, badge url. */
    external fun raAchievements(): Array<ByteArray>
    external fun raUserAgentClause(): String
```

- [x] **Step 6: Build and run the on-device tests**

Run: `bash tools/dev.sh apk` → BUILD SUCCESSFUL, no new C warnings (`-Wall -Wextra`). `bash tools/dev.sh androidtest` → all pass, including `retroAchievementsIsBuiltIn` (the state, cheat, rewind and fingerprint tests show the core lock and the loop change broke nothing). `bash tools/dev.sh test` → all CTest pass.

- [x] **Step 7: Docs and commit**

```bash
git add platform/android app/src/main/java/com/tengen/gbaemu/NativeBridge.kt app/src/androidTest/java/com/tengen/gbaemu/NativeLibraryTest.kt docs
git commit -m "feat(android): RetroAchievements in the game loop (core lock, hardcore limits) and NativeBridge.ra*

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

## Task 7: Kotlin — the internet for rcheevos, the sign-in file

**Files:**
- Modify: `app/src/main/java/com/tengen/gbaemu/Net.kt`
- Create: `app/src/main/java/com/tengen/gbaemu/RaNet.kt`, `app/src/main/java/com/tengen/gbaemu/RaAccount.kt`
- Test: `app/src/test/java/com/tengen/gbaemu/NetTest.kt` (change + append), `RaNetTest.kt`, `RaAccountTest.kt`, `BackupTest.kt`, `RestoreTest.kt` (append)

- [x] **Step 1: Write the failing tests**

In `NetTest.kt`, make the little server read a POST body and remember it, and let a reply carry a status with a body. Replace the `Server` class's `s.use { … }` block with:

```kotlin
                    s.use {
                        val input = s.getInputStream()
                        val head = generateSequence { readLine(input) }.takeWhile { it.isNotEmpty() }.toList()
                        lastRequest = head.joinToString("\n")
                        val length = head.firstOrNull { it.startsWith("Content-Length:", ignoreCase = true) }
                            ?.substringAfter(':')?.trim()?.toIntOrNull() ?: 0
                        lastBody = String(input.readNBytes(length))
                        val path = head.firstOrNull()?.split(" ")?.getOrNull(1) ?: "/"
                        val (status, body) = replies[path] ?: (404 to ByteArray(0))
                        val out = s.getOutputStream()
                        out.write("HTTP/1.1 $status X\r\nContent-Length: ${body.size}\r\nConnection: close\r\n\r\n".toByteArray())
                        out.write(body)
                        out.flush()
                    }
```

with `@Volatile var lastBody = ""` beside `lastRequest`, and in the class:

```kotlin
        /** One header line (the body is read as bytes after the headers, so no buffered reader may run ahead). */
        private fun readLine(input: java.io.InputStream): String {
            val b = java.io.ByteArrayOutputStream()
            while (true) {
                val c = input.read()
                if (c < 0 || c == '\n'.code) break
                if (c != '\r'.code) b.write(c)
            }
            return b.toString()
        }
```

Append tests:

```kotlin
    @Test
    fun sendPostsTheFormAndGivesStatusAndBody() {
        Server(mapOf("/dorequest.php" to (401 to "{\"Success\":false}".toByteArray()))).use { s ->
            val r = Net.send("${s.url}/dorequest.php", "r=login2&u=Tester".toByteArray(), "application/x-www-form-urlencoded",
                "GBAEmulator/1.0 (Android 16) rcheevos/12.5", 1000)
            assertEquals(401, r.status)
            assertEquals("{\"Success\":false}", String(r.body!!))
            assertEquals("r=login2&u=Tester", s.lastBody)
            assertTrue(s.lastRequest, "POST /dorequest.php" in s.lastRequest)
            assertTrue(s.lastRequest, "User-Agent: GBAEmulator/1.0 (Android 16) rcheevos/12.5" in s.lastRequest)
            assertTrue(s.lastRequest, "Content-Type: application/x-www-form-urlencoded" in s.lastRequest)
        }
    }

    @Test
    fun sendWithoutABodyIsAGetAndNoServerIsNoReply() {
        Server(mapOf("/b.png" to (200 to byteArrayOf(1, 2)))).use { s ->
            val r = Net.send("${s.url}/b.png", null, null, "UA", 1000)
            assertEquals(200, r.status)
            assertArrayEquals(byteArrayOf(1, 2), r.body)
            assertTrue(s.lastRequest, "GET /b.png" in s.lastRequest)
            assertEquals(Net.NO_REPLY, Net.send("${s.url}/b.png", null, null, "UA", 1).status) // too big
        }
        assertEquals(Net.NO_REPLY, Net.send("http://127.0.0.1:1/x", null, null, "UA", 1000).status)
    }
```

(import `org.junit.Assert.assertArrayEquals`.)

`app/src/test/java/com/tengen/gbaemu/RaNetTest.kt`:

```kotlin
package com.tengen.gbaemu

import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class RaNetTest {
    @Test
    fun aRequestIsReadFromTheNativeParts() {
        val r = RaRequest.of(arrayOf("7", "https://retroachievements.org/dorequest.php", "r=login2", "application/x-www-form-urlencoded"))!!
        assertEquals(7, r.id)
        assertEquals("r=login2", r.post)
        val get = RaRequest.of(arrayOf("8", "https://media.retroachievements.org/Badge/1.png", null, null))!!
        assertNull(get.post)
        assertNull(RaRequest.of(null))
        assertNull(RaRequest.of(arrayOf("x", "u", null, null)))
        assertNull(RaRequest.of(arrayOf("1", null, null, null)))
    }

    @Test
    fun theUserAgentNamesTheAppAndrcheevos() {
        assertEquals("GBAEmulator/1.0 (Android 16) rcheevos/12.5", RaNet.userAgent("16", "rcheevos/12.5"))
    }

    @Test
    fun theWorkerAnswersEveryRequestWithItsReply() {
        val queue = java.util.concurrent.LinkedBlockingQueue(listOf(RaRequest(1, "u1", "a", "t"), RaRequest(2, "u2", null, null)))
        val replies = java.util.concurrent.ConcurrentHashMap<Int, Pair<Int, ByteArray?>>()
        val done = CountDownLatch(2)
        val worker = RaWorker(
            take = { ms -> queue.poll(ms.toLong(), TimeUnit.MILLISECONDS) },
            reply = { id, status, body -> replies[id] = status to body; done.countDown() },
        )
        worker.send = { r -> if (r.post != null) Net.Reply(200, "ok ${r.id}".toByteArray()) else Net.Reply(Net.NO_REPLY, null) }
        worker.start()
        assertTrue(done.await(5, TimeUnit.SECONDS))
        worker.stop()
        assertEquals(200, replies[1]!!.first)
        assertArrayEquals("ok 1".toByteArray(), replies[1]!!.second)
        assertEquals(Net.NO_REPLY, replies[2]!!.first)
        assertNull(replies[2]!!.second)
    }
}
```

`app/src/test/java/com/tengen/gbaemu/RaAccountTest.kt`:

```kotlin
package com.tengen.gbaemu

import java.io.File
import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class RaAccountTest {
    private val dir: File = Files.createTempDirectory("ra").toFile()
    private val file = File(dir, "ra.json")

    @Test
    fun theNameAndTokenAreKeptAndReadBack() {
        val a = RaAccount(file)
        assertNull(a.load())
        assertTrue(a.save("Tester", "TOKEN"))
        assertEquals("Tester" to "TOKEN", RaAccount(file).load())
        assertFalse(File(dir, "ra.json.tmp").exists())
    }

    @Test
    fun clearForgetsItAndADamagedFileIsNoSignIn() {
        val a = RaAccount(file)
        a.save("Tester", "TOKEN")
        a.clear()
        assertNull(a.load())
        assertFalse(file.exists())
        file.writeText("{\"user\": \"Tester\"")
        assertNull(a.load())
        file.writeText("{\"user\": \"\", \"token\": \"T\"}")
        assertNull(a.load())
    }
}
```

In `BackupTest.onlyTheAppsPathsAreAllowed`, add `"ra.json", "ra/badges/01001.png", "ra-fake"` to the list of paths that are **not** allowed.

In `RestoreTest`, add:

```kotlin
    @Test
    fun theRetroAchievementsSignInIsNeitherBackedUpNorTouchedByARestore() {
        val a = Phone("a")
        full(a)
        a.put("ra.json", "{\"user\": \"A\", \"token\": \"TA\"}")
        a.put("ra/badges/01001.png", "badge")
        val bytes = a.backup()
        val b = Phone("b")
        b.put("ra.json", "{\"user\": \"B\", \"token\": \"TB\"}")
        assertTrue(restore(b, bytes).ok)
        assertEquals("{\"user\": \"B\", \"token\": \"TB\"}", File(b.dir, "ra.json").readText())
        assertFalse(File(b.dir, "ra/badges/01001.png").exists())
    }
```

- [x] **Step 2: Run them to see them fail**

Run: `bash tools/dev.sh jvmtest`
Expected: compile errors (`Net.send`, `RaRequest`, `RaWorker`, `RaAccount` unknown). (The Backup/Restore additions pass already: the backup is an allow-list and a restore touches only its own parts — the tests lock that in.)

- [x] **Step 3: Write the code**

In `Net.kt`, inside `object Net` (after `get`):

```kotlin
    /** A reply with its HTTP status and body (also for 4xx/5xx), or [NO_REPLY] with no body. */
    class Reply(val status: Int, val body: ByteArray?)

    /** No connection, a timeout, a body over the limit: rcheevos' "retryable client error" (platform/ra/ra.h RA_NO_REPLY). */
    const val NO_REPLY = -2

    /** GET [url], or POST [post] as [contentType]; 15 s to connect and to read; at most [max] bytes back. */
    fun send(url: String, post: ByteArray?, contentType: String?, userAgent: String, max: Int): Reply = try {
        val c = URL(url).openConnection() as HttpURLConnection
        try {
            c.connectTimeout = TIMEOUT
            c.readTimeout = TIMEOUT
            c.instanceFollowRedirects = true
            c.setRequestProperty("User-Agent", userAgent)
            if (post != null) {
                c.requestMethod = "POST"
                c.doOutput = true
                c.setRequestProperty("Content-Type", contentType ?: "application/x-www-form-urlencoded")
                // Not setFixedLengthStreamingMode: in streaming mode Java drops a 401's body, where RetroAchievements
                // puts its "wrong password" message. The forms are small.
                c.outputStream.use { it.write(post) }
            }
            val status = c.responseCode
            val stream = if (status >= 400) c.errorStream else c.inputStream
            if (stream == null) Reply(status, ByteArray(0))
            else stream.use { readUpTo(it, max) }?.let { Reply(status, it) } ?: Reply(NO_REPLY, null)
        } finally {
            c.disconnect()
        }
    } catch (e: Exception) {
        Reply(NO_REPLY, null)
    }
```

`app/src/main/java/com/tengen/gbaemu/RaNet.kt`:

```kotlin
package com.tengen.gbaemu

import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import kotlin.concurrent.thread

/** One of rcheevos' web requests (14): from NativeBridge.raWaitRequest; [post] null = a GET. */
class RaRequest(val id: Int, val url: String, val post: String?, val contentType: String?) {
    companion object {
        /** From raWaitRequest's [id, url, post, content type]; null if it isn't one. */
        fun of(parts: Array<String?>?): RaRequest? {
            if (parts == null || parts.size != 4) return null
            val id = parts[0]?.toIntOrNull() ?: return null
            val url = parts[1] ?: return null
            return RaRequest(id, url, parts[2], parts[3])
        }
    }
}

object RaNet {
    const val MAX_REPLY = 4 shl 20

    /** The app as RetroAchievements sees it: it recognises approved emulators by this name and version. */
    fun userAgent(androidRelease: String, rcheevosClause: String) = "GBAEmulator/1.0 (Android $androidRelease) $rcheevosClause"

    fun send(r: RaRequest, userAgent: String): Net.Reply =
        Net.send(r.url, r.post?.toByteArray(Charsets.UTF_8), r.contentType, userAgent, MAX_REPLY)
}

/**
 * Does rcheevos' internet: one thread waits for each request ([take], a timeout in ms) and two more send them ([send])
 * and hand back the answers ([reply]: id, status, body). [send] is the internet, or the pretend server.
 */
class RaWorker(private val take: (Int) -> RaRequest?, private val reply: (Int, Int, ByteArray?) -> Unit) {
    @Volatile var send: (RaRequest) -> Net.Reply = { Net.Reply(Net.NO_REPLY, null) }
    @Volatile private var running = false
    private var pool: ExecutorService? = null

    @Synchronized
    fun start() {
        if (running) return
        running = true
        val p = Executors.newFixedThreadPool(2) { r -> Thread(r, "ra-send").apply { isDaemon = true } }
        pool = p
        thread(isDaemon = true, name = "ra-requests") {
            while (running) {
                val r = take(1000) ?: continue
                p.execute {
                    val answer = try { send(r) } catch (e: Exception) { Net.Reply(Net.NO_REPLY, null) }
                    reply(r.id, answer.status, answer.body)
                }
            }
        }
    }

    @Synchronized
    fun stop() {
        running = false
        pool?.shutdown()
    }
}

/** The app's one worker, for its whole life (the native side's ra_t lives as long). */
object RaService {
    val worker = RaWorker(take = { ms -> RaRequest.of(NativeBridge.raWaitRequest(ms)) }, reply = NativeBridge::raReply)
}
```

`app/src/main/java/com/tengen/gbaemu/RaAccount.kt`:

```kotlin
package com.tengen.gbaemu

import java.io.File

/**
 * The RetroAchievements sign-in kept on the phone (14): the user's name and login token — never the password — in
 * `files/ra.json`. Backups leave it out (Backup's allow-list) and a restore never touches it.
 */
class RaAccount(private val file: File) {
    fun load(): Pair<String, String>? = try {
        val r = Json.Reader(file.readText())
        val m = r.value() as Map<*, *>
        r.end()
        val user = m["user"] as? String
        val token = m["token"] as? String
        if (user.isNullOrEmpty() || token.isNullOrEmpty()) null else user to token
    } catch (e: Exception) {
        null
    }

    fun save(user: String, token: String): Boolean = try {
        val tmp = File(file.path + ".tmp")
        tmp.writeText("{\"user\": ${Json.str(user)}, \"token\": ${Json.str(token)}}")
        tmp.renameTo(file) || (file.delete() && tmp.renameTo(file))
    } catch (e: Exception) {
        false
    }

    fun clear() {
        file.delete()
    }
}
```

- [x] **Step 4: Run the tests**

Run: `bash tools/dev.sh jvmtest` → all pass.

- [x] **Step 5: Docs and commit**

```bash
git add app/src/main/java/com/tengen/gbaemu/Net.kt app/src/main/java/com/tengen/gbaemu/RaNet.kt app/src/main/java/com/tengen/gbaemu/RaAccount.kt app/src/test docs
git commit -m "feat(app): POST for rcheevos, the request worker, the sign-in file kept out of backups

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

## Task 8: Kotlin — the achievements controller, the pretend server, on-device tests

**Files:**
- Create: `app/src/main/java/com/tengen/gbaemu/Achievements.kt`, `RaPopups.kt`, `FakeRaServer.kt`
- Test: `app/src/test/java/com/tengen/gbaemu/AchievementsTest.kt`, `RaPopupsTest.kt`, `FakeRaServerTest.kt`, `app/src/androidTest/java/com/tengen/gbaemu/AchievementsDeviceTest.kt`

- [x] **Step 1: Write the failing JVM tests**

`AchievementsTest.kt`:

```kotlin
package com.tengen.gbaemu

import java.io.File
import java.nio.ByteBuffer
import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class AchievementsTest {
    /** The native side as a script: each call is noted; statuses are set by the test. */
    private class FakeNative : RaNative {
        var login = intArrayOf(NativeBridge.RA_NOTHING, 0)
        var message = ""
        var userInfo: Array<String>? = null
        var game = intArrayOf(NativeBridge.RA_NOTHING, 0)
        var summaryInfo: IntArray? = null
        val calls = mutableListOf<String>()
        override fun loginPassword(user: String, password: String) { calls += "password $user"; login = intArrayOf(NativeBridge.RA_BUSY, 0) }
        override fun loginToken(user: String, token: String) { calls += "token $user $token"; login = intArrayOf(NativeBridge.RA_BUSY, 0) }
        override fun logout() { calls += "logout"; login = intArrayOf(NativeBridge.RA_NOTHING, 0) }
        override fun loginStatus() = login
        override fun loginMessage() = message
        override fun user() = userInfo
        override fun loadGame(rom: ByteBuffer) { calls += "load"; game = intArrayOf(NativeBridge.RA_BUSY, 0) }
        override fun unloadGame() { calls += "unload"; game = intArrayOf(NativeBridge.RA_NOTHING, 0) }
        override fun poll() { calls += "poll" }
        override fun gameStatus() = game
        override fun summary() = summaryInfo
    }

    private val dir: File = Files.createTempDirectory("ach").toFile()
    private val account = RaAccount(File(dir, "ra.json"))
    private val native = FakeNative()
    private val rom: ByteBuffer = ByteBuffer.allocateDirect(4)

    private fun signedIn(): Achievements {
        val a = Achievements(native, account)
        a.signIn("Tester", "secret")
        native.login = intArrayOf(NativeBridge.RA_OK, 0)
        native.userInfo = arrayOf("Tester", "TOKEN")
        assertTrue(a.update())
        return a
    }

    @Test
    fun aPasswordSignInKeepsTheTokenNotThePassword() {
        val a = signedIn()
        assertEquals(Achievements.SignIn.IN, a.signIn)
        assertEquals("Tester", a.name)
        val kept = File(dir, "ra.json").readText()
        assertTrue(kept, "TOKEN" in kept)
        assertFalse(kept, "secret" in kept)
        assertFalse(a.update()) // nothing new
    }

    @Test
    fun aWrongPasswordShowsTheServersMessageAndKeepsNothing() {
        val a = Achievements(native, account)
        a.signIn("Tester", "nope")
        assertEquals(Achievements.SignIn.BUSY, a.signIn)
        assertFalse(a.update()) // still busy
        native.login = intArrayOf(NativeBridge.RA_FAILED, Achievements.RC_INVALID_CREDENTIALS)
        native.message = "Invalid User/Password combination. Please try again."
        assertTrue(a.update())
        assertEquals(Achievements.SignIn.OUT, a.signIn)
        assertEquals("Invalid User/Password combination. Please try again.", a.error)
        assertNull(account.load())
    }

    @Test
    fun theSavedSignInIsUsedAtStart() {
        account.save("Tester", "TOKEN")
        val a = Achievements(native, account)
        a.start()
        assertEquals(listOf("token Tester TOKEN"), native.calls)
        assertEquals(Achievements.SignIn.BUSY, a.signIn)
        native.login = intArrayOf(NativeBridge.RA_OK, 0)
        native.userInfo = arrayOf("Tester", "TOKEN")
        a.update()
        assertEquals(Achievements.SignIn.IN, a.signIn)
    }

    @Test
    fun anExpiredTokenAsksToSignInAgain() {
        account.save("Tester", "OLD")
        val a = Achievements(native, account)
        a.start()
        native.login = intArrayOf(NativeBridge.RA_FAILED, Achievements.RC_EXPIRED_TOKEN)
        a.update()
        assertEquals(Achievements.SignIn.AGAIN, a.signIn)
        assertNull(account.load())
        assertFalse(a.loadGame(rom)) // no game is identified until the user signs in again
    }

    @Test
    fun noInternetAtStartKeepsTheSignInAndTriesAgainWithTheNextGame() {
        account.save("Tester", "TOKEN")
        val a = Achievements(native, account)
        a.start()
        native.login = intArrayOf(NativeBridge.RA_FAILED, -32) // RC_NO_RESPONSE
        a.update()
        assertEquals(Achievements.SignIn.IN, a.signIn)
        assertEquals("Tester" to "TOKEN", account.load())
        assertTrue(a.loadGame(rom))
        assertEquals(listOf("token Tester TOKEN", "token Tester TOKEN", "load"), native.calls)
    }

    @Test
    fun signOutForgetsEverything() {
        val a = signedIn()
        a.signOut()
        assertEquals(Achievements.SignIn.OUT, a.signIn)
        assertNull(a.name)
        assertNull(account.load())
        assertTrue("logout" in native.calls)
    }

    @Test
    fun aGameWithASetSaysHowManyAreUnlocked() {
        val a = signedIn()
        assertTrue(a.loadGame(rom))
        assertNull(a.gameStep(timedOut = false))
        assertTrue("poll" in native.calls)
        native.game = intArrayOf(NativeBridge.RA_OK, 0)
        native.summaryInfo = intArrayOf(1, 5, 5, 43)
        assertEquals(Achievements.Game.Set(1, 5), a.gameStep(timedOut = false))
    }

    @Test
    fun anUnknownGameHasNoSetAndAFailedLoadIsOffline() {
        val a = signedIn()
        a.loadGame(rom)
        native.game = intArrayOf(NativeBridge.RA_FAILED, Achievements.RC_NO_GAME_LOADED)
        assertEquals(Achievements.Game.NoSet, a.gameStep(timedOut = false))
        a.loadGame(rom)
        native.game = intArrayOf(NativeBridge.RA_FAILED, -32)
        assertEquals(Achievements.Game.Offline, a.gameStep(timedOut = false))
        a.loadGame(rom)
        native.game = intArrayOf(NativeBridge.RA_OK, 0)
        native.summaryInfo = null // loaded, but no achievements
        assertEquals(Achievements.Game.NoSet, a.gameStep(timedOut = false))
    }

    @Test
    fun noAnswerInTimeUnloadsAndSaysOffline() {
        val a = signedIn()
        a.loadGame(rom)
        assertEquals(Achievements.Game.Offline, a.gameStep(timedOut = true))
        assertTrue("unload" in native.calls)
    }

    @Test
    fun signedOutIdentifiesNothing() {
        assertFalse(Achievements(native, account).loadGame(rom))
        assertFalse("load" in native.calls)
    }
}
```

`RaPopupsTest.kt`:

```kotlin
package com.tengen.gbaemu

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class RaPopupsTest {
    private fun ev(kind: Int, id: Int = 1, title: String = "T", progress: String = "") =
        RaEvent(kind, id, 5, title, "D", progress, "https://m/Badge/0$id.png")

    @Test
    fun anEventIsReadFromItsUtf8Parts() {
        val parts = listOf("1", "1001", "5", "Première ★", "Desc", "", "https://m/Badge/01001.png").map { it.toByteArray() }
        assertEquals(RaEvent(RaEvent.UNLOCKED, 1001, 5, "Première ★", "Desc", "", "https://m/Badge/01001.png"),
            RaEvent.of(parts.toTypedArray()))
        assertNull(RaEvent.of(null))
        assertNull(RaEvent.of(arrayOf("x".toByteArray())))
    }

    @Test
    fun theListIsReadSevenPartsAnAchievement() {
        val parts = listOf("1004", "1", "1", "Saver", "Save once", "", "u1", "1002", "10", "0", "Collector", "Collect 10", "4/10", "u2")
            .map { it.toByteArray() }.toTypedArray()
        val list = RaAchievement.list(parts)
        assertEquals(2, list.size)
        assertTrue(list[0].unlocked)
        assertEquals("4/10", list[1].progress)
        assertFalse(list[1].unlocked)
    }

    @Test
    fun cardsShowOneAtATimeForTheirTime() {
        val p = RaPopups()
        p.add(ev(RaEvent.UNLOCKED, 1, "One"), 0)
        p.add(ev(RaEvent.UNLOCKED, 2, "Two"), 0)
        assertEquals("One", p.view().card!!.title)
        assertFalse(p.tick(RaPopups.UNLOCK_MS - 1))
        assertTrue(p.tick(RaPopups.UNLOCK_MS))
        assertEquals("Two", p.view().card!!.title)
        p.tick(2 * RaPopups.UNLOCK_MS)
        assertNull(p.view().card)
    }

    @Test
    fun masteredStaysLongerAndANoteIsShort() {
        val p = RaPopups()
        p.add(RaEvent(RaEvent.MASTERED, 0, 43, "Test Game", "", "", ""), 0)
        p.tick(RaPopups.UNLOCK_MS)
        assertEquals(RaEvent.MASTERED, p.view().card!!.kind)
        p.tick(RaPopups.MASTERED_MS)
        p.note("1 of 5 achievements", RaPopups.MASTERED_MS)
        assertEquals("1 of 5 achievements", p.view().card!!.title)
        p.tick(RaPopups.MASTERED_MS + RaPopups.NOTE_MS)
        assertNull(p.view().card)
    }

    @Test
    fun progressShowsTheLatestForThreeSeconds() {
        val p = RaPopups()
        p.add(ev(RaEvent.PROGRESS, 2, "Collector", "3/10"), 0)
        p.add(ev(RaEvent.PROGRESS, 2, "Collector", "4/10"), 1000)
        assertEquals("4/10", p.view().progress!!.text)
        p.tick(1000 + RaPopups.PROGRESS_MS - 1)
        assertEquals("4/10", p.view().progress!!.text)
        p.tick(1000 + RaPopups.PROGRESS_MS)
        assertNull(p.view().progress)
        p.add(ev(RaEvent.PROGRESS, 2, "Collector", "5/10"), 5000)
        p.add(ev(RaEvent.PROGRESS_HIDE, 0), 5001)
        assertNull(p.view().progress)
    }

    @Test
    fun challengesComeAndGoByAchievement() {
        val p = RaPopups()
        p.add(ev(RaEvent.CHALLENGE_SHOW, 3), 0)
        p.add(ev(RaEvent.CHALLENGE_SHOW, 3), 0)
        p.add(ev(RaEvent.CHALLENGE_SHOW, 4), 0)
        assertEquals(listOf(3, 4), p.view().challenges.map { it.id })
        p.add(ev(RaEvent.CHALLENGE_HIDE, 3), 0)
        assertEquals(listOf(4), p.view().challenges.map { it.id })
        p.clear()
        assertEquals(RaPopups.View(), p.view())
    }

    @Test
    fun aServerErrorAndGoingOfflineAreCards() {
        val p = RaPopups()
        p.add(RaEvent(RaEvent.SERVER_ERROR, 1, 0, "", "The server refused this unlock.", "", ""), 0)
        assertEquals("Couldn't send an unlock", p.view().card!!.title)
        assertEquals("The server refused this unlock.", p.view().card!!.text)
        p.tick(RaPopups.NOTE_MS)
        p.add(RaEvent(RaEvent.OFFLINE, 0, 0, "", "", "", ""), RaPopups.NOTE_MS)
        assertEquals("Offline", p.view().card!!.title)
        p.add(RaEvent(RaEvent.ONLINE, 0, 0, "", "", "", ""), RaPopups.NOTE_MS) // nothing to show
        p.tick(2 * RaPopups.NOTE_MS)
        assertNull(p.view().card)
    }
}
```

`FakeRaServerTest.kt`:

```kotlin
package com.tengen.gbaemu

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class FakeRaServerTest {
    private fun ask(post: String) = FakeRaServer.reply(RaRequest(1, "https://retroachievements.org/dorequest.php", post, "application/x-www-form-urlencoded"))
    private fun text(r: Net.Reply) = String(r.body!!)

    @Test
    fun itKnowsTesterAndAnExpiredToken() {
        val ok = ask("r=login2&u=Tester&p=secret")
        assertEquals(200, ok.status)
        assertTrue(text(ok), "\"Token\":\"TOKEN\"" in text(ok))
        assertEquals(200, ask("r=login2&u=Tester&t=TOKEN").status)
        assertEquals(401, ask("r=login2&u=Tester&p=wrong").status)
        assertEquals(403, ask("r=login2&u=Tester&t=EXPIRED").status)
    }

    @Test
    fun everyGameHasTheDemoSetUnlessMadeUnknown() {
        val set = ask("r=achievementsets&u=Tester&t=TOKEN&m=abc")
        assertEquals(200, set.status)
        assertTrue("\"ConsoleId\":5" in text(set))
        assertEquals(5, Regex("\"ID\":").findAll(text(set)).count())
        FakeRaServer.unknownHashes = setOf("abc")
        try {
            assertEquals(404, ask("r=achievementsets&u=Tester&t=TOKEN&m=abc").status)
        } finally {
            FakeRaServer.unknownHashes = emptySet()
        }
    }

    @Test
    fun theRefusedAchievementIsRefusedAndTheRestCountDownToMastered() {
        ask("r=startsession&u=Tester&t=TOKEN&g=1234")
        val first = ask("r=awardachievement&u=Tester&t=TOKEN&a=1&h=1")
        assertEquals(200, first.status)
        assertTrue(text(first), "\"AchievementsRemaining\":3" in text(first)) // 2, 3 and 5 left (4 is refused)
        val no = ask("r=awardachievement&u=Tester&t=TOKEN&a=${FakeRaServer.REFUSED_ID}&h=1")
        assertEquals(400, no.status)
        assertTrue("refused" in text(no))
        ask("r=awardachievement&u=Tester&t=TOKEN&a=2&h=1")
        ask("r=awardachievement&u=Tester&t=TOKEN&a=3&h=1")
        assertTrue("\"AchievementsRemaining\":0" in text(ask("r=awardachievement&u=Tester&t=TOKEN&a=5&h=1")))
        assertEquals(200, ask("r=ping&u=Tester&t=TOKEN").status)
    }
}
```

- [x] **Step 2: Run them to see them fail**

Run: `bash tools/dev.sh jvmtest`
Expected: compile errors (`RaNative`, `Achievements`, `RaEvent`, `RaPopups`, `FakeRaServer` unknown).

- [x] **Step 3: Write the code**

`app/src/main/java/com/tengen/gbaemu/Achievements.kt`:

```kotlin
package com.tengen.gbaemu

import java.nio.ByteBuffer

/** The native side of RetroAchievements (NativeBridge's ra* functions) that the controller uses, as an interface for tests. */
interface RaNative {
    fun loginPassword(user: String, password: String)
    fun loginToken(user: String, token: String)
    fun logout()
    fun loginStatus(): IntArray // [status, code]
    fun loginMessage(): String
    fun user(): Array<String>? // [name, token]
    fun loadGame(rom: ByteBuffer)
    fun unloadGame()
    fun poll()
    fun gameStatus(): IntArray // [status, code]
    fun summary(): IntArray? // [unlocked, total, points unlocked, points total]
}

/** The real one. */
object NativeRa : RaNative {
    private fun bytes(s: String) = s.toByteArray(Charsets.UTF_8)
    override fun loginPassword(user: String, password: String) = NativeBridge.raLoginPassword(bytes(user), bytes(password))
    override fun loginToken(user: String, token: String) = NativeBridge.raLoginToken(bytes(user), bytes(token))
    override fun logout() = NativeBridge.raLogout()
    override fun loginStatus() = NativeBridge.raLoginStatus()
    override fun loginMessage() = String(NativeBridge.raLoginMessage(), Charsets.UTF_8)
    override fun user() = NativeBridge.raUser()?.map { String(it, Charsets.UTF_8) }?.toTypedArray()
    override fun loadGame(rom: ByteBuffer) = NativeBridge.raLoadGame(rom)
    override fun unloadGame() = NativeBridge.raUnloadGame()
    override fun poll() = NativeBridge.raPoll()
    override fun gameStatus() = NativeBridge.raGameStatus()
    override fun summary() = NativeBridge.raSummary()
}

/**
 * Signing in to RetroAchievements and identifying games (14). The password goes to rcheevos once; the token it gives
 * back is kept in [account]. A saved sign-in that the server refuses becomes [SignIn.AGAIN] ("Sign in again"); one that
 * can't reach the server stays [SignIn.IN] and is tried again with the next game.
 */
class Achievements(private val native: RaNative, private val account: RaAccount) {
    enum class SignIn { OUT, BUSY, IN, AGAIN }

    sealed interface Game {
        data object NoSet : Game
        data object Offline : Game
        data class Set(val unlocked: Int, val total: Int) : Game
    }

    var signIn = SignIn.OUT
        private set
    var name: String? = null
        private set
    var error: String? = null // the last failed password sign-in's reason
        private set
    private var byToken = false

    /** App start: the saved sign-in, if there is one. */
    fun start() {
        val (user, token) = account.load() ?: return
        name = user
        byToken = true
        signIn = SignIn.BUSY
        native.loginToken(user, token)
    }

    fun signIn(user: String, password: String) {
        error = null
        name = user
        byToken = false
        signIn = SignIn.BUSY
        native.loginPassword(user, password)
    }

    fun signOut() {
        native.logout()
        account.clear()
        name = null
        error = null
        signIn = SignIn.OUT
    }

    /** Picks up a finished sign-in; true when [signIn] changed. Call it often while [signIn] is BUSY. */
    fun update(): Boolean {
        if (signIn != SignIn.BUSY) return false
        val st = native.loginStatus()
        when (st[0]) {
            NativeBridge.RA_OK -> {
                native.user()?.let { (user, token) ->
                    name = user
                    if (!account.save(user, token)) error = "Couldn't keep the sign-in on this phone."
                }
                signIn = SignIn.IN
            }
            NativeBridge.RA_FAILED -> if (byToken) {
                if (st[1] in CREDENTIAL_ERRORS) {
                    account.clear()
                    signIn = SignIn.AGAIN
                } else {
                    signIn = SignIn.IN // no internet: kept, tried again with the next game
                }
            } else {
                error = native.loginMessage().ifEmpty { "Couldn't sign in." }
                signIn = SignIn.OUT
            }
            else -> return false
        }
        return true
    }

    /** A game was loaded ([rom]): starts identifying it when signed in; false when there is nothing to wait for. */
    fun loadGame(rom: ByteBuffer): Boolean {
        if (signIn != SignIn.IN && signIn != SignIn.BUSY) return false
        if (signIn == SignIn.IN && native.loginStatus()[0] != NativeBridge.RA_OK) {
            account.load()?.let { (user, token) ->
                byToken = true
                signIn = SignIn.BUSY
                native.loginToken(user, token)
            }
        }
        native.loadGame(rom)
        return true
    }

    /** One step of waiting for [loadGame] (every ~50 ms): null while RetroAchievements is still answering; [timedOut] gives up. */
    fun gameStep(timedOut: Boolean): Game? {
        native.poll()
        update()
        val st = native.gameStatus()
        return when (st[0]) {
            NativeBridge.RA_BUSY -> if (timedOut) Game.Offline.also { native.unloadGame() } else null
            NativeBridge.RA_OK -> native.summary()?.takeIf { it[1] > 0 }?.let { Game.Set(it[0], it[1]) } ?: Game.NoSet
            NativeBridge.RA_FAILED ->
                if (st[1] == RC_NO_GAME_LOADED || signIn == SignIn.AGAIN || signIn == SignIn.OUT) Game.NoSet else Game.Offline
            else -> Game.NoSet
        }
    }

    companion object {
        // rcheevos' codes (third_party/rcheevos/include/rc_error.h)
        const val RC_LOGIN_REQUIRED = -28
        const val RC_NO_GAME_LOADED = -29
        const val RC_ACCESS_DENIED = -33
        const val RC_INVALID_CREDENTIALS = -34
        const val RC_EXPIRED_TOKEN = -35
        val CREDENTIAL_ERRORS = setOf(RC_ACCESS_DENIED, RC_INVALID_CREDENTIALS, RC_EXPIRED_TOKEN)
    }
}
```

`app/src/main/java/com/tengen/gbaemu/RaPopups.kt`:

```kotlin
package com.tengen.gbaemu

/** One of the glue's events (platform/ra/ra.h ra_event_t), from NativeBridge.raTakeEvent. */
data class RaEvent(val kind: Int, val id: Int, val points: Int, val title: String, val description: String,
                   val progress: String, val badgeUrl: String) {
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

        fun of(parts: Array<ByteArray>?): RaEvent? {
            if (parts == null || parts.size != 7) return null
            val p = parts.map { String(it, Charsets.UTF_8) }
            val kind = p[0].toIntOrNull() ?: return null
            return RaEvent(kind, p[1].toIntOrNull() ?: 0, p[2].toIntOrNull() ?: 0, p[3], p[4], p[5], p[6])
        }
    }
}

/** One achievement of the pause menu's list. */
data class RaAchievement(val id: Int, val points: Int, val unlocked: Boolean, val title: String, val description: String,
                         val progress: String, val badgeUrl: String) {
    companion object {
        /** NativeBridge.raAchievements' texts, 7 per achievement. */
        fun list(parts: Array<ByteArray>): List<RaAchievement> = parts.map { String(it, Charsets.UTF_8) }.chunked(7)
            .filter { it.size == 7 }
            .map { p -> RaAchievement(p[0].toIntOrNull() ?: 0, p[1].toIntOrNull() ?: 0, p[2] == "1", p[3], p[4], p[5], p[6]) }
    }
}

/**
 * What the game screen shows for RetroAchievements (14), against a clock in ms: one card at a time (an unlock,
 * "mastered", a problem, a note) for a few seconds each; the latest progress for 3 s; the challenges going on.
 */
class RaPopups {
    data class Card(val kind: Int, val title: String, val text: String, val points: Int, val badgeUrl: String)
    data class Challenge(val id: Int, val badgeUrl: String)
    data class View(val card: Card? = null, val progress: Card? = null, val challenges: List<Challenge> = emptyList())

    private val waiting = ArrayDeque<Card>()
    private var card: Card? = null
    private var cardUntil = 0L
    private var progress: Card? = null
    private var progressUntil = 0L
    private val challenges = mutableListOf<Challenge>()

    fun add(e: RaEvent, now: Long) {
        when (e.kind) {
            RaEvent.UNLOCKED -> waiting += Card(e.kind, e.title, e.description, e.points, e.badgeUrl)
            RaEvent.MASTERED -> waiting += Card(e.kind, e.title, "Every achievement unlocked", e.points, e.badgeUrl)
            RaEvent.SERVER_ERROR -> waiting += Card(e.kind, "Couldn't send an unlock", e.description, 0, "")
            RaEvent.OFFLINE -> waiting += Card(e.kind, "Offline", "Unlocks are sent when you're back online", 0, "")
            RaEvent.PROGRESS -> {
                progress = Card(e.kind, e.title, e.progress, e.points, e.badgeUrl)
                progressUntil = now + PROGRESS_MS
            }
            RaEvent.PROGRESS_HIDE -> progress = null
            RaEvent.CHALLENGE_SHOW -> if (challenges.none { it.id == e.id }) challenges += Challenge(e.id, e.badgeUrl)
            RaEvent.CHALLENGE_HIDE -> challenges.removeAll { it.id == e.id }
        }
        tick(now)
    }

    /** A short line of the app's own ("1 of 5 achievements", "RetroAchievements: offline", …). */
    fun note(text: String, now: Long) {
        waiting += Card(NOTE, text, "", 0, "")
        tick(now)
    }

    /** Moves on with time; true when what is shown changed. */
    fun tick(now: Long): Boolean {
        var changed = false
        if (card != null && now >= cardUntil) {
            card = null
            changed = true
        }
        if (card == null && waiting.isNotEmpty()) {
            val c = waiting.removeFirst()
            card = c
            cardUntil = now + when (c.kind) {
                RaEvent.UNLOCKED -> UNLOCK_MS
                RaEvent.MASTERED -> MASTERED_MS
                else -> NOTE_MS
            }
            changed = true
        }
        if (progress != null && now >= progressUntil) {
            progress = null
            changed = true
        }
        return changed
    }

    fun view() = View(card, progress, challenges.toList())

    fun clear() {
        waiting.clear()
        card = null
        progress = null
        challenges.clear()
    }

    companion object {
        const val NOTE = 100
        const val UNLOCK_MS = 4000L
        const val MASTERED_MS = 6000L
        const val NOTE_MS = 3000L
        const val PROGRESS_MS = 3000L
    }
}
```

(The `PROGRESS` hide also clears the 3 s timer implicitly: `progress = null`. In `progressShowsTheLatestForThreeSeconds`, the add at 5000 happens after the earlier timer ran out — fine.)

`app/src/main/java/com/tengen/gbaemu/FakeRaServer.kt`:

```kotlin
package com.tengen.gbaemu

import java.net.URLDecoder

/**
 * A pretend RetroAchievements server inside the app (14), answering like the real one (and like platform/ra/ra_fake.c):
 * user "Tester", password "secret", token "TOKEN" ("EXPIRED" has expired). Every game has the demo set below — five
 * achievements that unlock by themselves as frames go by (a condition that is always true, with a hit count): an unlock
 * at 5 s, a progress bar to 20 s, a challenge until 30 s, a refused unlock at 10 s and "mastered" at 40 s.
 * Used by the on-device tests and, in a debug build with `files/ra-fake`, by the app (Claude's emulator check).
 */
object FakeRaServer {
    const val REFUSED_ID = 4
    @Volatile var unknownHashes: Set<String> = emptySet()

    private fun ach(id: Int, title: String, desc: String, points: Int, mem: String) =
        "{\"ID\":$id,\"Title\":\"$title\",\"Description\":\"$desc\",\"Flags\":3,\"Points\":$points,\"MemAddr\":\"$mem\"," +
            "\"Author\":\"Test\",\"BadgeName\":\"0000$id\",\"Created\":1,\"Modified\":1}"

    private val DEMO_SET = listOf(
        ach(1, "Welcome", "Play for 5 seconds", 5, "0xH000000>=0.300."),
        ach(2, "Getting There", "Play for 20 seconds", 10, "M:0xH000000>=0.1200."),
        ach(3, "Daredevil", "Keep going for 30 seconds", 25, "0xH000000>=0_T:0xH000000>=0.1800."),
        ach(REFUSED_ID, "Refused", "The server says no", 1, "0xH000000>=0.600."),
        ach(5, "The Long Wait", "Play for 40 seconds", 50, "0xH000000>=0.2400."),
    ).joinToString(",", "[", "]")

    private const val LOGIN_OK = "{\"Success\":true,\"User\":\"Tester\",\"Token\":\"TOKEN\",\"Score\":100,\"SoftcoreScore\":5," +
        "\"Messages\":0,\"Permissions\":1,\"AccountType\":\"Registered\"}"
    private const val LOGIN_BAD = "{\"Success\":false,\"Error\":\"Invalid User/Password combination. Please try again.\"," +
        "\"Status\":401,\"Code\":\"invalid_credentials\"}"
    private const val LOGIN_EXPIRED = "{\"Success\":false,\"Error\":\"The access token has expired. Please log in again.\"," +
        "\"Status\":401,\"Code\":\"expired_token\"}"
    private const val NOT_FOUND = "{\"Success\":false,\"Error\":\"Unknown game\",\"Code\":\"not_found\",\"Status\":404}"

    fun params(form: String): Map<String, String> = form.split('&').mapNotNull { pair ->
        val kv = pair.split('=', limit = 2)
        if (kv.size == 2) kv[0] to URLDecoder.decode(kv[1], "UTF-8") else null
    }.toMap()

    fun reply(r: RaRequest): Net.Reply {
        val p = params(r.post ?: r.url.substringAfter('?', ""))
        fun say(status: Int, body: String) = Net.Reply(status, body.toByteArray())
        return when (p["r"]) {
            "login2" -> when {
                p["t"] == "EXPIRED" -> say(403, LOGIN_EXPIRED)
                p["u"] == "Tester" && (p["p"] == "secret" || p["t"] == "TOKEN") -> say(200, LOGIN_OK)
                else -> say(401, LOGIN_BAD)
            }
            "achievementsets" -> if (p["m"] in unknownHashes) say(404, NOT_FOUND) else say(200,
                "{\"Success\":true,\"GameId\":1234,\"Title\":\"Demo Set\",\"ConsoleId\":5," +
                    "\"ImageIconUrl\":\"https://media.retroachievements.org/Images/000001.png\",\"RichPresenceGameId\":1234," +
                    "\"RichPresencePatch\":\"\",\"Sets\":[{\"AchievementSetId\":1111,\"GameId\":1234,\"Title\":null," +
                    "\"Type\":\"core\",\"ImageIconUrl\":\"https://media.retroachievements.org/Images/000001.png\"," +
                    "\"Achievements\":$DEMO_SET,\"Leaderboards\":[]}]}")
            "startsession" -> {
                synchronized(awarded) { awarded.clear() }
                say(200, "{\"Success\":true,\"Unlocks\":[],\"HardcoreUnlocks\":[]}")
            }
            "awardachievement" -> if (p["a"] == REFUSED_ID.toString()) {
                say(400, "{\"Success\":false,\"Error\":\"The server refused this unlock.\"}")
            } else {
                // How many are left, as the real server says (rcheevos shows "mastered" at 0); the refused one counts as
                // done here, so the demo still ends with "mastered".
                val left = synchronized(awarded) {
                    p["a"]?.toIntOrNull()?.let { awarded += it }
                    (DEMO_IDS - awarded - REFUSED_ID).size
                }
                say(200, "{\"Success\":true,\"Score\":105,\"SoftcoreScore\":5,\"AchievementID\":${p["a"]},\"AchievementsRemaining\":$left}")
            }
            else -> say(200, "{\"Success\":true}")
        }
    }

    private val DEMO_IDS = setOf(1, 2, 3, REFUSED_ID, 5)
    private val awarded = mutableSetOf<Int>()
}
```

- [x] **Step 4: Run the JVM tests**

Run: `bash tools/dev.sh jvmtest` → all pass.

- [x] **Step 5: Write the on-device tests**

`app/src/androidTest/java/com/tengen/gbaemu/AchievementsDeviceTest.kt`:

```kotlin
package com.tengen.gbaemu

import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/** RetroAchievements on the phone with the app's pretend server (14): sign in, a game's set, an unlock, the hardcore limits. */
@RunWith(AndroidJUnit4::class)
class AchievementsDeviceTest {
    @Before
    fun pretendServer() {
        RaService.worker.send = FakeRaServer::reply
        RaService.worker.start()
    }

    @After
    fun signOut() {
        NativeBridge.stop()
        NativeBridge.raLogout()
        NativeBridge.raSetHardcore(true)
    }

    private fun waitFor(ms: Long, what: () -> Boolean): Boolean {
        val end = System.currentTimeMillis() + ms
        while (System.currentTimeMillis() < end) {
            if (what()) return true
            Thread.sleep(20)
        }
        return what()
    }

    private fun signIn() {
        NativeRa.loginPassword("Tester", "secret")
        assertTrue(waitFor(5000) { NativeBridge.raLoginStatus()[0] == NativeBridge.RA_OK })
    }

    /** m7_demo loaded and its (demo) set loaded before the start. */
    private fun gameWithSet() {
        signIn()
        val rom = TestRoms.rom("tonc/m7_demo.gba")
        assertEquals(0, NativeBridge.loadRom(rom))
        NativeBridge.raLoadGame(rom)
        assertTrue(waitFor(5000) { NativeBridge.raPoll(); NativeBridge.raGameStatus()[0] == NativeBridge.RA_OK })
    }

    @Test
    fun signingInGivesTheToken() {
        signIn()
        assertEquals(listOf("Tester", "TOKEN"), NativeRa.user()!!.toList())
    }

    @Test
    fun aGamesSetLoadsAndUnlocksWhileItRuns() {
        gameWithSet()
        assertEquals(listOf(0, 5), NativeBridge.raSummary()!!.take(2))
        NativeBridge.start()
        var unlocked: RaEvent? = null
        assertTrue(waitFor(20000) {
            generateSequence { RaEvent.of(NativeBridge.raTakeEvent()) }.firstOrNull { it.kind == RaEvent.UNLOCKED }?.let { unlocked = it }
            unlocked != null
        })
        assertEquals("Welcome", unlocked!!.title)
        NativeBridge.pause()
        val list = RaAchievement.list(NativeBridge.raAchievements())
        assertEquals(5, list.size)
        assertTrue(list[0].unlocked)
    }

    @Test
    fun hardcoreRefusesStateLoadsAndCheatsAndSoftcoreAllowsThem() {
        gameWithSet()
        NativeBridge.raSetHardcore(true)
        assertTrue(NativeBridge.raHardcoreActive())
        val state = java.io.File(androidx.test.platform.app.InstrumentationRegistry.getInstrumentation().targetContext.cacheDir, "ra.state")
        assertTrue(NativeBridge.saveState(state.path)) // saving is allowed
        assertEquals(NativeBridge.ERR_STATE, NativeBridge.loadState(state.path))
        assertEquals(NativeBridge.ERR_CHEAT, NativeBridge.setCheats(arrayOf("0203FFF0:12345678"), intArrayOf(NativeBridge.CHEAT_RAW)))
        NativeBridge.start()
        Thread.sleep(200)
        NativeBridge.pause()
        assertFalse(NativeBridge.debugPeek32(0x0203FFF0) == 0x12345678)
        NativeBridge.raSetHardcore(false)
        assertFalse(NativeBridge.raHardcoreActive())
        assertEquals(0, NativeBridge.loadState(state.path))
    }

    @Test
    fun signedOutThereAreNoLimits() {
        assertEquals(0, NativeBridge.loadRom(TestRoms.rom("tonc/m7_demo.gba")))
        assertFalse(NativeBridge.raHardcoreActive())
        assertNotNull(NativeBridge.raUserAgentClause())
        assertEquals(0, NativeBridge.raCanPause())
    }
}
```

- [x] **Step 6: Run the on-device tests**

Run: `bash tools/dev.sh androidtest` → all pass (the new class takes ~10–20 s for the unlock at 300 frames).

- [x] **Step 7: Docs and commit**

```bash
git add app/src/main/java/com/tengen/gbaemu/Achievements.kt app/src/main/java/com/tengen/gbaemu/RaPopups.kt app/src/main/java/com/tengen/gbaemu/FakeRaServer.kt app/src/test app/src/androidTest docs
git commit -m "feat(app): achievements controller, pop-up queue, pretend server; on-device tests with it

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

## Task 9: Settings → RetroAchievements (impeccable)

**Files:**
- Modify: `Settings.kt`, `SettingsScreens.kt`, `MainActivity.kt`
- Create: `app/src/main/java/com/tengen/gbaemu/FakeBadges.kt`, `BadgeCache.kt`
- Test: `SettingsTest.kt`, `app/src/test/java/com/tengen/gbaemu/BadgeCacheTest.kt`

- [x] **Step 1: Write the failing tests**

`SettingsTest`: in `nothingStoredMeansTheDefaultsWhichAreTodaysBehaviour` add `assertEquals(true, s.hardcore)`; in `everySettingIsSavedAndReadBack` add `hardcore = false` to the `Settings(...)`.

`BadgeCacheTest.kt`:

```kotlin
package com.tengen.gbaemu

import java.io.File
import java.nio.file.Files
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class BadgeCacheTest {
    private val dir: File = Files.createTempDirectory("badges").toFile()

    @Test
    fun aBadgeIsDownloadedOnceAndKept() {
        var fetched = 0
        val cache = BadgeCache(dir) { fetched++; byteArrayOf(1, 2, 3) }
        val url = "https://media.retroachievements.org/Badge/01001.png"
        assertArrayEquals(byteArrayOf(1, 2, 3), cache.bytes(url))
        assertArrayEquals(byteArrayOf(1, 2, 3), cache.bytes(url))
        assertEquals(1, fetched)
        assertArrayEquals(byteArrayOf(1, 2, 3), File(dir, "01001.png").readBytes())
    }

    @Test
    fun oddNamesAndFailedDownloadsGiveNoPicture() {
        val cache = BadgeCache(dir) { null }
        assertNull(cache.bytes("https://media.retroachievements.org/Badge/01001_lock.png"))
        assertNull(BadgeCache(dir) { byteArrayOf(1) }.bytes("https://x/../secret"))
        assertNull(BadgeCache(dir) { byteArrayOf(1) }.bytes(""))
        assertEquals(0, dir.listFiles()!!.size)
    }
}
```

- [x] **Step 2: Run them to see them fail**

Run: `bash tools/dev.sh jvmtest` → compile errors (`hardcore`, `BadgeCache`).

- [x] **Step 3: The setting and the badge cache**

`Settings.kt`: field `val hardcore: Boolean = true, // RetroAchievements' hardcore mode (14): its limits apply only in games with a set`; `save`: `store.put("ra_hardcore", flag(hardcore))`; `load`: `hardcore = bool(store, "ra_hardcore", d.hardcore),`.

`app/src/main/java/com/tengen/gbaemu/BadgeCache.kt`:

```kotlin
package com.tengen.gbaemu

import java.io.File

/**
 * RetroAchievements' badge pictures kept on the phone (14) in `files/ra/badges/` (left out of backups: they download
 * again), by the name at the end of their URL (e.g. `01001.png`, `01001_lock.png`). [fetch] downloads one (null: failed).
 */
class BadgeCache(private val dir: File, private val fetch: (String) -> ByteArray?) {
    private fun fileOf(url: String): File? =
        url.substringAfterLast('/').substringBefore('?').takeIf(NAME::matches)?.let { File(dir, it) }

    /** The picture's bytes, from the folder or downloaded and kept; null without one. Slow: not on the UI thread. */
    fun bytes(url: String): ByteArray? {
        val f = fileOf(url) ?: return null
        if (f.isFile) return try { f.readBytes() } catch (e: Exception) { null }
        val b = fetch(url) ?: return null
        try {
            dir.mkdirs()
            val tmp = File(dir, f.name + ".tmp")
            tmp.writeBytes(b)
            if (!tmp.renameTo(f)) tmp.delete()
        } catch (e: Exception) {
        }
        return b
    }

    companion object {
        private val NAME = Regex("[A-Za-z0-9_-]{1,64}\\.png")
    }
}
```

`app/src/main/java/com/tengen/gbaemu/FakeBadges.kt`:

```kotlin
package com.tengen.gbaemu

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import java.io.ByteArrayOutputStream

/** Badge pictures for the pretend server (14): a coloured square with the badge's number; grey for a locked one. */
object FakeBadges {
    fun png(url: String): ByteArray {
        val name = url.substringAfterLast('/').substringBefore(".png")
        val locked = name.endsWith("_lock")
        val number = name.removeSuffix("_lock").trimStart('0').ifEmpty { "0" }
        val bmp = Bitmap.createBitmap(64, 64, Bitmap.Config.ARGB_8888)
        val c = Canvas(bmp)
        val hue = (number.hashCode() and 0xFF) * 360f / 256f
        c.drawColor(if (locked) Color.rgb(110, 110, 110) else Color.HSVToColor(floatArrayOf(hue, 0.6f, 0.85f)))
        val p = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.WHITE; textSize = 28f; textAlign = Paint.Align.CENTER }
        c.drawText(number, 32f, 42f, p)
        return ByteArrayOutputStream().also { bmp.compress(Bitmap.CompressFormat.PNG, 100, it) }.toByteArray()
    }
}
```

- [x] **Step 4: Shape the page with impeccable**

Invoke the **impeccable** skill for the new Settings page (craft on the existing surface: `SettingsScreens.kt`, keeping to `DESIGN.md` / `PRODUCT.md`: the grouped cartridge-plastic panels, the phone's fonts, the skin's accent, light/dark). Brief: a sign-in form (username, password, Sign in; the server's message on a failure; a busy state), the signed-in state (the name, Sign out), "Sign in again" after an expired sign-in, and the Hardcore switch with its one-line help. Start from the code below; take the skill's changes where they fit the design system. No image generation on this machine: code-led.

`SettingsScreens.kt`:
- `enum class SettingsPage`: add `ACHIEVEMENTS("RetroAchievements")` (its `up` is MAIN by the `else`).
- Above `SettingsScreen`:

```kotlin
/** The RetroAchievements sign-in as the Settings screen shows it (14). */
data class RaUi(val signIn: Achievements.SignIn = Achievements.SignIn.OUT, val name: String? = null, val error: String? = null)

/** The main page's line for RetroAchievements. */
fun raSummary(ra: RaUi, hardcore: Boolean): String = when (ra.signIn) {
    Achievements.SignIn.IN -> "Signed in as ${ra.name}" + if (hardcore) " · Hardcore" else ""
    Achievements.SignIn.BUSY -> "Signing in"
    Achievements.SignIn.AGAIN -> "Sign in again"
    Achievements.SignIn.OUT -> "Not signed in"
}
```

- `SettingsScreen` parameters: add `ra: RaUi = RaUi(), onRaSignIn: (String, String) -> Unit = { _, _ -> }, onRaSignOut: () -> Unit = {}`.
- MAIN page: after the existing first `Group { … }` (Picture … Backup), add:

```kotlin
                        Group {
                            NavRow(Icons.Filled.EmojiEvents, "RetroAchievements", raSummary(ra, s.hardcore), last = true) {
                                onPage(SettingsPage.ACHIEVEMENTS)
                            }
                        }
```

- The page: `SettingsPage.ACHIEVEMENTS -> RaPage(ra, s, onChange, onRaSignIn, onRaSignOut)` and, below `SettingsScreen`:

```kotlin
/** Settings → RetroAchievements (14): sign in or out, and the Hardcore switch. */
@Composable
private fun RaPage(ra: RaUi, s: Settings, onChange: (Settings) -> Unit, onSignIn: (String, String) -> Unit, onSignOut: () -> Unit) {
    Spacer(Modifier.height(8.dp))
    if (ra.signIn == Achievements.SignIn.IN) {
        Group {
            Row(Modifier.fillMaxWidth().padding(16.dp), verticalAlignment = Alignment.CenterVertically) {
                Text("Signed in as", style = MaterialTheme.typography.bodyLarge, modifier = Modifier.weight(1f))
                Text(ra.name.orEmpty(), color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
        }
        Spacer(Modifier.height(8.dp))
        OutlinedButton(onClick = onSignOut, modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp)) { Text("Sign out") }
        ra.error?.let { Help(it) }
    } else {
        var user by remember(ra.name) { mutableStateOf(ra.name.orEmpty()) }
        var password by remember { mutableStateOf("") }
        val busy = ra.signIn == Achievements.SignIn.BUSY
        val canSend = !busy && user.isNotBlank() && password.isNotEmpty()
        fun send() {
            if (!canSend) return
            onSignIn(user.trim(), password)
            password = ""
        }
        if (ra.signIn == Achievements.SignIn.AGAIN) {
            Text("Your sign-in stopped working. Sign in again.", color = MaterialTheme.colorScheme.error,
                modifier = Modifier.padding(vertical = 8.dp))
        }
        OutlinedTextField(user, { user = it }, label = { Text("Username") }, singleLine = true, enabled = !busy,
            keyboardOptions = KeyboardOptions(autoCorrectEnabled = false, keyboardType = KeyboardType.Ascii, imeAction = ImeAction.Next),
            modifier = Modifier.fillMaxWidth())
        Spacer(Modifier.height(8.dp))
        OutlinedTextField(password, { password = it }, label = { Text("Password") }, singleLine = true, enabled = !busy,
            visualTransformation = PasswordVisualTransformation(),
            keyboardOptions = KeyboardOptions(autoCorrectEnabled = false, keyboardType = KeyboardType.Password, imeAction = ImeAction.Done),
            keyboardActions = KeyboardActions(onDone = { send() }), modifier = Modifier.fillMaxWidth())
        ra.error?.let {
            Text(it, color = MaterialTheme.colorScheme.error, style = MaterialTheme.typography.bodySmall, modifier = Modifier.padding(top = 6.dp))
        }
        Spacer(Modifier.height(12.dp))
        Button(onClick = ::send, enabled = canSend, modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp)) {
            if (busy) {
                CircularProgressIndicator(Modifier.size(18.dp), strokeWidth = 2.dp, color = LocalContentColor.current)
                Spacer(Modifier.width(8.dp))
                Text("Signing in")
            } else Text("Sign in")
        }
        Help("Your retroachievements.org account. The password goes to RetroAchievements once and is never kept on this phone. Uses the internet.")
    }
    Spacer(Modifier.height(12.dp))
    Group {
        SwitchRow("Hardcore mode", s.hardcore, { onChange(s.copy(hardcore = it)) },
            help = "Earn hardcore points. In games with achievements, Load state, rewind and cheats are off.")
    }
}
```

(imports: `androidx.compose.foundation.text.KeyboardActions`, `KeyboardOptions`, `androidx.compose.ui.text.input.ImeAction`, `KeyboardType`, `PasswordVisualTransformation`, `androidx.compose.material.icons.filled.EmojiEvents`, `androidx.compose.material3.OutlinedTextField`, `CircularProgressIndicator`, `LocalContentColor`, and whatever the file lacks.)

- [x] **Step 5: Wire it in MainActivity**

Fields (with the others):

```kotlin
    private lateinit var ra: Achievements
    private lateinit var badges: BadgeCache
    private var raUi by mutableStateOf(RaUi())
```

In `onCreate`, after `applySettings(settings)`:

```kotlin
        // RetroAchievements (14): the internet (or, in a debug build with files/ra-fake, the pretend server) and the saved sign-in.
        val debuggable = applicationInfo.flags and android.content.pm.ApplicationInfo.FLAG_DEBUGGABLE != 0
        val fake = debuggable && File(filesDir, "ra-fake").exists()
        val userAgent = RaNet.userAgent(android.os.Build.VERSION.RELEASE, NativeBridge.raUserAgentClause())
        RaService.worker.send = if (fake) FakeRaServer::reply else { r -> RaNet.send(r, userAgent) }
        RaService.worker.start()
        badges = BadgeCache(File(filesDir, "ra/badges"),
            if (fake) FakeBadges::png else { url -> (Net.get(url, 512 * 1024) as? Net.Got)?.bytes })
        ra = Achievements(NativeRa, RaAccount(File(filesDir, "ra.json")))
        ra.start()
        watchSignIn()
```

`applySettings`: add `NativeBridge.raSetHardcore(s.hardcore)`.

Helpers:

```kotlin
    private fun refreshRaUi() {
        raUi = RaUi(ra.signIn, ra.name, ra.error)
    }

    /** While a sign-in is on its way: picks up the answer (every 200 ms). */
    private fun watchSignIn() {
        refreshRaUi()
        if (ra.signIn != Achievements.SignIn.BUSY) return
        handler.postDelayed({
            ra.update()
            watchSignIn()
        }, 200)
    }
```

`SettingsScreen(...)` call: add `ra = raUi, onRaSignIn = { u, p -> ra.signIn(u, p); watchSignIn() }, onRaSignOut = { ra.signOut(); refreshRaUi() }`.

- [x] **Step 6: Run the tests and look at the page**

Run: `bash tools/dev.sh jvmtest` → pass. `bash tools/dev.sh install`, then make the pretend server the app's: `MSYS_NO_PATHCONV=1 "$LOCALAPPDATA/Android/Sdk/platform-tools/adb.exe" shell run-as com.tengen.gbaemu touch files/ra-fake`, force-stop and `bash tools/dev.sh launch`. Settings → RetroAchievements: sign in with the pretend server's test account (`Tester` / `secret`, a value of this project's pretend server, nothing leaves the phone) → "Signed in as Tester"; a wrong password → the server's message; Sign out. Screenshots (light, dark) into `.impeccable/review/` for the impeccable pass.

- [x] **Step 7: Docs and commit**

```bash
git add app/src/main/java/com/tengen/gbaemu app/src/test docs
git commit -m "feat(app): Settings - RetroAchievements sign-in and the Hardcore switch

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

## Task 10: Opening a game — identify first, Continue, the hardcore limits in the app

**Files:**
- Modify: `MainActivity.kt`, `Screens.kt` (`GameMenu`, `MenuTiles`, `HardcoreRestartDialog`), `AppTheme.kt` (`MenuRow`)
- Checked by: the JVM and on-device suites staying green, and the emulator walk-through in Step 4 (the activity's flow has no unit seam; its decisions — `Achievements.gameStep`, the native limits — are tested in Tasks 4, 6 and 8)

- [x] **Step 1: A greyed-out menu row**

`AppTheme.kt` — `MenuRow` gets `enabled` and a `note`:

```kotlin
/** A flat row of the pause menu: an icon and its label (no card inside the panel — the menu stays sparse). Off: faded,
 * with [note] saying why. */
@Composable
fun MenuRow(icon: ImageVector, label: String, onClick: () -> Unit, enabled: Boolean = true, note: String? = null) {
    val fade = Modifier.alpha(if (enabled) 1f else 0.38f)
    Row(Modifier.fillMaxWidth().clickable(enabled = enabled, onClick = onClick).heightIn(min = 52.dp).padding(horizontal = 4.dp, vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically) {
        Icon(icon, contentDescription = null, tint = MaterialTheme.colorScheme.onSurfaceVariant, modifier = Modifier.size(24.dp).then(fade))
        Spacer(Modifier.width(16.dp))
        Column {
            Text(label, style = MaterialTheme.typography.bodyLarge, fontWeight = FontWeight.Medium, modifier = fade)
            if (note != null) Text(note, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
    }
}
```

`Screens.kt` — `GameMenu` gets `locked: Boolean = false, onAchievements: (() -> Unit)? = null` (passed to both `MenuTiles` calls), and `MenuTiles`:

```kotlin
@Composable
private fun MenuTiles(onSave: () -> Unit, onLoad: () -> Unit, onCheats: () -> Unit, onSettings: () -> Unit,
                      locked: Boolean = false, onAchievements: (() -> Unit)? = null) {
    val off = "Off in hardcore mode"
    Column {
        MenuRow(Icons.Filled.Save, "Save state", onSave)
        HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
        MenuRow(Icons.Filled.FolderOpen, "Load state", onLoad, enabled = !locked, note = if (locked) off else null)
        HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
        if (onAchievements != null) {
            MenuRow(Icons.Filled.EmojiEvents, "Achievements", onAchievements)
            HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
        }
        MenuRow(Icons.Filled.AutoFixHigh, "Cheats", onCheats, enabled = !locked, note = if (locked) off else null)
        HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
        MenuRow(Icons.Filled.Settings, "Settings", onSettings)
    }
}
```

- [x] **Step 2: Identify the game before it starts**

`MainActivity.kt` fields:

```kotlin
    private val raPopups = RaPopups()
    private var raView by mutableStateOf(RaPopups.View())
    private var raSet by mutableStateOf(false)       // the game being played has achievements
    private var hardcoreOn by mutableStateOf(false)  // and the hardcore limits apply to it
    private var askHardcoreRestart by mutableStateOf(false)
```

In `open(...)`, replace everything from `if (resumeAuto && st.used(States.AUTO)) {` to the end of the function with:

```kotlin
        raPopups.clear()
        raView = raPopups.view()
        raSet = false
        hardcoreOn = false
        if (ra.loadGame(rom)) waitForAchievements { game -> useAchievements(game); continueOpen(st, resumeAuto) }
        else continueOpen(st, resumeAuto)
    }

    /** The rest of opening a game, once RetroAchievements has answered (or there was nothing to ask). */
    private fun continueOpen(st: States, resumeAuto: Boolean) {
        if (resumeAuto && st.used(States.AUTO)) {
            if (hardcoreOn) {
                raPopups.note("Hardcore: started from your in-game save", SystemClock.uptimeMillis())
            } else {
                if (st.load(States.AUTO)) {
                    startGame()
                    return
                }
                toast("Couldn't resume where you left off; starting the game.")
            }
        }
        when (NativeBridge.saveStatus()) {
            NativeBridge.SAVE_EMPTY -> {
                importError = null
                askImport = true
            }
            NativeBridge.SAVE_MISFIT -> {
                startGame()
                toast("The save file for this game doesn't fit it and was not loaded.")
            }
            else -> startGame()
        }
    }

    /** Waits (on the UI thread, 50 ms at a time) for RetroAchievements to identify the game; a spinner after 0.3 s, up to 10 s. */
    private fun waitForAchievements(then: (Achievements.Game) -> Unit) {
        val start = SystemClock.uptimeMillis()
        val step = object : Runnable {
            override fun run() {
                val waited = SystemClock.uptimeMillis() - start
                val game = ra.gameStep(timedOut = waited >= RA_WAIT_MS)
                if (game == null) {
                    if (waited >= 300 && busy == null) busy = CHECKING
                    handler.postDelayed(this, 50)
                    return
                }
                if (busy == CHECKING) busy = null
                refreshRaUi()
                then(game)
            }
        }
        step.run()
    }

    private fun useAchievements(game: Achievements.Game) {
        val now = SystemClock.uptimeMillis()
        when (game) {
            is Achievements.Game.Set -> {
                raSet = true
                hardcoreOn = NativeBridge.raHardcoreActive()
                raPopups.note("${game.unlocked} of ${game.total} achievements" + if (hardcoreOn) " · Hardcore" else "", now)
            }
            Achievements.Game.Offline -> raPopups.note("RetroAchievements: offline", now)
            Achievements.Game.NoSet -> {}
        }
        raView = raPopups.view()
    }
```

with `private const val RA_WAIT_MS = 10_000L` and `private const val CHECKING = "Checking RetroAchievements"` in a `companion object` (or top-level private vals).

(`rom` is the direct `ByteBuffer` already made in `open`; `raLoadGame` hashes it before returning.)

- [x] **Step 3: The limits in the app**

- `sendCheats()`: first line `if (hardcoreOn) { NativeBridge.setCheats(arrayOf(), intArrayOf()); return }`.
- `sendKeys(...)`: `NativeBridge.setRewinding(!hardcoreOn && all and KeyMap.REWIND != 0)`.
- `quickLoad()`: first line `if (hardcoreOn) return toast("Off in hardcore mode")`.
- `openMenu()`:

```kotlin
    private fun openMenu() {
        if (hardcoreOn && NativeBridge.raCanPause() > 0) { // hardcore: no quick repeated pauses
            raPopups.note("Wait a few seconds to pause again in hardcore mode", SystemClock.uptimeMillis())
            raView = raPopups.view()
            return
        }
        stopExtras()
        NativeBridge.pause()
        slotsChanged++
        menu = MenuPage.MAIN
    }
```

- `stopGame()`: add `raPopups.clear(); raView = raPopups.view(); raSet = false; hardcoreOn = false`.
- `GameMenu(...)` call: add `locked = hardcoreOn, onAchievements = if (raSet) ({ menu = MenuPage.ACHIEVEMENTS }) else null` (`MenuPage.ACHIEVEMENTS` and its panel come in Task 11; until then add the enum value and `MenuPage.ACHIEVEMENTS -> {}` in the `when`).
- Switching hardcore while playing — `changeSettings(s)`, first lines:

```kotlin
        if (s.hardcore != settings.hardcore) {
            if (s.hardcore && playing && raSet && ra.signIn == Achievements.SignIn.IN) {
                askHardcoreRestart = true // the switch stays off unless the user says yes
                return
            }
            if (!s.hardcore) hardcoreOn = false
        }
```

and the question, beside the `RestartDialog` line in `setContent`:

```kotlin
                if (askHardcoreRestart) HardcoreRestartDialog(
                    onRestart = {
                        askHardcoreRestart = false
                        settings = settings.copy(hardcore = true)
                        settings.save(settingsStore)
                        applySettings(settings)
                        settingsPage = null
                        restartGame()
                    },
                    onCancel = { askHardcoreRestart = false })
```

`Screens.kt`:

```kotlin
/** Hardcore switched on in a game with achievements (14): it only counts from a fresh start. */
@Composable
fun HardcoreRestartDialog(onRestart: () -> Unit, onCancel: () -> Unit) {
    AlertDialog(
        onDismissRequest = onCancel,
        title = { Text("Restart the game?") },
        text = { Text("Hardcore mode starts from the title screen. Anything not saved in the game is lost.") },
        confirmButton = { DangerButton("Restart", onRestart) },
        dismissButton = { TextButton(onClick = onCancel) { Text("Cancel") } },
    )
}
```

- Sign out while playing (`onRaSignOut`): also `hardcoreOn = false; raSet = false`.

- [x] **Step 4: Build and check**

Run: `bash tools/dev.sh jvmtest`, `bash tools/dev.sh apk`, `bash tools/dev.sh androidtest` → all pass (no game test signs in, so nothing changes for them). On the emulator with `files/ra-fake`: signed in, open m7_demo → "0 of 5 achievements · Hardcore", pause menu: Load state and Cheats greyed with "Off in hardcore mode"; quit and Continue → starts fresh with the hardcore note; Settings → Hardcore off → Load state works; on again in the game → "Restart the game?".

- [x] **Step 5: Docs and commit**

```bash
git add app/src/main/java/com/tengen/gbaemu docs
git commit -m "feat(app): games are identified before they start; Continue, menu, rewind, cheats follow hardcore

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

## Task 11: Pop-ups, badge pictures and the Achievements list (impeccable)

**Files:**
- Create: `app/src/main/java/com/tengen/gbaemu/AchievementScreens.kt`
- Modify: `Screens.kt` (`GameScreen` overlay slot; `MenuLayer` → `internal`; `Tabular` → `internal`), `MainActivity.kt`

- [x] **Step 1: Shape the pieces with impeccable**

Invoke the **impeccable** skill for the in-game layer and the panel (keeping to `DESIGN.md`: Game Pak plastic, the skin's accent, the phone's fonts, light/dark; the pop-ups must not cover the touch buttons and must read over any game picture; upright and sideways). Brief: an unlock card (badge, "Achievement unlocked", title, description, points), a "Mastered" card, problem / note cards, a small progress pill ("3/10" with the badge), challenge badges, and the pause menu's Achievements panel (summary line + bar, rows with badge, title, description, progress, points; locked rows quieter). Start from the code below.

- [x] **Step 2: The composables**

`Screens.kt`: change `private fun MenuLayer` to `internal fun MenuLayer` and `private val Tabular` to `internal val Tabular`; `GameScreen` gets the last parameter `overlay: @Composable BoxScope.() -> Unit = {}` and calls `overlay()` as the last line inside its `BoxWithConstraints`.

`app/src/main/java/com/tengen/gbaemu/AchievementScreens.kt`:

```kotlin
package com.tengen.gbaemu

import android.graphics.BitmapFactory
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutVertically
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.EmojiEvents
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.runtime.produceState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/** A badge picture from [badges] (downloaded off the UI thread the first time); null while there is none. */
@Composable
fun rememberBadge(url: String, badges: BadgeCache): ImageBitmap? = produceState<ImageBitmap?>(null, url) {
    value = if (url.isEmpty()) null else withContext(Dispatchers.IO) {
        badges.bytes(url)?.let { BitmapFactory.decodeByteArray(it, 0, it.size)?.asImageBitmap() }
    }
}.value

/** A badge: its picture, or a trophy on plain plastic while there is none. */
@Composable
fun Badge(url: String, badges: BadgeCache, size: Dp) {
    val picture = rememberBadge(url, badges)
    Box(Modifier.size(size).clip(RoundedCornerShape(6.dp)).background(MaterialTheme.colorScheme.surfaceVariant),
        contentAlignment = Alignment.Center) {
        if (picture != null) Image(picture, contentDescription = null, modifier = Modifier.fillMaxSize(), contentScale = ContentScale.Crop)
        else Icon(Icons.Filled.EmojiEvents, contentDescription = null, tint = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.size(size * 0.55f))
    }
}

/** RetroAchievements over the game (14): the card at the top, the progress pill and the challenge badges below it. */
@Composable
fun BoxScope.RaPopupLayer(view: RaPopups.View, badges: BadgeCache) {
    Column(Modifier.align(Alignment.TopCenter).statusBarsPadding().padding(top = 8.dp, start = 12.dp, end = 12.dp)
        .widthIn(max = 380.dp), horizontalAlignment = Alignment.CenterHorizontally) {
        AnimatedVisibility(view.card != null, enter = fadeIn() + slideInVertically { -it }, exit = fadeOut() + slideOutVertically { -it }) {
            view.card?.let { RaCard(it, badges) }
        }
        Row(Modifier.padding(top = 6.dp), horizontalArrangement = Arrangement.spacedBy(6.dp), verticalAlignment = Alignment.CenterVertically) {
            view.challenges.forEach { Badge(it.badgeUrl, badges, 32.dp) }
            view.progress?.let { p ->
                Row(Modifier.background(Scrim, RoundedCornerShape(50)).padding(start = 4.dp, end = 12.dp, top = 4.dp, bottom = 4.dp),
                    verticalAlignment = Alignment.CenterVertically) {
                    Badge(p.badgeUrl, badges, 24.dp)
                    Spacer(Modifier.width(8.dp))
                    Text(p.text, color = androidx.compose.ui.graphics.Color.White,
                        style = MaterialTheme.typography.labelLarge.merge(Tabular))
                }
            }
        }
    }
}

@Composable
private fun RaCard(card: RaPopups.Card, badges: BadgeCache) {
    val label = when (card.kind) {
        RaEvent.UNLOCKED -> "Achievement unlocked"
        RaEvent.MASTERED -> "Mastered"
        else -> null
    }
    Surface(shape = MaterialTheme.shapes.large, color = MaterialTheme.colorScheme.surfaceContainerHigh, shadowElevation = 6.dp,
        modifier = Modifier.fillMaxWidth()) {
        Row(Modifier.padding(12.dp), verticalAlignment = Alignment.CenterVertically) {
            if (card.badgeUrl.isNotEmpty()) {
                Badge(card.badgeUrl, badges, 48.dp)
                Spacer(Modifier.width(12.dp))
            }
            Column(Modifier.weight(1f)) {
                label?.let { Text(it, style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.primary) }
                Text(card.title, style = MaterialTheme.typography.titleSmall, fontWeight = FontWeight.SemiBold, maxLines = 2,
                    overflow = TextOverflow.Ellipsis)
                if (card.text.isNotEmpty()) Text(card.text, style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant, maxLines = 2, overflow = TextOverflow.Ellipsis)
            }
            if (card.points > 0) {
                Spacer(Modifier.width(8.dp))
                Text("${card.points}", style = MaterialTheme.typography.titleMedium.merge(Tabular), color = MaterialTheme.colorScheme.primary)
            }
        }
    }
}

/** Pause menu → Achievements (14): the summary and every achievement, unlocked first. [summary]: raSummary's numbers. */
@Composable
fun AchievementsPanel(game: String, summary: IntArray?, list: List<RaAchievement>, hardcore: Boolean, badges: BadgeCache,
                      onBack: () -> Unit) {
    MenuLayer {
        Column(Modifier.padding(bottom = 16.dp).widthIn(min = 300.dp)) {
            ScreenHeader("Achievements", onBack = onBack, subtitle = game)
            summary?.let { s ->
                Text("${s[0]} of ${s[1]} unlocked · ${s[2]} of ${s[3]} points" + if (hardcore) " · Hardcore" else "",
                    style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
                Spacer(Modifier.height(8.dp))
                LinearProgressIndicator(progress = { if (s[1] == 0) 0f else s[0].toFloat() / s[1] }, modifier = Modifier.fillMaxWidth())
                Spacer(Modifier.height(8.dp))
            }
            Column(Modifier.weight(1f, fill = false).heightIn(max = 420.dp).verticalScroll(rememberScrollState())) {
                list.forEachIndexed { i, a ->
                    AchievementRow(a, badges)
                    if (i < list.lastIndex) HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
                }
            }
        }
    }
}

@Composable
private fun AchievementRow(a: RaAchievement, badges: BadgeCache) {
    Row(Modifier.fillMaxWidth().padding(vertical = 10.dp), verticalAlignment = Alignment.CenterVertically) {
        Badge(a.badgeUrl, badges, 44.dp)
        Spacer(Modifier.width(12.dp))
        Column(Modifier.weight(1f)) {
            Text(a.title, style = MaterialTheme.typography.titleSmall, fontWeight = FontWeight.SemiBold,
                color = if (a.unlocked) MaterialTheme.colorScheme.onSurface else MaterialTheme.colorScheme.onSurfaceVariant)
            Text(a.description, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant,
                maxLines = 3, overflow = TextOverflow.Ellipsis)
            if (!a.unlocked && a.progress.isNotEmpty()) {
                Text(a.progress, style = MaterialTheme.typography.labelSmall.merge(Tabular), color = MaterialTheme.colorScheme.primary)
            }
        }
        Spacer(Modifier.width(8.dp))
        Text("${a.points}", style = MaterialTheme.typography.titleSmall.merge(Tabular),
            color = if (a.unlocked) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant)
    }
}
```

(`Scrim` is the existing colour used by `SpeedLabel`; make it `internal` if it is private.)

- [x] **Step 3: Wire them in MainActivity**

- `GameScreen(...)`: add `overlay = { RaPopupLayer(raView, badges) }`.
- The events, beside the save-error `LaunchedEffect` in the `playing` block:

```kotlin
                    LaunchedEffect(Unit) { // RetroAchievements' events and the pop-ups' timing (14)
                        while (true) {
                            val now = SystemClock.uptimeMillis()
                            var changed = false
                            while (true) {
                                val e = RaEvent.of(NativeBridge.raTakeEvent()) ?: break
                                raPopups.add(e, now)
                                changed = true
                            }
                            if (raPopups.tick(now) || changed) raView = raPopups.view()
                            delay(100)
                        }
                    }
```

- The panel: field `private var raList by mutableStateOf<Pair<IntArray?, List<RaAchievement>>>(null to emptyList())`; the menu's `onAchievements = if (raSet) ({ raList = NativeBridge.raSummary() to RaAchievement.list(NativeBridge.raAchievements()); menu = MenuPage.ACHIEVEMENTS }) else null`; in the `when (menu)`:

```kotlin
                    MenuPage.ACHIEVEMENTS -> AchievementsPanel(String(NativeBridge.raGameTitle(), Charsets.UTF_8).ifEmpty { currentTitle },
                        raList.first, raList.second, hardcoreOn, badges, onBack = { menu = MenuPage.MAIN })
```

(the `BackHandler` for `menu != MenuPage.MAIN` already goes back to the main menu.)

- [x] **Step 4: Build and look**

Run: `bash tools/dev.sh jvmtest`, `bash tools/dev.sh apk`, `bash tools/dev.sh androidtest` → all pass. On the emulator with `files/ra-fake`, signed in as Tester, play m7_demo for 45 s: the start note, "Welcome" at ~5 s, the progress pill to 20 s, the challenge badge until 30 s, "Couldn't send an unlock" at ~10 s, "Mastered" at ~40 s; the pause menu's Achievements panel with pictures. Screenshots upright and sideways, light and dark, into `.impeccable/review/`.

- [x] **Step 5: Docs and commit**

```bash
git add app/src/main/java/com/tengen/gbaemu docs
git commit -m "feat(app): achievement pop-ups, badge pictures and the pause menu's Achievements panel

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

## Task 12: Claude's emulator check and the finish review

**Files:**
- Modify: anything the check finds; `docs/PROGRESS.md`, `DESIGN.md` / `.impeccable/design.json` if the design system grew (impeccable documenter)

- [x] **Step 1: The whole suite**

Run: `bash tools/dev.sh test`, `bash tools/dev.sh unit`, `bash tools/dev.sh jvmtest`, `bash tools/dev.sh androidtest`, `bash tools/dev.sh games --check` (every locked game unchanged), `python tools/fetch_rcheevos.py --check`.
Expected: all pass; record the new totals (unit, CTest, Kotlin, on-device) in PROGRESS.md.

- [x] **Step 2: The check on the phone emulator, with the pretend server**

With `files/ra-fake` in place, go through and screenshot (into `.impeccable/review/`): Settings → RetroAchievements signed out / busy / wrong password / signed in / "Sign in again" (sign in, then `run-as … ` write `{"user": "Tester", "token": "EXPIRED"}` into `files/ra.json`, force-stop, launch); the game start note; each pop-up kind; the progress pill; the challenge badge; the pause menu in hardcore (greyed rows) and softcore; the Achievements panel; the "Restart the game?" question; Continue in hardcore; the pause limit note (open the menu twice quickly); a game without a set (`FakeRaServer.unknownHashes` cannot be set from outside — use signed out instead: no note, nothing greyed); airplane mode with the real server (`rm files/ra-fake`, `adb shell cmd connectivity airplane-mode enable`) → "RetroAchievements: offline" and the game starts after the wait. Each in light and dark, upright and sideways, and at font scale 1.3 (`adb shell settings put system font_scale 1.3`, back to 1.0 after).

- [x] **Step 3: The impeccable finish review**

Run the impeccable finish review on the new surfaces with the screenshots; fix what it lists until it says `ship`. If the design system grew (the pop-up card, the badge), let the impeccable documenter update `DESIGN.md` and `.impeccable/design.json`.

- [x] **Step 4: Clean up the emulator**

`run-as com.tengen.gbaemu rm files/ra-fake files/ra.json`, airplane mode off, font scale 1.0.

- [x] **Step 5: Docs and commit**

```bash
git add -A app docs DESIGN.md .impeccable/design.json
git commit -m "test(app): RetroAchievements emulator check with the pretend server; finish review ship

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

## Task 13: The user's try, then the milestone exit

- [x] **Step 1: Ask the user to try it (stop here)**

The app on the phone emulator uses the real server (no `files/ra-fake`). Tell the user, in layman mode:
1. Put a game that has a RetroAchievements set into the games folder (e.g. their `game-c.gba` or `game-d.gba`; push it with `& "C:\Program Files\Git\bin\bash.exe" tools/dev.sh push "<file>"` if it isn't on the emulator).
2. Settings → RetroAchievements → sign in with their own retroachievements.org account (Claude does not type it).
3. Open the game: the note "N of M achievements · Hardcore"; play until an easy achievement unlocks (pick one from the game's page on retroachievements.org), see the pop-up, and check the pause menu's Achievements list.
4. Say what they saw. (Until RetroAchievements approves the app, the website lists the unlock as softcore — expected.)

- [x] **Step 2: When the user's try passes — the milestone exit**

Update all docs (PROGRESS.md: milestone 14 ✅ with the user's result, status table, Next = whatever the user picks next or "no milestone planned — ask the user"; the spec's status line → implemented; the parent spec's milestone table and status line; CLAUDE.md "Key files": no current milestone, last finished 14), then:

```bash
git add -A docs CLAUDE.md
git commit -m "docs: milestone 14 RetroAchievements complete

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git tag milestone-14
```

Then ask the user before merging into `main` (superpowers:finishing-a-development-branch): `git checkout main && git merge --no-ff refs/heads/milestone-14` and `git branch -d milestone-14` — as was done for milestones 1–13.

- [x] **Step 3: The user's note for later**

In PROGRESS.md's Notes: "When the app goes public: ask RetroAchievements to approve the client (User-Agent `GBAEmulator/<version> (Android …) rcheevos/12.5`) so hardcore unlocks count as hardcore."
