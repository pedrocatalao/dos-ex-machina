/* processors.h — the chips the machine can be, one per stop of the turbo
 * display: the 486s a 1993 case could claim, and then the Pentiums that
 * went into the same beige boxes over the next four years, up to the 233
 * MMX.  A late-DOS game in a high VESA mode wanted one of those and will
 * crawl below them, which is what the Pentiums are for.
 *
 * Each names what the emulator is ASKED for, in DOSBox cycles - the
 * instructions a millisecond - at roughly what that chip did; cycles
 * climb faster than the clock because the later chips did more in each
 * one.  Not what it gets: the host has to be able to execute that many,
 * and the top stops want the recompiler rather than the interpreter.
 *
 * The stop is kept between runs (dxm.cfg, as `processor = code`), chosen
 * in SETUP's MACHINE section, stepped by the display's - and + keys, and
 * printed on the POST as the CPU type and clock. */
#ifndef DXM_PROCESSORS_H
#define DXM_PROCESSORS_H

typedef struct {
    const char *code; /* as dxm.cfg keeps it */
    const char *name; /* as SETUP lists it */
    const char *chip; /* as the POST prints it, on its CPU Type line */
    int mhz;          /* the clock, as the turbo display shows it */
    int cycles;       /* what that asks of the emulator */
} dxm_processor;

#define DXM_NPROCESSORS 10
#define DXM_PROCESSOR_DEFAULT 3 /* the 486DX2 at 66: what a 1993 case had in it */
extern const dxm_processor dxm_processors[DXM_NPROCESSORS];

/* the stop a code names, or -1 */
int dxm_processor_find(const char *code);
#endif
