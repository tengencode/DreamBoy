/* The rewind packer (core_common/delta_pack.c): unpacking gives back exactly the other snapshot. */
#include <stdlib.h>
#include <string.h>

#include "delta_pack.h"
#include "gba_debug.h"
#include "test.h"
#include "testutil.h"

static uint32_t rng = 12345;
static uint32_t next_random(void) {
    rng = rng * 1103515245u + 12345u;
    return rng >> 8;
}

/* Packs a→b, checks the size bound, unpacks into a copy of a and expects b; returns the packed size. */
static size_t round_trip(const uint8_t *a, const uint8_t *b, size_t n) {
    uint8_t *packed = malloc(delta_pack_bound(n));
    uint8_t *buf = malloc(n ? n : 1);
    size_t len = delta_pack(a, b, n, packed);
    CHECK(len <= delta_pack_bound(n));
    memcpy(buf, a, n);
    CHECK(delta_unpack(packed, len, buf, n));
    CHECK(memcmp(buf, b, n) == 0);
    CHECK(delta_unpack(packed, len, buf, n)); /* and back again */
    CHECK(memcmp(buf, a, n) == 0);
    free(packed);
    free(buf);
    return len;
}

TEST(delta_pack_unchanged_bytes_take_almost_nothing) {
    static uint8_t a[100000];
    for (size_t i = 0; i < sizeof a; i++) a[i] = (uint8_t)(i * 13);
    CHECK(round_trip(a, a, sizeof a) <= 4);
}

TEST(delta_pack_every_byte_changed_stays_within_the_bound) {
    enum { N = 5000 };
    uint8_t *a = malloc(N), *b = malloc(N);
    for (size_t i = 0; i < N; i++) {
        a[i] = (uint8_t)i;
        b[i] = (uint8_t)~i;
    }
    size_t len = round_trip(a, b, N);
    CHECK(len <= N + 8);
    free(a);
    free(b);
}

TEST(delta_pack_random_changes_and_odd_sizes) {
    for (size_t n = 0; n < 200; n += 7) {
        for (int pattern = 0; pattern < 4; pattern++) {
            uint8_t *a = malloc(n + 1), *b = malloc(n + 1);
            for (size_t i = 0; i < n; i++) a[i] = b[i] = (uint8_t)next_random();
            for (size_t i = 0; i < n; i++) {
                int change = pattern == 0 ? next_random() % 50 == 0   /* rare single bytes */
                             : pattern == 1 ? (i / 5) % 3 == 1         /* short runs, short gaps */
                             : pattern == 2 ? (i / 9) % 2 == 0         /* runs and gaps around the cut-off */
                                            : next_random() % 2 == 0;  /* half of everything */
                if (change) b[i] ^= (uint8_t)(1 + next_random() % 255);
            }
            round_trip(a, b, n);
            free(a);
            free(b);
        }
    }
}

TEST(delta_pack_two_real_snapshots_pack_small) {
    static const uint32_t ops[] = {0xE3A01402, 0xE2800001, 0xE5810000, 0xEAFFFFFC}; /* count into EWRAM */
    gba_t *g = tu_arm(ops, TU_LEN(ops));
    size_t n = gba_state_size(g);
    uint8_t *a = malloc(n), *b = malloc(n);
    gba_run_frame(g);
    gba_state_save(g, a, n);
    for (int i = 0; i < 4; i++) gba_run_frame(g);
    gba_state_save(g, b, n);
    CHECK(round_trip(a, b, n) < n / 20);
    free(a);
    free(b);
    gba_destroy(g);
}

TEST(delta_pack_broken_input_is_refused) {
    enum { N = 64 };
    uint8_t a[N], b[N], packed[2 * N + 2], buf[N + 1];
    for (size_t i = 0; i < N; i++) {
        a[i] = (uint8_t)i;
        b[i] = (uint8_t)(i % 3 ? i : ~i);
    }
    size_t len = delta_pack(a, b, N, packed);
    memcpy(buf, a, N);
    CHECK(!delta_unpack(packed, len - 1, buf, N)); /* cut short */
    memcpy(buf, a, N);
    CHECK(!delta_unpack(packed, len, buf, N - 1)); /* describes more bytes than there are */
    memcpy(buf, a, N);
    buf[N] = 0;
    CHECK(!delta_unpack(packed, len, buf, N + 1)); /* describes fewer */
    memcpy(buf, a, N);
    CHECK(delta_unpack(packed, len, buf, N)); /* the right size is fine */
}
