#include "triple_buffer.h"

#include <string.h>

void tb_init(triple_buffer_t *tb) {
    memset(tb->slot, 0, sizeof tb->slot);
    tb->back = 0;
    atomic_init(&tb->middle, 1u);
    tb->front = 2;
}

uint16_t *tb_back(triple_buffer_t *tb) {
    return tb->slot[tb->back];
}

void tb_publish(triple_buffer_t *tb) {
    unsigned old = atomic_exchange(&tb->middle, tb->back | TB_FRESH);
    tb->back = old & ~TB_FRESH;
}

const uint16_t *tb_take(triple_buffer_t *tb, bool *fresh) {
    *fresh = (atomic_load(&tb->middle) & TB_FRESH) != 0;
    if (*fresh) tb->front = atomic_exchange(&tb->middle, tb->front) & ~TB_FRESH;
    return tb->slot[tb->front];
}
