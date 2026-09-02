/* png.c — just enough PNG to show a game's artwork.
 *
 * zlib is already linked for the data archives, and a PNG is IHDR, a
 * zlib stream of filtered scanlines, and IEND.  That makes this about a
 * hundred and fifty lines, against seven thousand for a general decoder we
 * would use one corner of.
 *
 * 8-bit RGB and RGBA, non-interlaced.  Everything else is refused rather
 * than half-handled: the art in the catalogue is ours to produce, so the
 * constraint costs nothing and the failure is loud.
 *
 * The bytes come off the network, so nothing here trusts a length field.
 * Every read is bounded by the buffer, and the pixel buffer is sized from
 * dimensions that were range-checked first. */
#include "png.h"
#include <zlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3];
}

static int paeth(int a, int b, int c) {
    int p = a + b - c, pa = abs(p-a), pb = abs(p-b), pc = abs(p-c);
    return (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
}

uint8_t *png_decode(const uint8_t *d, size_t n, int *ow, int *oh,
                    char *err, size_t errsz) {
    static const uint8_t SIG[8] = {0x89,'P','N','G','\r','\n',0x1a,'\n'};
    if (n < 8 || memcmp(d, SIG, 8) != 0) {
        snprintf(err, errsz, "not a PNG");
        return NULL;
    }
    uint32_t w = 0, h = 0;
    int bpp_ch = 0;                 /* channels per pixel */
    uint8_t *idat = NULL; size_t idat_n = 0, idat_cap = 0;
    size_t p = 8;

    while (p + 8 <= n) {
        uint32_t len = be32(d + p);
        const uint8_t *type = d + p + 4;
        if (len > n || p + 12 + len > n) {          /* the +12 covers crc */
            snprintf(err, errsz, "PNG is truncated");
            free(idat); return NULL;
        }
        const uint8_t *body = d + p + 8;
        if (!memcmp(type, "IHDR", 4)) {
            if (len < 13) { snprintf(err,errsz,"bad IHDR"); free(idat); return NULL; }
            w = be32(body); h = be32(body + 4);
            int depth = body[8], colour = body[9], interlace = body[12];
            if (depth != 8 || interlace != 0 || (colour != 2 && colour != 6)) {
                snprintf(err, errsz,
                         "unsupported PNG (need 8-bit RGB/RGBA, no interlace)");
                free(idat); return NULL;
            }
            if (!w || !h || w > 8192 || h > 8192) {
                snprintf(err, errsz, "PNG dimensions out of range");
                free(idat); return NULL;
            }
            bpp_ch = (colour == 6) ? 4 : 3;
        } else if (!memcmp(type, "IDAT", 4)) {
            if (idat_n + len > idat_cap) {
                size_t cap = idat_cap ? idat_cap * 2 : 65536;
                while (cap < idat_n + len) cap *= 2;
                uint8_t *q = realloc(idat, cap);
                if (!q) { snprintf(err,errsz,"out of memory"); free(idat); return NULL; }
                idat = q; idat_cap = cap;
            }
            memcpy(idat + idat_n, body, len);
            idat_n += len;
        } else if (!memcmp(type, "IEND", 4)) {
            break;
        }
        p += 12 + len;
    }
    if (!w || !idat_n) {
        snprintf(err, errsz, "PNG has no image data");
        free(idat); return NULL;
    }

    /* one filter byte per row, then w*channels bytes */
    size_t stride = (size_t)w * bpp_ch;
    size_t raw_n  = (stride + 1) * h;
    uint8_t *raw = malloc(raw_n);
    if (!raw) { snprintf(err,errsz,"out of memory"); free(idat); return NULL; }

    z_stream zs; memset(&zs, 0, sizeof zs);
    if (inflateInit(&zs) != Z_OK) {
        snprintf(err,errsz,"inflate init failed"); free(raw); free(idat); return NULL;
    }
    zs.next_in = idat; zs.avail_in = (uInt)idat_n;
    zs.next_out = raw; zs.avail_out = (uInt)raw_n;
    int r = inflate(&zs, Z_FINISH);
    inflateEnd(&zs);
    free(idat);
    if (zs.total_out != raw_n && r != Z_STREAM_END) {
        snprintf(err, errsz, "PNG data is corrupt");
        free(raw); return NULL;
    }

    /* undo the per-row filters, in place, then pack to RGBA */
    for (uint32_t y = 0; y < h; y++) {
        uint8_t *row = raw + (stride + 1) * y;
        int f = row[0];
        uint8_t *cur = row + 1;
        const uint8_t *up = y ? raw + (stride + 1) * (y - 1) + 1 : NULL;
        for (size_t i = 0; i < stride; i++) {
            int a = i >= (size_t)bpp_ch ? cur[i - bpp_ch] : 0;
            int b = up ? up[i] : 0;
            int c = (up && i >= (size_t)bpp_ch) ? up[i - bpp_ch] : 0;
            int x = cur[i];
            switch (f) {
                case 0: break;
                case 1: x += a; break;
                case 2: x += b; break;
                case 3: x += (a + b) / 2; break;
                case 4: x += paeth(a, b, c); break;
                default:
                    snprintf(err, errsz, "unknown PNG filter");
                    free(raw); return NULL;
            }
            cur[i] = (uint8_t)x;
        }
    }

    uint8_t *out = malloc((size_t)w * h * 4);
    if (!out) { snprintf(err,errsz,"out of memory"); free(raw); return NULL; }
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t *src = raw + (stride + 1) * y + 1;
        uint8_t *dst = out + (size_t)y * w * 4;
        for (uint32_t x = 0; x < w; x++) {
            dst[x*4+0] = src[x*bpp_ch+0];
            dst[x*4+1] = src[x*bpp_ch+1];
            dst[x*4+2] = src[x*bpp_ch+2];
            dst[x*4+3] = bpp_ch == 4 ? src[x*bpp_ch+3] : 255;
        }
    }
    free(raw);
    *ow = (int)w; *oh = (int)h;
    return out;
}

uint8_t *png_load(const char *path, int *w, int *h, char *err, size_t errsz) {
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(err, errsz, "cannot open image"); return NULL; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > 32L*1024*1024) {
        fclose(f); snprintf(err, errsz, "image is not a sensible size"); return NULL;
    }
    uint8_t *buf = malloc((size_t)n);
    if (!buf) { fclose(f); snprintf(err,errsz,"out of memory"); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    if (got != (size_t)n) { free(buf); snprintf(err,errsz,"short read"); return NULL; }
    uint8_t *px = png_decode(buf, got, w, h, err, errsz);
    free(buf);
    return px;
}
