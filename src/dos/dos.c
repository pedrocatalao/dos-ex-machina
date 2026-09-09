/* dos.c — the DOS the machine boots into: the façade main.c talks to,
 * dispatching to the boot theatre, the prompt and the navigator, and
 * holding the state they share.  The prompt is the UI: there is no other
 * way to reach anything (SPEC §7). */
#include "internal.h"

dos_machine machine;

void dos_init(void) {
    term_clear();
    memset(&machine, 0, sizeof machine);
    machine.state = DOS_BOOT;
    shell_init();
    boot_init();
}

void dos_handover_mode(void) {
    machine.handover = 1;
}

void dos_launch(const char *id) {
    snprintf(machine.launch, sizeof machine.launch, "%s", id);
    machine.floppy_req = 2.6; /* the drive reads the game */
    machine.launch_at = -1.0; /* armed; set on the next tick */
    machine.state = DOS_RUNNING;
}

/* The game does not appear the instant you type its name: the drive spins
 * up and reads first, exactly as it would have.  dos_update() releases the
 * launch once that load has had time to run. */
const char *dos_launch_request(void) {
    if (!machine.launch_pending)
        return NULL;
    machine.launch_pending = 0;
    return machine.launch;
}

void dos_core_failed(void) {
    term_put('\n');
    term_sayln("Cannot run that program.");
    shell_prompt();
    machine.state = DOS_PROMPT;
    shell_reset_line();
}

void dos_core_exited(void) {
    machine.state = DOS_PROMPT;
    shell_reset_line();
    if (nc_resume_after_game())
        return;
    term_put('\n');
    shell_prompt();
}

int dos_nc_open(void) {
    return nc_is_open();
}

void dos_key(int ch, int sc) {
    if (machine.state != DOS_PROMPT) {
        if (machine.state == DOS_BOOT)
            boot_skip();
        return;
    }
    if (nc_is_open()) {
        nc_key(ch, sc);
        return;
    }
    shell_key(ch, sc);
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
    if (nc_is_open())
        nc_update(t);

    /* loading pause between the command and the game taking over */
    if (machine.state == DOS_RUNNING && !machine.launch_pending && machine.launch_at < 0.0)
        machine.launch_at = t + 2.3;
    if (machine.state == DOS_RUNNING && machine.launch_at > 0.0 && t >= machine.launch_at) {
        machine.launch_pending = 1;
        machine.launch_at = 0.0;
    }

    if (machine.state == DOS_BOOT && boot_update(t)) {
        if (machine.handover) {
            /* the BIOS screen goes; the DOS that takes over draws the next thing */
            term_clear();
            machine.state = DOS_HANDOVER;
        } else {
            machine.state = DOS_PROMPT;
            shell_prompt();
        }
    }
    return machine.state;
}

const uint8_t *dos_render(void) {
    static double blink;
    blink += 1.0;
    term_render();
    if (nc_is_open())
        nc_draw_art();
    if (machine.state == DOS_BOOT)
        boot_badge(machine.now); /* POST only */
    if (machine.state == DOS_PROMPT && !nc_is_open() && ((int)(blink / 28) & 1))
        term_draw_cursor();
    return term_fb();
}
