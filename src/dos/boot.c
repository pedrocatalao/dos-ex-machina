/* boot.c — the boot theatre: the BIOS banner, the RAM count that spins in
 * place, the disk lines, and the badge that sits top-right through it all.
 * It ends on "Starting DXM-DOS...": what follows is the real DOS's own
 * AUTOEXEC, printed by DOSBox on the screen it takes over. */
#include "internal.h"
#include "version.h" /* the BIOS banner carries the release */

static const char *BOOT[] = {"DXM BIOS v" DXM_VERSION " (C) 2026 DOS ex Machina",
                             "",
                             "Main Processor  : 80486DX2  66 MHz",
                             "Memory Test     : ", /* counted live, see dos_update */
                             "",
                             "Fixed Disk 0    : WDC AC2540F  540 MB",
                             "Floppy Disk A   : 1.44 MB, 3.5 in.",
                             "",
                             "Starting DXM-DOS...",
                             "",
                             NULL};

static double t0, next_boot;
static int boot_step;
static int mem_counting;     /* the memory test is spinning          */
static double mem_next;      /* next number update                   */
static long mem_shown;       /* KB counted so far                    */
static int mem_row, mem_col; /* where to overwrite the digits        */

void boot_init(void) {
    boot_step = 0;
    t0 = -1;
    next_boot = 0;
    mem_counting = 0;
    mem_shown = 0;
}
void boot_skip(void) {
    next_boot = 0;
}

/* The RAM count spins in place: the BIOS printed the running total and
 * overwrote it, so we hold the cursor at the digits and rewrite them. */
#define MEM_TOTAL_KB 655360L
#define MEM_STEP_KB 8192L /* the count visibly steps, not smooth */

static void mem_draw(long kb) {
    char buf[24];
    snprintf(buf, sizeof buf, "%ld KB", kb);
    int c = mem_col;
    for (const char *p = buf; *p && c < DOS_COLS; p++)
        term_poke(mem_row, c++, *p);
    while (c < DOS_COLS && c < mem_col + 12)
        term_poke(mem_row, c++, ' ');
}

int boot_update(double t) {
    if (t0 < 0) {
        t0 = t;
        next_boot = t + 0.35;
    }
    if (mem_counting) {
        if (t >= mem_next) {
            mem_shown += MEM_STEP_KB;
            if (mem_shown >= MEM_TOTAL_KB) {
                mem_shown = MEM_TOTAL_KB;
                mem_draw(mem_shown);
                {
                    int c = mem_col + 11;
                    const char *ok = " OK";
                    for (const char *p = ok; *p && c < DOS_COLS; p++)
                        term_poke(mem_row, c++, *p);
                }
                mem_counting = 0;
                term_newline();           /* close the line */
                machine.beep_pending = 1; /* POST beep AFTER the RAM check */
                next_boot = t + 0.45;
            } else {
                mem_draw(mem_shown);
                mem_next = t + 0.010;
            }
        }
        return 0; /* boot text pauses while counting */
    }

    if (t >= next_boot) {
        if (BOOT[boot_step]) {
            const char *ln = BOOT[boot_step++];
            term_say(ln);
            if (strstr(ln, "Floppy Disk A"))
                machine.floppy_req = 1.1;    /* BIOS seeks A: */
            if (strstr(ln, "Memory Test")) { /* start the live count here */
                mem_row = term_row();
                mem_col = term_col();
                mem_counting = 1;
                mem_shown = 0;
                mem_next = t;
                return 0;
            }
            term_put('\n');
            next_boot = t + 0.16;
        } else
            return 1;
    }
    return 0;
}

/* The DXM mark, shown top-right during POST the way a 486 showed its BIOS
 * or power-management badge.  Baked in by tools/mklogo.py with the white
 * background keyed to alpha so it composites onto the black screen. */
#include "gen/logo.h"
void boot_badge(double t) {
    if (t0 < 0)
        return;
    float a = (float)((t - t0 - 0.55) / 1.2); /* fade in */
    if (a <= 0.0f)
        return;
    if (a > 1.0f)
        a = 1.0f;
    int x0 = DOS_W - DXM_LOGO_W - 14, y0 = 10;
    for (int y = 0; y < DXM_LOGO_HT; y++) {
        int dy = y0 + y;
        if (dy < 0 || dy >= DOS_H)
            continue;
        for (int x = 0; x < DXM_LOGO_W; x++) {
            int dx = x0 + x;
            if (dx < 0 || dx >= DOS_W)
                continue;
            const uint8_t *sp = dxm_logo + ((size_t)y * DXM_LOGO_W + x) * 4;
            float al = sp[3] / 255.0f * a;
            if (al <= 0.004f)
                continue;
            uint8_t *q = term_fb() + ((size_t)dy * DOS_W + dx) * 3;
            for (int k = 0; k < 3; k++)
                q[k] = (uint8_t)(q[k] * (1.0f - al) + sp[k] * al);
        }
    }
}
