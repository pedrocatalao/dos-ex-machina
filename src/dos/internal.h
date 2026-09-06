/* internal.h — shared between the files of the DOS simulation, and
 * nothing outside src/dos.  The public interface is src/dos.h. */
#ifndef DXM_DOS_INTERNAL_H
#define DXM_DOS_INTERNAL_H
#include "dos.h"
#include "term.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* The machine as a whole: what the shell, the boot sequence and the
 * navigator all read and change, and what main.c asks about each frame. */
typedef struct {
    dos_state state;
    double    now;              /* the time of the last dos_update */
    double    floppy_req;       /* seconds of drive activity wanted, taken by main */
    int       beep_pending;     /* the POST beep, taken by main */
    char      launch[32];       /* the game about to start */
    int       launch_pending;   /* main takes it once, when the drive is done */
    double    launch_at;        /* when the hold ends; <0 armed, 0 idle */
} dos_machine;
extern dos_machine machine;

/* Start a game: the command has been echoed, the drive reads for a while,
 * and dos_update releases the launch when that has had time to run. */
void dos_launch(const char *id);

/* shell.c - the prompt and its commands */
void shell_init(void);
void shell_prompt(void);
void shell_reset_line(void);           /* forget what was being typed */
void shell_key(int ch,int sc);
void shell_set_dir(int in_games);

/* boot.c - POST and AUTOEXEC */
void boot_init(void);
void boot_skip(void);                  /* a key: no more waiting between lines */
int  boot_update(double t);            /* 1 the moment the prompt should appear */
void boot_badge(double t);             /* the POST badge, over the frame buffer */

/* nc.c - the navigator */
void nc_open_panel(int from_games);
int  nc_is_open(void);
void nc_key(int ch,int sc);
void nc_update(double t);
void nc_draw_art(void);                /* the artwork, after the cells */
int  nc_resume_after_game(void);       /* 1 if the navigator took the screen back */
#endif
