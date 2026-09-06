/* selftest.c — see selftest.h. */
#include "selftest.h"
#include "dos.h"
#include "corehost.h"
#include "dxm_core.h"
#include <stdio.h>

/* Timed in SECONDS, not frames.  Everything this waits on is wall-clock -
 * the launch holds 2.3s while the drive reads, the intro runs at its own
 * pace - so a frame-count timeout makes the test pass or fail on how fast
 * the machine draws.  Windowed with no true vsync that is hundreds of
 * frames a second, and the window closed before the launch had even
 * fired. */
int selftest_step(double t) {
    static int stage = 0, runs = 0, fail = 0;
    static double mark = -1.0;
    if (mark < 0.0)
        mark = t;
    double E = t - mark; /* seconds in this stage */
    switch (stage) {
    /* Games live in C:\GAMES and run from there, so the test has to walk
     * there like a user would. */
    case 0:
        if (dos_update(t) == DOS_PROMPT) {
            for (const char *q = "CD GAMES"; *q; q++)
                dos_key(*q, 0);
            dos_key('\r', 0);
            for (const char *q = "SKYROADS"; *q; q++)
                dos_key(*q, 0);
            dos_key('\r', 0);
            mark = t;
            stage = 1;
        }
        break;
    case 1:
        if (E > 4.0) { /* > the 2.3s load pause */
            if (!corehost_running()) {
                printf("FAIL: core did not start (run %d)\n", runs + 1);
                fail = 1;
                stage = 4;
            } else {
                printf("  run %d: core running\n", runs + 1);
                mark = t;
                stage = 2;
            }
        }
        break;
    case 2: /* Esc: intro -> menu -> plat_exit -> longjmp -> unwind */
    {
        int phase = (int)(E / 0.33);
        corehost_push_key(DXM_SC_ESC, (phase & 1) == 0, (phase & 1) ? 0 : 27);
    }
        if (!corehost_running()) {
            printf("  run %d: core unwound, host ALIVE (PORTING 3.1)\n", runs + 1);
            runs++;
            mark = t;
            stage = (runs < 2) ? 3 : 4;
        } else if (E > 12.0) {
            printf("FAIL: core never unwound\n");
            fail = 1;
            stage = 4;
        }
        break;
    case 3:
        if (E > 1.5) {
            /* the prompt comes back where the game left it, so this is
             * already C:\GAMES */
            for (const char *q = "SKYROADS"; *q; q++)
                dos_key(*q, 0);
            dos_key('\r', 0);
            mark = t;
            stage = 1;
        }
        break;
    case 4:
        printf(fail ? "SELFTEST FAIL\n"
                    : "SELFTEST PASS: launch, unwind, reload, relaunch (PORTING 3.1 + 3.2)\n");
        return 1;
    }
    return 0;
}
