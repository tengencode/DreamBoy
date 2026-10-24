#include "sys/sched.h"
#include "test.h"

TEST(sched_empty_has_no_events) {
    sched_t s;
    sched_init(&s);
    uint64_t when;
    CHECK_EQ(sched_next(&s), SCHED_NEVER);
    CHECK_EQ(sched_pop_due(&s, 1000, &when), -1);
}

TEST(sched_pops_in_time_order) {
    sched_t s;
    sched_init(&s);
    uint64_t when;
    sched_add(&s, EV_LINE_END, 100);
    sched_add(&s, EV_HBLANK, 50);
    CHECK_EQ(sched_next(&s), 50);
    CHECK_EQ(sched_pop_due(&s, 40, &when), -1);
    CHECK_EQ(sched_pop_due(&s, 200, &when), EV_HBLANK);
    CHECK_EQ(when, 50);
    CHECK_EQ(sched_pop_due(&s, 200, &when), EV_LINE_END);
    CHECK_EQ(when, 100);
    CHECK_EQ(sched_pop_due(&s, 200, &when), -1);
}

TEST(sched_ties_fire_lower_id_first) {
    sched_t s;
    sched_init(&s);
    uint64_t when;
    sched_add(&s, EV_LINE_END, 10);
    sched_add(&s, EV_HBLANK, 10);
    CHECK_EQ(sched_pop_due(&s, 10, &when), EV_HBLANK);
    CHECK_EQ(sched_pop_due(&s, 10, &when), EV_LINE_END);
}

TEST(sched_add_replaces_and_cancel_removes) {
    sched_t s;
    sched_init(&s);
    sched_add(&s, EV_HBLANK, 10);
    sched_add(&s, EV_HBLANK, 30);
    CHECK_EQ(sched_next(&s), 30);
    sched_cancel(&s, EV_HBLANK);
    CHECK_EQ(sched_next(&s), SCHED_NEVER);
}
