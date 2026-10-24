#ifndef GBAEMU_SAVE_FILE_H
#define GBAEMU_SAVE_FILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A game's save file (plain C: the PC tests build it too). */

/* The whole file: malloc'd bytes and *size, or NULL if it does not exist or cannot be read. */
uint8_t *save_file_load(const char *path, size_t *size);

/* Writes safely: a copy of the current file goes to <path>.bak, the new bytes go to <path>.tmp
 * (flushed to the disk), then <path>.tmp replaces <path> in one step — so <path> is always either
 * the old or the new save, never half of one. On any failure <path> is left as it was: false. */
bool save_file_write(const char *path, const uint8_t *data, size_t size);

/* The same without the .bak copy (save states: the slots themselves are the history). */
bool save_file_replace(const char *path, const uint8_t *data, size_t size);

#endif
