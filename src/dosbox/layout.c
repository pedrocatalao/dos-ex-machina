/* layout.c — which keyboard the host has, as a DOS keyboard layout.
 *
 * DOS knows nothing about the keyboard the machine is really sitting on:
 * the core is told a layout at boot and translates the keys it is sent
 * through it.  Told nothing, it assumes a US board, and a Portuguese or a
 * French keyboard then types the wrong symbols at the prompt.
 *
 * There is no portable call that names the host's layout.  What SDL does
 * have is the mapping itself: ask it what a physical key produces and the
 * answer comes back through whatever layout the desktop is set to.  A
 * handful of keys is enough to tell the common boards apart - the letter
 * top left, the two beside the L, the one beside the M - because that is
 * exactly where the national layouts differ.
 *
 * It is a guess, so it says so in the log, with the keys it read: an
 * unfamiliar board falls back to US and `--keyboard CODE` overrides it. */
#include "internal.h"
#include "log.h"
#include <SDL3/SDL.h>
#include <string.h>

/* what a physical key produces on this host, with nothing held down */
static Uint32 key_of(SDL_Scancode sc) {
    return (Uint32)SDL_GetKeyFromScancode(sc, SDL_KMOD_NONE, false);
}

/* the codepoints that tell the boards apart, named so the table reads */
#define K_ccedil 0xE7  /* ç */
#define K_ntilde 0xF1  /* ñ */
#define K_ograve 0xF2  /* ò */
#define K_odiaer 0xF6  /* ö */
#define K_oslash 0xF8  /* ø */
#define K_aeligat 0xE6 /* æ */
#define K_eacute 0xE9  /* é */
#define K_shorti 0x439 /* и, in the Cyrillic block */

const char *dosbox_layout_detect(void) {
    Uint32 q = key_of(SDL_SCANCODE_Q);            /* AZERTY puts A here      */
    Uint32 y = key_of(SDL_SCANCODE_Y);            /* QWERTZ puts Z here      */
    Uint32 semi = key_of(SDL_SCANCODE_SEMICOLON); /* the key right of L      */
    Uint32 slash = key_of(SDL_SCANCODE_SLASH);    /* ABNT2 puts ; here       */
    /* the pound sign on 3, which is the UK board's own mark: a shifted 2
     * would not do, since half of Europe has the quote there */
    Uint32 pound = SDL_GetKeyFromScancode(SDL_SCANCODE_3, SDL_KMOD_SHIFT, false);

    const char *code = "us";
    if (q >= 0x400 && q <= 0x4FF)
        code = "ru"; /* a Cyrillic board */
    else if (q == 'a')
        code = "fr"; /* AZERTY: Belgium overrides, the two are close */
    else if (semi == K_ccedil)
        code = (slash == ';' ? "br" : "po"); /* ABNT2 against Portugal */
    else if (semi == K_ntilde)
        code = "sp";
    else if (semi == K_ograve)
        code = "it";
    else if (semi == K_aeligat)
        code = "dk";
    else if (semi == K_oslash)
        code = "no";
    else if (semi == K_eacute)
        code = "hu";
    else if (semi == K_odiaer)
        code = (y == 'z' ? "gr" : "sv"); /* Germany against Sweden/Finland */
    else if (pound == 0xA3)              /* £ */
        code = "uk";

    dxm_log("keyboard: q=U+%04X y=U+%04X semicolon=U+%04X slash=U+%04X shift3=U+%04X", q, y, semi,
            slash, pound);
    dxm_log("keyboard: DOS layout %s%s", code,
            !strcmp(code, "us") ? " (nothing else matched; --keyboard CODE overrides)" : "");
    return code;
}
