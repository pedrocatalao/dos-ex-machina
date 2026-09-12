/* banner.c — the artwork at the top of SETUP: opened, shaded, and fitted
 * to a palette the screen can hold.
 *
 * The files travel in the program as the files they were made as
 * (src/gen/banners.c), which is a third of what the same pictures cost as
 * pixels.  The price is this file: everything the baker used to do offline
 * happens here when one is opened, and a 256-colour screen needs a palette
 * the file has not got.
 *
 * Median cut, on a histogram of the image at five bits a channel rather
 * than on its quarter of a million pixels: the boxes split on their widest
 * side at the weighted median, every colour in a box becomes that box's
 * average, and the pixels are a lookup afterwards.  It is the algorithm
 * the machines of the day used for the same job, for the same reason. */
#include "internal.h"
#include "log.h"
#include "gen/banners.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* stb_image, vendored as it ships (public domain).  It is not ours to
 * clean up after, so its warnings are silenced here rather than edited
 * out of it; only the two formats the machine actually reads are built. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wmissing-prototypes"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#include "stb_image.h"
#pragma GCC diagnostic pop

#define FADE_FROM 0.45f /* of the height: full brightness above this */
#define LIFT 1.45f      /* against the tube's gamma, which pulls it down */
#define DARK 12         /* a row this dim at the top is the artwork's border */
#define CELLS (32 * 32 * 32)

static banner B;
static uint8_t px[DXM_BANNER_W * DXM_BANNER_H];
static uint16_t cellbuf[DXM_BANNER_W * DXM_BANNER_H]; /* the cell each pixel fell in */
static uint8_t pal[BANNER_COLOURS * 3];

/* the histogram, and the boxes median cut carves it into */
static int count[CELLS];
static int cell[CELLS]; /* the populated ones, in box order */
static uint8_t cmap[CELLS];

typedef struct {
    int at, n; /* a run of cell[] */
    long pixels;
} box;

static int axis; /* what box_cmp is sorting on */
static int box_cmp(const void *a, const void *b) {
    int ca = *(const int *)a >> (axis * 5), cb = *(const int *)b >> (axis * 5);
    return (ca & 31) - (cb & 31);
}

static int cell_of(int r, int g, int b) {
    return (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10);
}
static void cell_rgb(int c, int *r, int *g, int *b) {
    *r = ((c & 31) << 3) | 4;
    *g = (((c >> 5) & 31) << 3) | 4;
    *b = (((c >> 10) & 31) << 3) | 4;
}

/* Fit `want` colours to the histogram and write them into pal, leaving
 * cmap pointing every populated cell at the one it became. */
static void median_cut(int cells, int want) {
    box *boxes = calloc((size_t)want, sizeof *boxes);
    if (!boxes)
        return;
    int nb = 1;
    boxes[0].at = 0;
    boxes[0].n = cells;
    for (int i = 0; i < cells; i++)
        boxes[0].pixels += count[cell[i]];

    while (nb < want) {
        int pick = -1;
        for (int i = 0; i < nb; i++)
            if (boxes[i].n > 1 && (pick < 0 || boxes[i].pixels > boxes[pick].pixels))
                pick = i;
        if (pick < 0)
            break;
        box *bx = &boxes[pick];
        int lo[3] = {31, 31, 31}, hi[3] = {0, 0, 0};
        for (int i = bx->at; i < bx->at + bx->n; i++)
            for (int k = 0; k < 3; k++) {
                int v = (cell[i] >> (k * 5)) & 31;
                if (v < lo[k])
                    lo[k] = v;
                if (v > hi[k])
                    hi[k] = v;
            }
        axis = 0;
        for (int k = 1; k < 3; k++)
            if (hi[k] - lo[k] > hi[axis] - lo[axis])
                axis = k;
        qsort(&cell[bx->at], (size_t)bx->n, sizeof cell[0], box_cmp);

        long half = bx->pixels / 2, run = 0;
        int cut = 1;
        for (int i = 0; i < bx->n - 1; i++) {
            run += count[cell[bx->at + i]];
            if (run >= half) {
                cut = i + 1;
                break;
            }
        }
        long left = 0;
        for (int i = 0; i < cut; i++)
            left += count[cell[bx->at + i]];
        boxes[nb].at = bx->at + cut;
        boxes[nb].n = bx->n - cut;
        boxes[nb].pixels = bx->pixels - left;
        bx->n = cut;
        bx->pixels = left;
        nb++;
    }

    for (int i = 0; i < nb; i++) {
        long sr = 0, sg = 0, sb = 0, n = 0;
        for (int j = boxes[i].at; j < boxes[i].at + boxes[i].n; j++) {
            int c = cell[j], r, g, b;
            cell_rgb(c, &r, &g, &b);
            long w = count[c];
            sr += r * w;
            sg += g * w;
            sb += b * w;
            n += w;
            cmap[c] = (uint8_t)i;
        }
        if (!n)
            n = 1;
        pal[i * 3] = (uint8_t)(sr / n);
        pal[i * 3 + 1] = (uint8_t)(sg / n);
        pal[i * 3 + 2] = (uint8_t)(sb / n);
    }
    free(boxes);
}

