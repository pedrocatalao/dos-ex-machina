/* corehost.c — runs one core on its own thread and implements dxm_host.
 * The game owns its control flow (SPEC §4.1), so synchronisation happens at
 * the plat_present() boundary and nowhere else. */
#include "corehost.h"
#include "coreload.h"
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

static pthread_t       th;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static volatile int    running, quit_req;
static const dxm_core_info *cur_info;
static char            cur_data[1024], cur_pref[1026];

static uint8_t  rgb[2][320*400*3];   /* double buffer, generous for 640x400 */
static int      fw[2], fh[2], flines[2];
static volatile int  front = -1;
static int      back;

#define NKEYS 256
static volatile unsigned char keys[NKEYS];
static int      chq[64]; static volatile int chq_r, chq_w;

/* The core linked in at build time.  A module opened at run time replaces
 * these through corehost_use_module(); everything below goes through the
 * pointers so it cannot tell the two apart. */
const dxm_core_info *dxm_core_get_info(void);
int  dxm_core_main(const dxm_host *h,const char *dir);
void dxm_core_audio(int16_t *out,int frames);

static dxm_core_main_fn  f_main  = dxm_core_main;
static dxm_core_audio_fn f_audio = dxm_core_audio;

void corehost_use_module(const dxm_module *m){
    if(m && m->handle){ f_main=m->main_fn; f_audio=m->audio_fn; }
    else              { f_main=dxm_core_main; f_audio=dxm_core_audio; }
}

static double now_s(void){
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts);
    return ts.tv_sec + ts.tv_nsec*1e-9;
}
static void h_present(const dxm_frame *f){
    const dxm_mode *m=f->mode;
    int n=m->w*m->h;
    if((size_t)n*3 > sizeof rgb[0]) return;
    uint8_t *d=rgb[back];
    if(m->format==DXM_FB_INDEX8 && f->palette){
        const uint8_t *pal=f->palette;
        for(int i=0;i<n;i++){ const uint8_t *c=pal+f->pixels[i]*3;
                              d[i*3]=c[0]; d[i*3+1]=c[1]; d[i*3+2]=c[2]; }
    } else memcpy(d,f->pixels,(size_t)n*3);
    fw[back]=m->w; fh[back]=m->h; flines[back]=m->crt_lines;
    pthread_mutex_lock(&mu); front=back; back=1-back; pthread_mutex_unlock(&mu);
    /* SPEC 4.1: pace at the present boundary.  The game's wait-loops spin on
     * the 36.4Hz tick calling present each iteration; without this they
     * busy-burn a core publishing hundreds of identical frames.  A short
     * sleep keeps tick resolution (27ms period) while cooling the loop.
     * Game SPEED is unaffected either way - it is wall-clock tick driven. */
    usleep(2500);
}
static int    h_key_down(int sc){ return sc>=0&&sc<NKEYS ? keys[sc] : 0; }
static int    h_getch(void){
    if(chq_r==chq_w) return 0;
    int v=chq[chq_r]; chq_r=(chq_r+1)%64; return v;
}
static int    h_should_quit(void){ return quit_req; }
static double h_now(void){ return now_s(); }
static void   h_sleep(int ms){ usleep((useconds_t)ms*1000); }
static void   h_log(const char *m){ fprintf(stderr,"[core] %s\n",m); }

/* The core's one lock lives here, not in the core: the shell owns the
 * thread, and a core shipped as a loadable module must not link a threading
 * library of its own. */
static pthread_mutex_t game_mu = PTHREAD_MUTEX_INITIALIZER;
static void   h_lock(void)  { pthread_mutex_lock(&game_mu); }
static void   h_unlock(void){ pthread_mutex_unlock(&game_mu); }

static dxm_host HOST = {
    h_present, h_key_down, h_getch, h_should_quit, h_now, h_sleep, h_log,
    h_lock, h_unlock,
    NULL, NULL
};

static void *thread_main(void *ud){
    (void)ud;
    f_main(&HOST, cur_data);
    running=0;
    return NULL;
}
int corehost_start(const dxm_core_info *info,const char *data_dir){
    if(running) return -1;
    if(!info || info->abi!=DXM_ABI){
        fprintf(stderr,"[dxm] core ABI %d, this build speaks %d - refusing\n",
                info?info->abi:-1,DXM_ABI);
        return -1;
    }
    cur_info=info; snprintf(cur_data,sizeof cur_data,"%s",data_dir);
    /* plat_pref_path() is concatenated directly by callers (skyroads'
     * cfg_path() does "%sskyroads.cfg"), so it MUST end in a separator. */
    size_t n=strlen(cur_data);
    if(n && cur_data[n-1]!='/' && n+1<sizeof cur_pref)
        snprintf(cur_pref,sizeof cur_pref,"%s/",cur_data);
    else snprintf(cur_pref,sizeof cur_pref,"%s",cur_data);
    HOST.data_dir=cur_data; HOST.pref_dir=cur_pref;
    memset((void*)keys,0,sizeof keys); chq_r=chq_w=0;
    front=-1; back=0; quit_req=0; running=1;
    if(pthread_create(&th,NULL,thread_main,NULL)!=0){ running=0; return -1; }
    return 0;
}
void corehost_stop(void){
    if(!running && front<0) return;
    quit_req=1;
    for(int i=0;i<200 && running;i++) usleep(10000);   /* ~2s (PORTING §3.6) */
    pthread_join(th,NULL);
    running=0; front=-1; quit_req=0;
}
int corehost_running(void){ return running; }
const uint8_t *corehost_frame(int *w,int *h,int *lines){
    pthread_mutex_lock(&mu); int f=front; pthread_mutex_unlock(&mu);
    if(f<0) return NULL;
    *w=fw[f]; *h=fh[f]; *lines=flines[f];
    return rgb[f];
}
void corehost_push_key(int sc,int down,int ch){
    if(sc>=0&&sc<NKEYS) keys[sc]=(unsigned char)down;
    if(down&&ch){ int n=(chq_w+1)%64; if(n!=chq_r){ chq[chq_w]=ch; chq_w=n; } }
}
void corehost_audio(int16_t *out,int nframes){
    /* Only pull once the core has published a frame: before that it is still
     * inside audio_init, and rendering concurrently races the soundfont load.
     * The mutex closes the same window during teardown. */
    pthread_mutex_lock(&mu);
    int ok = running && front >= 0;
    if(ok) f_audio(out,nframes);
    else   memset(out,0,(size_t)nframes*4);
    pthread_mutex_unlock(&mu);
}
