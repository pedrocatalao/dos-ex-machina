/* dos.c — the boot's façade: main.c talks to this, and it dispatches to
 * the POST theatre and holds what the frame loop asks about. */
#include "internal.h"

dos_machine machine;

void dos_init(void) {
    term_clear();
    memset(&machine, 0, sizeof machine);
    machine.state = DOS_BOOT;
    boot_init();
}

void dos_skip(void) {
    if (machine.state == DOS_BOOT)
        boot_skip();
}

/* The PC speaker POST beep: taken once, at power-on. */
int dos_take_beep(void) {
    int b = machine.beep_pending;
    machine.beep_pending = 0;
    return b;
}
double dos_take_floppy(void) {
    double f = machine.floppy_req;
    machine.floppy_req = 0.0;
    return f;
}

dos_state dos_update(double t) {
    machine.now = t;
    if (machine.state == DOS_BOOT && boot_update(t)) {
        /* the BIOS screen goes; the DOS that takes over draws the next thing */
        term_clear();
        machine.state = DOS_HANDOVER;
    }
    return machine.state;
}

const uint8_t *dos_render(void) {
    term_render();
    if (machine.state == DOS_BOOT)
        boot_badge(machine.now); /* POST only */
    return term_fb();
}
