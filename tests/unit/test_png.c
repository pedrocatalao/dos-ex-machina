/* png: every filter type, RGB and RGBA, the real badge artwork, and the
 * refusals.  Decoded pixels are compared by digest against a reference
 * decoder run when the fixtures were made. */
#include "check.h"
#include "png.h"
#include "sha256.h"
#include <stdlib.h>

static void digest_of(const uint8_t *px, int w, int h, char out[65]) {
    sha256 c;
    sha256_init(&c);
    sha256_update(&c, px, (size_t)w * h * 4);
    sha256_final(&c, out);
}

int main(void) {
    char err[128], sum[65];
    int w, h;
    uint8_t *px;

    /* 7x5 RGB, one row per filter type 0..4 */
    err[0] = 0;
    px = png_load(TEST_FIXTURES "/filters_rgb.png", &w, &h, err, sizeof err);
    CHECK(px != NULL);
    if (px) {
        CHECK(w == 7 && h == 5);
        digest_of(px, w, h, sum);
        CHECK_STR(sum, "e191b8ff46a5b28061473922db1e402eb3f5a828e0b67e90627689b01319a835");
        /* RGB comes out with an opaque alpha */
        CHECK(px[3] == 255 && px[(size_t)w * h * 4 - 1] == 255);
        free(px);
    }

    /* 4x4 RGBA, filters 4,3,2,1 */
    px = png_load(TEST_FIXTURES "/filters_rgba.png", &w, &h, err, sizeof err);
    CHECK(px != NULL);
    if (px) {
        CHECK(w == 4 && h == 4);
        digest_of(px, w, h, sum);
        CHECK_STR(sum, "dbf9abaa0bf20f20c5ae6d703a1a239f1bd57b12c6b4058d8598610d3bf05f59");
        free(px);
    }

    /* the badge from assets/: 400x166 RGBA, as the tools see it */
    px = png_load(TEST_ASSETS "/dxm-badge.png", &w, &h, err, sizeof err);
    CHECK(px != NULL);
    if (px) {
        CHECK(w == 400 && h == 166);
        digest_of(px, w, h, sum);
        CHECK_STR(sum, "83f25a33ea1bda8f54ff627821c62a32624f6fdaf6f26724f3c1cd234290b5f9");
        free(px);
    }

    /* refused, with a reason: 16-bit depth */
    err[0] = 0;
    px = png_load(TEST_FIXTURES "/depth16.png", &w, &h, err, sizeof err);
    CHECK(px == NULL);
    CHECK(err[0] != 0);
    /* refused: not a PNG */
    err[0] = 0;
    px = png_load(TEST_FIXTURES "/notazip.bin", &w, &h, err, sizeof err);
    CHECK(px == NULL);
    CHECK(err[0] != 0);
    /* refused: missing */
    err[0] = 0;
    px = png_load(TEST_FIXTURES "/missing.png", &w, &h, err, sizeof err);
    CHECK(px == NULL);
    CHECK(err[0] != 0);
    /* png_decode on a truncated buffer must not read past it */
    {
        FILE *f = fopen(TEST_FIXTURES "/filters_rgb.png", "rb");
        uint8_t buf[4096];
        size_t n = f ? fread(buf, 1, sizeof buf, f) : 0;
        if (f)
            fclose(f);
        CHECK(n > 40);
        err[0] = 0;
        px = png_decode(buf, n / 2, &w, &h, err, sizeof err);
        CHECK(px == NULL);
        CHECK(err[0] != 0);
    }
    return check_done("png");
}
