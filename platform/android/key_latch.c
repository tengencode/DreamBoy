#include "key_latch.h"

void key_latch_init(key_latch_t *k) {
    atomic_init(&k->held, 0u);
    atomic_init(&k->pressed, 0u);
}

void key_latch_set(key_latch_t *k, unsigned held) {
    unsigned before = atomic_exchange(&k->held, held);
    atomic_fetch_or(&k->pressed, held & ~before);
}

unsigned key_latch_take(key_latch_t *k) {
    return atomic_load(&k->held) | atomic_exchange(&k->pressed, 0u);
}
