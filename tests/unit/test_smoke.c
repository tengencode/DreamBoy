#include "gba.h"
#include "test.h"

TEST(gba_create_and_destroy) {
    gba_t *g = gba_create(NULL);
    CHECK(g != NULL);
    gba_destroy(g);
}
