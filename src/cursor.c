/* cursor.c — see cursor.h. */
#include "cursor.h"
#include "log.h"

/* The pointer, 12 by 19 as the mouse drivers of the day drew it: a light
 * arrow with a one-pixel dark outline.  '#' fill, '.' outline, ' ' clear. */
static const char *const ARROW[19] = {
    ".           ", "..          ", ".#.         ", ".##.        ", ".###.       ",
    ".####.      ", ".#####.     ", ".######.    ", ".#######.   ", ".########.  ",
    ".#####..... ", ".##.##.     ", ".#. .##.    ", "..  .##.    ", ".    .##.   ",
    "     .##.   ", "      .##.  ", "      .##.  ", "       ..   ",
};
#define ARROW_W 12
#define ARROW_H 19

/* The arrow blown up `scale` times, so its pixels stay pixels. */
static SDL_Surface *arrow_surface(int scale) {
    SDL_Surface *s = SDL_CreateSurface(ARROW_W * scale, ARROW_H * scale, SDL_PIXELFORMAT_RGBA32);
    if (!s)
        return NULL;
    Uint32 *px = s->pixels;
    int pitch = s->pitch / 4;
    for (int y = 0; y < ARROW_H * scale; y++)
        for (int x = 0; x < ARROW_W * scale; x++) {
            char ch = ARROW[y / scale][x / scale];
            /* ABGR in memory for RGBA32: a warm off-white, a near-black line */
            Uint32 v = ch == '#' ? 0xFFE0E6E8u : ch == '.' ? 0xFF1A1E20u : 0u;
            px[y * pitch + x] = v;
        }
    return s;
}

SDL_Cursor *cursor_vintage(void) {
    /* At its own size, a point a pixel; the 2x display gets a second
     * image at twice that, so the pixels stay pixels there too. */
    SDL_Surface *s1 = arrow_surface(1), *s2 = arrow_surface(2);
    if (!s1 || !s2) {
        dxm_log("cursor: %s", SDL_GetError());
        return NULL;
    }
    SDL_AddSurfaceAlternateImage(s1, s2);
    SDL_Cursor *cur = SDL_CreateColorCursor(s1, 0, 0);
    if (!cur)
        dxm_log("cursor: %s", SDL_GetError());
    SDL_DestroySurface(s2);
    SDL_DestroySurface(s1);
    return cur;
}
