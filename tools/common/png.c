#include "png.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t crc_table[256];

static void crc_init(void) {
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
}

static uint32_t crc_update(uint32_t crc, const uint8_t *buf, size_t len) {
    for (size_t i = 0; i < len; i++) crc = crc_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

static void put32be(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static int write_chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len) {
    uint8_t head[8], tail[4];
    put32be(head, len);
    memcpy(head + 4, type, 4);
    uint32_t crc = crc_update(0xFFFFFFFFu, head + 4, 4);
    crc = crc_update(crc, data, len) ^ 0xFFFFFFFFu;
    put32be(tail, crc);
    return fwrite(head, 1, 8, f) == 8 && (len == 0 || fwrite(data, 1, len, f) == len) && fwrite(tail, 1, 4, f) == 4;
}

static uint8_t expand5(uint32_t c) {
    return (uint8_t)(c << 3 | c >> 2);
}

int png_write_bgr555(const char *path, const uint16_t *pixels, int width, int height) {
    crc_init();
    size_t row = 1 + (size_t)width * 3;
    size_t raw_len = row * (size_t)height;
    uint8_t *raw = malloc(raw_len);
    if (!raw) return -1;
    for (int y = 0; y < height; y++) {
        uint8_t *out = raw + (size_t)y * row;
        *out++ = 0; /* filter: none */
        for (int x = 0; x < width; x++) {
            uint32_t c = pixels[y * width + x];
            *out++ = expand5(c & 31);
            *out++ = expand5((c >> 5) & 31);
            *out++ = expand5((c >> 10) & 31);
        }
    }

    /* zlib stream made of stored (uncompressed) deflate blocks. */
    size_t blocks = (raw_len + 65534) / 65535;
    size_t z_len = 2 + raw_len + blocks * 5 + 4;
    uint8_t *z = malloc(z_len);
    if (!z) {
        free(raw);
        return -1;
    }
    size_t p = 0;
    z[p++] = 0x78;
    z[p++] = 0x01;
    for (size_t off = 0; off < raw_len;) {
        size_t n = raw_len - off > 65535 ? 65535 : raw_len - off;
        z[p++] = (off + n == raw_len) ? 1 : 0; /* BFINAL on the last block, BTYPE 00 */
        z[p++] = (uint8_t)n;
        z[p++] = (uint8_t)(n >> 8);
        z[p++] = (uint8_t)~n;
        z[p++] = (uint8_t)(~n >> 8);
        memcpy(z + p, raw + off, n);
        p += n;
        off += n;
    }
    uint32_t a = 1, b = 0; /* Adler-32 of the uncompressed data */
    for (size_t i = 0; i < raw_len; i++) {
        a = (a + raw[i]) % 65521;
        b = (b + a) % 65521;
    }
    put32be(z + p, b << 16 | a);
    p += 4;

    uint8_t ihdr[13];
    put32be(ihdr, (uint32_t)width);
    put32be(ihdr + 4, (uint32_t)height);
    ihdr[8] = 8;  /* bits per channel */
    ihdr[9] = 2;  /* colour type: RGB */
    ihdr[10] = 0; /* compression */
    ihdr[11] = 0; /* filter */
    ihdr[12] = 0; /* interlace */

    static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    FILE *f = fopen(path, "wb");
    int ok = f && fwrite(signature, 1, 8, f) == 8 && write_chunk(f, "IHDR", ihdr, 13) &&
             write_chunk(f, "IDAT", z, (uint32_t)p) && write_chunk(f, "IEND", NULL, 0);
    if (f && fclose(f) != 0) ok = 0;
    free(raw);
    free(z);
    return ok ? 0 : -1;
}
