#include "save_file.h"

#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

#define PATH_LEN 1024

uint8_t *save_file_load(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    uint8_t *data = NULL;
    long n = -1;
    if (fseek(f, 0, SEEK_END) == 0) n = ftell(f);
    if (n >= 0 && fseek(f, 0, SEEK_SET) == 0 && (data = malloc(n ? (size_t)n : 1)) != NULL &&
        fread(data, 1, (size_t)n, f) != (size_t)n) {
        free(data);
        data = NULL;
    }
    fclose(f);
    if (data) *size = (size_t)n;
    return data;
}

/* The whole buffer into a new file, flushed to the disk (not just to the system's cache). */
static bool write_synced(const char *path, const uint8_t *data, size_t size) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    bool ok = fwrite(data, 1, size, f) == size && fflush(f) == 0;
#ifdef _WIN32
    ok = ok && _commit(_fileno(f)) == 0;
#else
    ok = ok && fsync(fileno(f)) == 0;
#endif
    return fclose(f) == 0 && ok;
}

/* Puts `from` in place of `to` in one step (replacing it). */
static bool replace_file(const char *from, const char *to) {
#ifdef _WIN32
    return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING) != 0;
#else
    return rename(from, to) == 0;
#endif
}

bool save_file_write(const char *path, const uint8_t *data, size_t size) {
    char tmp[PATH_LEN], bak[PATH_LEN];
    if (snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp) return false;
    if (snprintf(bak, sizeof bak, "%s.bak", path) >= (int)sizeof bak) return false;
    size_t old_size = 0;
    uint8_t *old = save_file_load(path, &old_size);
    if (old) { /* keep the previous save */
        bool kept = write_synced(tmp, old, old_size) && replace_file(tmp, bak);
        free(old);
        if (!kept) {
            remove(tmp);
            return false;
        }
    }
    if (!write_synced(tmp, data, size) || !replace_file(tmp, path)) {
        remove(tmp);
        return false;
    }
    return true;
}

bool save_file_replace(const char *path, const uint8_t *data, size_t size) {
    char tmp[PATH_LEN];
    if (snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp) return false;
    if (!write_synced(tmp, data, size) || !replace_file(tmp, path)) {
        remove(tmp);
        return false;
    }
    return true;
}
