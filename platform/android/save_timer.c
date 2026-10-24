#include "save_timer.h"

void save_timer_init(save_timer_t *t) {
    t->countdown = 0;
}

bool save_timer_frame(save_timer_t *t, bool dirty) {
    if (dirty) {
        t->countdown = SAVE_TIMER_FRAMES;
        return false;
    }
    return t->countdown > 0 && --t->countdown == 0;
}

bool save_timer_flush(save_timer_t *t) {
    bool pending = t->countdown > 0;
    t->countdown = 0;
    return pending;
}
