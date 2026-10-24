#ifndef GBA_TOOLS_PNG_H
#define GBA_TOOLS_PNG_H

#include <stdint.h>

/* Write a BGR555 image as a 24-bit RGB PNG. Returns 0 on success. */
int png_write_bgr555(const char *path, const uint16_t *pixels, int width, int height);

#endif