const banner *banner_open(int which) {
    if (dxm_banner_count <= 0)
        return NULL;
    which %= dxm_banner_count;
    /* the screen is drawn every frame; the artwork is opened once */
    static int loaded = -1;
    if (which == loaded)
        return &B;
    clock_t t0 = clock();
    const dxm_banner *b = &dxm_banners[which];
    int w, h, comp;
    stbi_uc *rgb = stbi_load_from_memory(b->file, (int)b->len, &w, &h, &comp, 3);
    if (!rgb) {
        dxm_log("setup: %s will not open: %s", b->name, stbi_failure_reason());
        return NULL;
    }
    if (w != DXM_BANNER_W || h > DXM_BANNER_H) {
        dxm_log("setup: %s is %dx%d, not %dx%d", b->name, w, h, DXM_BANNER_W, DXM_BANNER_H);
        stbi_image_free(rgb);
        return NULL;
    }

    /* off the top, the black band the artwork was drawn with: on screen it
     * is a gap between the top of the picture and the picture itself */
    int first = 0;
    for (int y = 0; y < h; y++) {
        long sum = 0;
        for (int x = 0; x < w * 3; x++)
            sum += rgb[(size_t)y * w * 3 + x];
        if (sum / (w * 3) >= DARK) {
            first = y;
            break;
        }
    }
    int hh = h - first;

    /* bright to the waist, then away to black by the bottom edge, so the
     * artwork dissolves into the screen instead of being washed all over */
    memset(count, 0, sizeof count);
    int cells = 0;
    for (int y = 0; y < hh; y++) {
        float f = ((float)y / (float)(hh - 1) - FADE_FROM) / (1.0f - FADE_FROM);
        float keep = f <= 0.0f ? 1.0f : powf(1.0f - f, 1.4f);
        for (int x = 0; x < w; x++) {
            const stbi_uc *s = rgb + ((size_t)(y + first) * w + x) * 3;
            int v[3];
            for (int k = 0; k < 3; k++) {
                float c = (float)s[k] * LIFT * keep;
                v[k] = c > 255.0f ? 255 : (int)(c + 0.5f);
            }
            int c = cell_of(v[0], v[1], v[2]);
            if (!count[c])
                cell[cells++] = c;
            count[c]++;
            cellbuf[(size_t)y * w + x] = (uint16_t)c;
        }
    }
    stbi_image_free(rgb);

    median_cut(cells, BANNER_COLOURS);
    for (int i = 0; i < hh * w; i++)
        px[i] = cmap[cellbuf[i]];

    B.w = w;
    B.h = hh;
    B.px = px;
    B.pal = pal;
    B.name = b->name;
    loaded = which;
    dxm_log("setup: %s opened, %d colours fitted in %.0f ms", b->name, BANNER_COLOURS,
            (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC);
    return &B;
}

int banner_count(void) {
    return dxm_banner_count;
}
