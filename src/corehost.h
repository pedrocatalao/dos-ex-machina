#ifndef DXM_COREHOST_H
#define DXM_COREHOST_H
#include <stdint.h>
#include "dxm_core.h"
int  corehost_start(const dxm_core_info *info, const char *data_dir);
void corehost_stop(void);              /* ask the core to unwind, then join   */
int  corehost_running(void);
/* latest published frame, already palette-resolved to RGB8; NULL if none */
const uint8_t *corehost_frame(int *w,int *h,int *crt_lines);
void corehost_push_key(int scancode,int down,int ch);
void corehost_audio(int16_t *out,int nframes);
#endif
