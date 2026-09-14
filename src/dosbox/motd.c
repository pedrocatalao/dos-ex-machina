/* motd.c — the message of the day, the line under the greeting.
 *
 * Drawn at each boot, so the machine has something else to say every time
 * it is switched on; the same one twice in a row is the price of that.
 * A MOTD.TXT in the root of C: replaces the built-in set entirely - one
 * message a line, blank lines and lines starting with ';' ignored - so the
 * machine can be given its own without touching the program. */
#include "internal.h"
#include <SDL3/SDL.h> /* SDL_rand */
#include <stdio.h>
#include <string.h>

/* The shipped set.  Period advice, mostly true. */
static const char *const MOTD[] = {
    "The future was better in 1993.",
    "Reality is just another compatibility layer.",
    "It's not a bug. It's undocumented nostalgia.",
    "Wake up. The machine is dreaming.",
    "Somewhere, a 486 is still waiting.",
    "Your memory is insufficient. Ours is 640K.",
    "The pixels remember.",
    "Insert disk. Change reality.",
    "You can't kill what still runs in DOS.",
    "The machine knows what you did in 1994.",
    "The old ways still work.",
    "This machine has seen things you people wouldn't believe.",
    "There's always another level.",
    "Never underestimate a blinking cursor.",
    "You are entering a world of pain, pixels and IRQ conflicts.",
    "640K ought to be enough for tonight.",
    "Somewhere between 320x200 and infinity.",
    "All your base are still belong to us.",
    "There is no save point in real life.",
    "The game is never over. It just returns to DOS.",
    "Welcome back. We kept your machine running.",
    "Every byte under 640K is worth fighting for.",
    "A clean boot floppy is a friend for life.",
    "Label your diskettes. You will not remember.",
    "Press any key. Yes, that one.",
};
#define MOTD_N ((int)(sizeof MOTD / sizeof MOTD[0]))

/* One of n, drawn afresh.  Not SDL_rand: its generator is seeded from the
 * clock in whole seconds, so two machines started in the same second - or
 * one started twice while testing - say the same thing.  The performance
 * counter moves far faster than that; the shuffle spreads its low bits. */
static int draw(int n) {
    Uint64 s = SDL_GetPerformanceCounter();
    s ^= s >> 33;
    s *= 0xff51afd7ed558ccdULL;
    s ^= s >> 29;
    return n > 0 ? (int)(s % (Uint64)n) : 0;
}

/* A line of the user's own MOTD.TXT, if there is one.  1 if buf was
 * filled, 0 to fall back to the shipped set. */
static int from_file(const char *c_drive, char *buf, size_t n) {
    char path[1200];
    snprintf(path, sizeof path, "%s/MOTD.TXT", c_drive);
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    char text[8192];
    size_t got = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[got] = 0;

    /* two passes: count the usable lines, then take the one drawn */
    int count = 0, pick = 0;
    for (int pass = 0; pass < 2; pass++) {
        int at = 0;
        for (char *p = text; *p;) {
            char *end = p + strcspn(p, "\r\n");
            char keep = *end;
            *end = 0;
            while (*p == ' ' || *p == '\t')
                p++;
            if (*p && *p != ';') {
                if (pass == 0)
                    count++;
                else if (at++ == pick) {
                    snprintf(buf, n, "%s", p);
                    return 1;
                }
            }
            *end = keep;
            p = end + (keep ? 1 : 0);
            while (*p == '\n' || *p == '\r')
                p++;
        }
        if (pass == 0) {
            if (!count)
                return 0;
            pick = draw(count);
        }
    }
    return 0;
}

void motd_today(const char *c_drive, char *buf, size_t n) {
    if (c_drive && from_file(c_drive, buf, n))
        return;
    snprintf(buf, n, "%s", MOTD[draw(MOTD_N)]);
}
