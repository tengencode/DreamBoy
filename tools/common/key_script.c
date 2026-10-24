#include "key_script.h"

#include <stdlib.h>
#include <string.h>

#include "gba.h"

bool key_script_add(key_script_t *ks, long frame, uint16_t keys) {
    if (ks->count == ks->cap) {
        int cap = ks->cap ? ks->cap * 2 : 64;
        key_event_t *ev = realloc(ks->ev, sizeof *ev * (size_t)cap);
        if (!ev) return false;
        ks->ev = ev;
        ks->cap = cap;
    }
    ks->ev[ks->count++] = (key_event_t){frame, keys};
    return true;
}

bool key_script_parse(const char *list, key_script_t *ks) {
    static const struct {
        const char *name;
        uint16_t key;
    } names[] = {{"A", GBA_KEY_A},         {"B", GBA_KEY_B},         {"SELECT", GBA_KEY_SELECT},
                 {"START", GBA_KEY_START}, {"RIGHT", GBA_KEY_RIGHT}, {"LEFT", GBA_KEY_LEFT},
                 {"UP", GBA_KEY_UP},       {"DOWN", GBA_KEY_DOWN},   {"R", GBA_KEY_R},
                 {"L", GBA_KEY_L},         {"CW", KEY_CW},           {"CCW", KEY_CCW}};
    const size_t name_count = sizeof names / sizeof names[0];
    const char *p = list;
    ks->count = 0;
    while (*p) {
        char *end;
        long frame = strtol(p, &end, 10);
        if (end == p || *end != ':' || frame < 0) return false;
        if (ks->count > 0 && frame <= ks->ev[ks->count - 1].frame) return false;
        p = end + 1;
        uint16_t keys = 0;
        if (*p == '-') {
            p++;
        } else {
            for (;;) {
                size_t len = strcspn(p, "+,");
                size_t k = 0;
                while (k < name_count && !(strlen(names[k].name) == len && !strncmp(p, names[k].name, len))) k++;
                if (k == name_count) return false;
                keys |= names[k].key;
                p += len;
                if (*p != '+') break;
                p++;
            }
        }
        if (!key_script_add(ks, frame, keys)) return false;
        if (*p == ',') p++;
        else if (*p) return false;
    }
    return true;
}

uint16_t key_script_at(const key_script_t *ks, long frame) {
    uint16_t keys = 0;
    for (int i = 0; i < ks->count && ks->ev[i].frame <= frame; i++) keys = ks->ev[i].keys;
    return keys;
}
