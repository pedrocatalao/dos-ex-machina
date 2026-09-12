/* boot.c — the boot theatre, the way an Award BIOS drew it: the banner
 * with the badge top-right, the CPU, the RAM count that spins in place,
 * the IDE drives detected one by one, and along the bottom the SETUP
 * prompt and the BIOS clock.  It ends by clearing the screen, which is the
 * handover: the machine's second screen, the System Configurations box, is
 * printed inside the emulator by the core's own BIOS program (the fork's
 * dosbox_pure_dxm.h), so DOS's greeting and prompt come up under it. */
#include "internal.h"
#include "version.h" /* the BIOS banner carries the release */
#include <time.h>

/* What the machine is.  The memory is DOSBox Pure's default, which the
 * machine does not change; the disk is the fiction C: is dressed in. */
#define MEM_TOTAL_KB 16384L
#define MEM_STEP_KB 256L /* the count visibly steps, not smooth */
#define DISK_MODEL "WDC AC2540F"
#define BIOS_SERIAL "2A4KD000C-00"
/* the clock under --deterministic: 2026-09-11 12:00:00, read as UTC so the
 * golden frames do not depend on the time zone they are captured in */
#define FIXED_EPOCH 1789128000

typedef enum { LINE, CPU, MEMORY, DETECT, END } step_kind;
typedef struct {
    step_kind kind;
    const char *text, *found; /* DETECT: the drive looked for, and what answers */
    double wait;              /* before the next step */
    double drive;             /* seconds of drive run while this step is on screen */
} step;

static const step POST[] = {
    {LINE, "DXM Modular BIOS v" DXM_VERSION ", An Energy Star Ally", NULL, 0.16, 0.0},
    {LINE, "Copyright (C) 1993-2026, DOS ex Machina", NULL, 0.16, 0.0},
    {LINE, "", NULL, 0.16, 0.0},
    {CPU, NULL, NULL, 0.16, 0.0},
    {MEMORY, "Memory Test :  ", NULL, 0.45, 0.0}, /* counted live, see boot_update */
    {LINE, "", NULL, 0.30, 0.0},
    {LINE, "DXM Plug and Play BIOS Extension v1.0A", NULL, 0.16, 0.0},
    {LINE, "Copyright (C) 2026, DOS ex Machina", NULL, 0.30, 0.0},
    /* the drive rattles while the BIOS looks at the drives, the way a real
     * one seeked A: on its way past */
    {DETECT, "Primary Master", DISK_MODEL, 0.12, 0.9},
    {DETECT, "Primary Slave ", "None", 0.80, 0.0},
    {END, NULL, NULL, 0.0, 0.0},
};
#define DETECT_SEEK 0.25 /* the pause before a drive answers */

static double t0, next_boot;
static int at;               /* the next step of POST[]                  */
static int footed;           /* the SETUP prompt and the clock are up    */
static const char *found;    /* a drive's answer, still to be printed    */
static int mhz, fixed_clock; /* the clock the turbo display powered on at */
static time_t epoch;         /* the wall time at t0                      */
static int mem_counting;     /* the memory test is spinning              */
static double mem_next;      /* next number update                       */
static long mem_shown;       /* KB counted so far                        */
static int mem_row, mem_col; /* where to overwrite the digits            */

void boot_init(int clock_mhz, int fixed) {
    t0 = -1;
    next_boot = 0;
    at = footed = 0;
    found = NULL;
    mhz = clock_mhz;
    fixed_clock = fixed;
    epoch = fixed ? (time_t)FIXED_EPOCH : time(NULL);
    mem_counting = 0;
    mem_shown = 0;
}
void boot_skip(void) {
    next_boot = 0;
}

static const char *cpu_name(void) {
    return mhz >= 100 ? "486DX4" : mhz >= 50 ? "486DX2" : "486DX";
}

/* The bottom of the screen: the way into SETUP, and the BIOS ID line with
 * the real-time clock ticking in it. */
static void footer(double t) {
    time_t now = epoch + (time_t)(t - t0);
    struct tm *tm = fixed_clock ? gmtime(&now) : localtime(&now);
    if (!tm)
        return;
    char buf[96];
    snprintf(buf, sizeof buf, "%02d/%02d/%04d %02d:%02d:%02d   S/N %s", tm->tm_mon + 1, tm->tm_mday,
             tm->tm_year + 1900, tm->tm_hour, tm->tm_min, tm->tm_sec, BIOS_SERIAL);
    term_puts(DOS_ROWS - 2, 0, "Press SPACE to enter SETUP", TERM_ATTR);
    term_puts(DOS_ROWS - 2, 6, "SPACE", 0x0F); /* the key, in bright white */
    term_puts(DOS_ROWS - 1, 0, buf, TERM_ATTR);
}

/* The RAM count spins in place: the BIOS printed the running total and
 * overwrote it, so we hold the cursor at the digits and rewrite them. */
static void mem_draw(long kb, const char *tail) {
    char buf[32];
    snprintf(buf, sizeof buf, "%ldK%s", kb, tail);
    term_fill_row(mem_row, mem_col, 12, ' ', TERM_ATTR);
    term_puts(mem_row, mem_col, buf, TERM_ATTR);
}

int boot_update(double t) {
    if (t0 < 0) {
        t0 = t;
        next_boot = t + 0.35;
    }
    if (footed)
        footer(t);
    if (mem_counting) {
        if (t >= mem_next) {
            mem_shown += MEM_STEP_KB;
            if (mem_shown >= MEM_TOTAL_KB) {
                mem_draw(MEM_TOTAL_KB, " OK");
                mem_counting = 0;
                term_newline();           /* close the line */
                machine.beep_pending = 1; /* POST beep AFTER the RAM check */
                next_boot = t + POST[at - 1].wait;
            } else {
                mem_draw(mem_shown, "");
                mem_next = t + 0.010;
            }
        }
        return 0; /* boot text pauses while counting */
    }
    if (t < next_boot)
        return 0;
    if (found) { /* the drive answers, and the next is looked for */
        term_sayln(found);
        found = NULL;
        next_boot = t + POST[at - 1].wait;
        return 0;
    }

    const step *s = &POST[at++];
    char buf[64];
    switch (s->kind) {
    case LINE:
        if (!footed) {
            footed = 1;
            footer(t);
        }
        term_sayln(s->text);
        break;
    case CPU:
        snprintf(buf, sizeof buf, "%s CPU at %dMHz", cpu_name(), mhz);
        term_sayln(buf);
        break;
    case MEMORY: /* start the live count here */
        term_say(s->text);
        mem_row = term_row();
        mem_col = term_col();
        mem_counting = 1;
        mem_shown = 0;
        mem_next = t;
        return 0;
    case DETECT:
        snprintf(buf, sizeof buf, "   Detecting HDD %s ... ", s->text);
        term_say(buf);
        if (s->drive > 0.0)
            machine.floppy_req = s->drive;
        found = s->found;
        next_boot = t + DETECT_SEEK;
        return 0;
    case END:
        at--;
        return 1;
    }
    next_boot = t + s->wait;
    return 0;
}

/* The DXM mark, shown top-right the way a 486 showed its BIOS or
 * power-management badge.  Baked in by tools/mklogo.py with the white
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
