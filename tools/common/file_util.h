#ifndef GBA_TOOLS_FILE_UTIL_H
#define GBA_TOOLS_FILE_UTIL_H

#include <stddef.h>
#include <stdint.h>

/* Read a whole file; NULL if it is missing, empty or unreadable. Free the result with free(). */
uint8_t *read_file(const char *path, size_t *size);

/* Write size bytes to path (replacing it). Returns 0 on success. */
int write_file(const char *path, const uint8_t *data, size_t size);

#endif
