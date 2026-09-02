/* install.h — see install.c.  One install at a time; NC polls the status. */
#ifndef DXM_INSTALL_H
#define DXM_INSTALL_H
#include "catalog.h"

typedef enum { INST_IDLE = 0, INST_RUNNING, INST_DONE, INST_FAILED } inst_state;

typedef struct {
    inst_state state;
    char       id[32], title[64];
    char       stage[64];      /* "Downloading game", "Verifying", ... */
    int        step, steps;
    double     frac;           /* 0..1 within the current step */
    double     got, total;     /* bytes, for the byte counter   */
    char       err[192];
    char       skipped[96];    /* an optional part that did not arrive */
} inst_status;

int  install_start(const cat_game *g);
void install_cancel(void);
void install_poll(inst_status *out);
void install_clear(void);      /* forget a finished result */

#endif
