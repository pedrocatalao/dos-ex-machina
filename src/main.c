/* main.c — the appliance.  One window, always fullscreen, no chrome.
 * --windowed and --shot are hidden dev flags (SPEC §11). */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gpu.h"
#include "dos.h"
#include "chassis.h"
#include "corehost.h"
#include "coreload.h"
#include "library.h"
#include "catalog.h"
#include "net.h"
#include "dxm_core.h"
#include "crt.h"
#include "sound.h"
#include "ui.h"
#include "dxm_splash.h"
#include "dxm_icon.h"
#include <math.h>
#include <stdarg.h>

/* Startup diagnostics go to stderr AND to dxm.log in the preferences
 * directory.  stderr alone is useless for the case that matters: a machine
 * where DXM was double-clicked and sits on the splash.  There is no console
 * to read, and the report that comes back is "it hangs".  The file says how
 * far it got and how long each step took, from either thread. */
static FILE *g_log;
static void dxm_log(const char *fmt,...){
    char line[1024]; va_list ap; va_start(ap,fmt);
    vsnprintf(line,sizeof line,fmt,ap); va_end(ap);
    unsigned long long ms=SDL_GetTicksNS()/1000000ull;
    fprintf(stderr,"[dxm] %6llu ms  %s\n",ms,line);
    if(g_log){ fprintf(g_log,"%6llu ms  %s\n",ms,line); fflush(g_log); }
}
static void gpu_log_cb(const char *m){ dxm_log("%s",m); }
#ifndef DXM_VERSION
#  define DXM_VERSION "dev"
#endif

static int sc_from_sdl(SDL_Scancode s){
    switch(s){
        case SDL_SCANCODE_ESCAPE: return DXM_SC_ESC;
        case SDL_SCANCODE_RETURN: return DXM_SC_ENTER;
        case SDL_SCANCODE_SPACE:  return DXM_SC_SPACE;
        case SDL_SCANCODE_UP:     return DXM_SC_UP;
        case SDL_SCANCODE_DOWN:   return DXM_SC_DOWN;
        case SDL_SCANCODE_LEFT:   return DXM_SC_LEFT;
        case SDL_SCANCODE_RIGHT:  return DXM_SC_RIGHT;
        case SDL_SCANCODE_F9:     return DXM_SC_F9;
        default: return 0;
    }
}
static FILE *g_audio_dump;   /* --dump-audio, dev verification */

static void audio_cb(void *ud,SDL_AudioStream *st,int add,int total){
    (void)ud;(void)total;
    if(add<=0) return;
    static int16_t buf[4096];
    int frames=add/4; if(frames>2048) frames=2048;
    corehost_audio(buf,frames);
    snd_mix(buf,frames);
    if(g_audio_dump){ fwrite(buf,4,(size_t)frames,g_audio_dump); }
    SDL_PutAudioStreamData(st,buf,frames*4);
}
static void write_bmp(const char *path,const uint8_t *rgb,int w,int h){
    FILE *f=fopen(path,"wb"); if(!f) return;
    int row=(w*3+3)&~3, sz=54+row*h;
    uint8_t hd[54]={0}; hd[0]='B';hd[1]='M';
    memcpy(hd+2,&sz,4); int off=54; memcpy(hd+10,&off,4);
    int ih=40; memcpy(hd+14,&ih,4); memcpy(hd+18,&w,4); memcpy(hd+22,&h,4);
    hd[26]=1; hd[28]=24; fwrite(hd,1,54,f);
    uint8_t pad[3]={0};
    for(int y=0;y<h;y++){                      /* GL readback is bottom-up */
        for(int x=0;x<w;x++){ const uint8_t *p=rgb+((size_t)y*w+x)*3;
                              uint8_t bgr[3]={p[2],p[1],p[0]}; fwrite(bgr,1,3,f); }
        fwrite(pad,1,row-w*3,f);
    }
    fclose(f);
}

/* chassis_render() is the one genuinely slow thing at startup - a few
 * million pixels of signed-distance work - and it touches no GL, so it runs
 * on a worker while the main thread holds the splash up.  Doing it inline
 * would freeze the fade for its whole duration. */
/* The worker reports completion itself, through `done`; that also covers
 * the no-threads fallback, where the work has already happened inline. */
typedef struct { int W,H; dxm_layout L; uint8_t *px;
                 volatile int done; Uint64 ms; } chassis_job;
static int SDLCALL chassis_worker(void *ud){
    chassis_job *j=(chassis_job *)ud;
    Uint64 t0=SDL_GetTicksNS();
    dxm_log("chassis worker: start, %dx%d",j->W,j->H);
    j->L=chassis_layout(j->W,j->H);
    dxm_log("chassis worker: layout done");
    j->px=chassis_render(&j->L,j->W,j->H);
    j->ms=(SDL_GetTicksNS()-t0)/1000000;
    dxm_log("chassis worker: render %s in %llu ms",
            j->px?"done":"FAILED - no pixels",(unsigned long long)j->ms);
    j->done=1;
    return 0;
}

int main(int argc,char **argv){
    int windowed=0, shot_frames=0, selftest=0, quit_early=0; const char *shot=NULL; const char *autocmd=NULL;
    float ambient=0.4f;             /* room light: 0 dark room .. 1 bright */
    int win_w=1600, win_h=900;
    for(int i=1;i<argc;i++){
        if(!strcmp(argv[i],"--dump-audio")&&i+1<argc)
            g_audio_dump=fopen(argv[++i],"wb");
        else if(!strcmp(argv[i],"--windowed")) windowed=1;
        else if(!strcmp(argv[i],"--selftest")){ selftest=1; windowed=1; }
        else if(!strcmp(argv[i],"--shot")&&i+1<argc) shot=argv[++i];   /* honours fullscreen */
        else if(!strcmp(argv[i],"--frames")&&i+1<argc) shot_frames=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--type")&&i+1<argc) autocmd=argv[++i];
        else if(!strcmp(argv[i],"--size")&&i+1<argc) sscanf(argv[++i],"%dx%d",&win_w,&win_h);
        else if(!strcmp(argv[i],"--ambient")&&i+1<argc){ ambient=(float)atof(argv[++i]);
            if(ambient<0)ambient=0; if(ambient>1)ambient=1; }
    }
    if(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO)){
        fprintf(stderr,"SDL_Init: %s\n",SDL_GetError()); return 1; }
    const char *pref=SDL_GetPrefPath("DOSexMachina","dxm");
    { char lp[1024]; snprintf(lp,sizeof lp,"%sdxm.log",pref?pref:"./");
      g_log=fopen(lp,"w"); }
    { int v=SDL_GetVersion();
      dxm_log("DOS ex Machina " DXM_VERSION " on %s, SDL %d.%d.%d",SDL_GetPlatform(),
              SDL_VERSIONNUM_MAJOR(v),SDL_VERSIONNUM_MINOR(v),SDL_VERSIONNUM_MICRO(v)); }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);
    SDL_WindowFlags fl=SDL_WINDOW_OPENGL|SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if(!windowed) fl|=SDL_WINDOW_FULLSCREEN;
    SDL_Window *win=SDL_CreateWindow("DOS ex Machina",win_w,win_h,fl);
    if(!win){
        dxm_log("window: %s",SDL_GetError());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"DOS ex Machina",
                                 SDL_GetError(),NULL);
        return 1;
    }
    /* The window/taskbar icon.  Windows also carries one as a resource for
     * Explorer to show on the .exe, and macOS gets it from the bundle - this
     * is what Linux has, and it costs nothing on the others. */
    { SDL_Surface *ic=SDL_CreateSurfaceFrom(DXM_ICON_W,DXM_ICON_HT,
                                            SDL_PIXELFORMAT_RGBA32,
                                            (void *)dxm_icon,DXM_ICON_W*4);
      if(ic){ SDL_SetWindowIcon(win,ic); SDL_DestroySurface(ic); } }

    SDL_GLContext ctx=SDL_GL_CreateContext(win);
    if(!ctx){
        /* The most likely failure on a machine that cannot run DXM at all:
         * no OpenGL 3.3 core context to be had.  Say so on screen. */
        char msg[600];
        snprintf(msg,sizeof msg,"Could not create an OpenGL 3.3 core context, "
                 "which DOS ex Machina needs.\n\n%s",SDL_GetError());
        dxm_log("%s",msg);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"DOS ex Machina",msg,win);
        return 1;
    }
    SDL_GL_SetSwapInterval(1);
    SDL_HideCursor();                       /* no OS chrome, ever (SPEC §2.1) */

    if(!windowed){
        SDL_SetWindowFullscreenMode(win,NULL);   /* NULL = desktop mode */
        SDL_SetWindowFullscreen(win,true);
        SDL_SyncWindow(win);                     /* the transition is ASYNC on
                                                  * macOS; measuring before it
                                                  * settles lays the machine
                                                  * out for the wrong size */
    }
    /* HiDPI: size from the DRAWABLE, never the window (SPEC §6.3) */
    int W=0,H=0; SDL_GetWindowSizeInPixels(win,&W,&H);
    /* mouse arrives in WINDOW units; the panel works in drawable px */
    float win_wf=1,win_hf=1;
    { int ww,wh; SDL_GetWindowSize(win,&ww,&wh);
      win_wf=(float)(ww>0?ww:W); win_hf=(float)(wh>0?wh:H);
      dxm_log("window %dx%d px (%dx%d units), %s",W,H,ww,wh,
              windowed?"windowed":"fullscreen"); }
    gpu_set_log(gpu_log_cb);
    gpu *g=gpu_create(W,H);
    if(!g){
        /* Say so where it will be seen.  A person who double-clicked the
         * program has no stderr; a message box they have. */
        char msg[1400];
        snprintf(msg,sizeof msg,
                 "This computer's OpenGL driver does not provide OpenGL 3.3 "
                 "core, which DOS ex Machina needs.\n\nDriver: %s\nMissing: %s",
                 gpu_describe(),gpu_missing());
        dxm_log("%s",msg);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"DOS ex Machina",msg,win);
        return 1;
    }
    dxm_log("GL: %s",gpu_describe());

    /* The machine is switched on before it is drawn: the splash and the
     * sound of it running come up first, and the chassis is built behind
     * them. */
    { uint8_t *spl=dxm_splash_rgba();
      if(spl){ gpu_set_splash(g,spl,DXM_SPLASH_W,DXM_SPLASH_HT); free(spl); } }

    /* The core renders at 44100 Hz (skyroads audio.c SAMPLE_RATE).  The
     * stream must be opened at the CORE's rate - SDL3 resamples to whatever
     * the hardware wants.  Opening at 48000 played everything 8.8%% fast. */
    SDL_AudioSpec as={SDL_AUDIO_S16,2,44100};
    SDL_AudioStream *ast=SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&as,audio_cb,NULL);
    if(ast) SDL_ResumeAudioStreamDevice(ast);
    if(ast) dxm_log("audio stream open");
    else    dxm_log("audio: no device (%s) - silent",SDL_GetError());
    snd_init(44100);
    snd_power(1);                 /* fans and spindle spin up, immediately */

    chassis_job job={W,H,{0},NULL,0,0};
    SDL_Thread *cth=SDL_CreateThread(chassis_worker,"chassis",&job);
    if(!cth){
        dxm_log("no worker thread (%s) - drawing inline",SDL_GetError());
        chassis_worker(&job);
    }

    if(!selftest){
        /* Fast in, hold until the machine is ready, then out.  The hold has
         * a floor so a quick build does not flash the mark up and away. */
        const double FADE_IN=0.30, HOLD_MIN=0.95, FADE_OUT=0.45;
        Uint64 s0=SDL_GetTicksNS();
        int frames=0; double beat=0;
        for(;;){
            SDL_Event se; while(SDL_PollEvent(&se)) if(se.type==SDL_EVENT_QUIT) quit_early=1;
            double e=(SDL_GetTicksNS()-s0)/1e9;
            float a=(float)(e<FADE_IN ? e/FADE_IN : 1.0);
            gpu_draw_splash(g,a*a*(3.0f-2.0f*a));   /* ease, no linear ramp */
            SDL_GL_SwapWindow(win);
            if(++frames==1) dxm_log("splash: first frame on screen");
            /* a heartbeat, so a log from a machine that never leaves the
             * splash shows whether the swaps come back and the worker ends */
            if(e-beat>=2.0){ beat=e;
                dxm_log("splash: %.1f s, %d frames, worker %s",e,frames,
                        job.done?"done":"still running"); }
            if(quit_early) break;
            if(e>=HOLD_MIN && job.done) break;
        }
        dxm_log("splash: over after %.2f s, %d frames",(SDL_GetTicksNS()-s0)/1e9,frames);
        Uint64 f0=SDL_GetTicksNS();
        while(!quit_early){
            SDL_Event se; while(SDL_PollEvent(&se)) if(se.type==SDL_EVENT_QUIT) quit_early=1;
            double e=(SDL_GetTicksNS()-f0)/1e9;
            if(e>=FADE_OUT) break;
            float a=(float)(1.0-e/FADE_OUT);
            gpu_draw_splash(g,a*a*(3.0f-2.0f*a));
            SDL_GL_SwapWindow(win);
        }
    }
    /* The machine comes up out of the same black the splash left behind,
     * so the two reads as one continuous power-on rather than a cut. */
    Uint64 mach_fade0=SDL_GetTicksNS();
    const double MACH_FADE=0.70;
    if(cth) SDL_WaitThread(cth,NULL);
    dxm_log("chassis %dx%d joined, %llu ms%s",
            W,H,(unsigned long long)job.ms, job.px?"":"  (FAILED - no pixels)");
    dxm_layout L=job.L;
    uint8_t *chas=job.px;
    gpu_set_chassis(g,chas,W,H);
    dxm_log("chassis uploaded");


    /* brightness, contrast, bloom, burn_in, noise, jitter, glow_line,
     * ambient, flicker, hsync, rgb_shift, chassis_glow, persistence,
     * scan, vgrid, sharp_text, warp, margin, overscan, aperture_r,
     * crt_lines, crt_cols */
    /* The imperfection effects all default to ZERO: a machine in good
     * repair.  Non-zero h-sync and RGB shift in particular put every
     * character at a different sub-pixel phase, which reads as the same
     * glyph having a thin left edge here and a thin right edge there. */
    gpu_knobs k={0.5f, 1.0f, 0.55f, 0.0f, 0.0f, 0.0f, 0.0f,
                 ambient, 0.0f, 0.0f, 0.0f, 0.55f, 0.45f,
                 0.62f, 0.20f, 1.0f, DXM_WARP, 0.0f, 1.0f, 0.0f, 400, DOS_W};

    ui_init(&k);
    static char cfgpath[1024];
    snprintf(cfgpath,sizeof cfgpath,"%scrt.cfg",pref?pref:"./");
    ui_load(cfgpath);
    /* What DXM can run is whatever is installed, not what it was built
     * with.  Scanning here means the prompt and the navigator agree about
     * the machine's contents from the first frame. */
    lib_scan();
    dxm_log("library scanned: %d installed",lib_count());
    /* The cached catalogue first, so the navigator is populated instantly
     * and works with no network at all; then a refresh in the background. */
    cat_load_cached();
    cat_refresh_begin();
    dxm_log("catalogue: cache read, refresh started");
    for(int i=0;i<lib_count();i++){
        const lib_game *g=lib_at(i);
        dxm_log("game %-12s %s",g->id,
                g->ready?"ready":(g->note[0]?g->note:"not ready"));
    }
    if(lib_count()==0)
        dxm_log("no games installed - %sgames",lib_root());
    dos_init();
    dxm_log("dos ready, entering the frame loop");

    Uint64 t_start=SDL_GetTicksNS();
    int frame=0, quit=quit_early;
    while(!quit){
        SDL_Event e;
        while(SDL_PollEvent(&e)){
            if(e.type==SDL_EVENT_QUIT) quit=1;
            else if(e.type==SDL_EVENT_MOUSE_BUTTON_DOWN)
                ui_mouse((int)(e.button.x*W/win_wf),(int)(e.button.y*H/win_hf),1,0);
            else if(e.type==SDL_EVENT_MOUSE_BUTTON_UP)
                ui_mouse((int)(e.button.x*W/win_wf),(int)(e.button.y*H/win_hf),0,0);
            else if(e.type==SDL_EVENT_MOUSE_MOTION)
                ui_mouse((int)(e.motion.x*W/win_wf),(int)(e.motion.y*H/win_hf),
                         (e.motion.state&SDL_BUTTON_LMASK)?1:0,1);
            else if(e.type==SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
                    e.type==SDL_EVENT_WINDOW_DISPLAY_CHANGED){
                int nw,nh; SDL_GetWindowSizeInPixels(win,&nw,&nh);
                if(nw>0 && nh>0 && (nw!=W || nh!=H)){
                    W=nw; H=nh;
                    gpu_resize(g,W,H);
                    { int ww,wh; SDL_GetWindowSize(win,&ww,&wh);
                      win_wf=(float)(ww>0?ww:W); win_hf=(float)(wh>0?wh:H); }
                    L=chassis_layout(W,H);
                    free(chas); chas=chassis_render(&L,W,H);
                    gpu_set_chassis(g,chas,W,H);
                }
            }
            else if(e.type==SDL_EVENT_KEY_DOWN||e.type==SDL_EVENT_KEY_UP){
                int down=(e.type==SDL_EVENT_KEY_DOWN);
                int sc=sc_from_sdl(e.key.scancode);
                if(corehost_running()){
                    int ch=0;
                    if(sc==DXM_SC_ESC) ch=27; else if(sc==DXM_SC_ENTER) ch=13;
                    else if(sc==DXM_SC_SPACE) ch=' ';
                    else if(sc) ch=0x100|sc;
                    corehost_push_key(sc,down,ch);
                } else if(down){
                    /* dev-only room-light adjust while at the prompt:
                     * F5 darker, F6 brighter (the real fiction control is a
                     * chassis knob, SPEC 6.8 - this is for tuning taste) */
                    if(e.key.key==SDLK_F1){ ui_toggle(); }
                    else if(e.key.key==SDLK_F5||e.key.key==SDLK_F6){
                        k.ambient+=(e.key.key==SDLK_F6)?0.05f:-0.05f;
                        if(k.ambient<0)k.ambient=0;
                        if(k.ambient>1)k.ambient=1;
                        dxm_log("ambient = %.2f",k.ambient);
                    }
                    else if(e.key.key=='\r') dos_key('\r',sc);
                    else if(e.key.key==SDLK_BACKSPACE) dos_key('\b',sc);
                    else if(e.key.key>=32&&e.key.key<127) dos_key((int)e.key.key,sc);
                    /* the navigator is driven by keys that carry no
                     * character at all - without this the prompt never
                     * hears an arrow or an Esc */
                    else if(sc) dos_key(0,sc);
                }
            }
        }
        double t=(SDL_GetTicksNS()-t_start)/1e9;
        if(selftest){
            /* Timed in SECONDS, not frames.  Everything this waits on is
             * wall-clock - the launch holds 2.3s while the drive reads, the
             * intro runs at its own pace - so a frame-count timeout makes
             * the test pass or fail on how fast the machine draws.  Windowed
             * with no true vsync that is hundreds of frames a second, and
             * the window closed before the launch had even fired. */
            static int stage=0, runs=0, fail=0;
            static double mark=-1.0;
            if(mark<0.0) mark=t;
            double E=t-mark;                      /* seconds in this stage */
            switch(stage){
            /* Games live in C:\GAMES and run from there, so the test has to
             * walk there like a user would. */
            case 0: if(dos_update(t)==DOS_PROMPT){
                        for(const char *q="CD GAMES";*q;q++) dos_key(*q,0);
                        dos_key('\r',0);
                        for(const char *q="SKYROADS";*q;q++) dos_key(*q,0);
                        dos_key('\r',0); mark=t; stage=1; }
                    break;
            case 1: if(E>4.0){                    /* > the 2.3s load pause */
                        if(!corehost_running()){
                            printf("FAIL: core did not start (run %d)\n",runs+1); fail=1; stage=4; }
                        else { printf("  run %d: core running\n",runs+1); mark=t; stage=2; }
                    } break;
            case 2: /* Esc: intro -> menu -> plat_exit -> longjmp -> unwind */
                    { int phase=(int)(E/0.33);
                      corehost_push_key(DXM_SC_ESC,(phase&1)==0,(phase&1)?0:27); }
                    if(!corehost_running()){
                        printf("  run %d: core unwound, host ALIVE (PORTING 3.1)\n",runs+1);
                        runs++; mark=t; stage=(runs<2)?3:4;
                    } else if(E>12.0){ printf("FAIL: core never unwound\n"); fail=1; stage=4; }
                    break;
            case 3: if(E>1.5){
                        /* the prompt comes back where the game left it, so
                         * this is already C:\GAMES */
                        for(const char *q="SKYROADS";*q;q++) dos_key(*q,0);
                        dos_key('\r',0); mark=t; stage=1; }
                    break;
            case 4: printf(fail?"SELFTEST FAIL\n"
                               :"SELFTEST PASS: launch, unwind, relaunch (PORTING 3.1 + 3.2)\n");
                    quit=1; break;
            }
        }

        /* core lifecycle */
        /* Only return to the prompt if a core was ACTUALLY started and has
         * since exited.  DOS_RUNNING also covers the loading pause before a
         * launch, and testing the state alone aborted the launch instantly. */
        static int core_started=0;
        if(core_started && !corehost_running()){
            core_started=0;
            dos_core_exited();
        }
        const char *req=dos_launch_request();
        if(req){
            const lib_game   *g=lib_find(req);
            const dxm_module *m=g?lib_module(g):NULL;
            if(m){
                snd_floppy(2.2);      /* the drive works while it loads */
                corehost_use_module(m);
                /* DXM_DATA still overrides, for working on a port without
                 * installing it first. */
                const char *dd=getenv("DXM_DATA");
                if(corehost_start(m->info, dd?dd:g->data)==0)
                    core_started=1;
            }
            if(!core_started) dos_core_failed();
        }
        if(dos_take_beep()) snd_beep(240.0);  /* after the RAM check */
        { double f=dos_take_floppy(); if(f>0.0) snd_floppy(f); }
        /* --type takes a ';'-separated list, typed one per return to the
         * prompt - so a sequence like "CD GAMES;DIR" can be driven. */
        if(autocmd && *autocmd && dos_update(t)==DOS_PROMPT){
            const char *e=strchr(autocmd,';');
            const char *end=e?e:autocmd+strlen(autocmd);
            for(const char *q=autocmd;q<end;q++) dos_key(*q,0);
            dos_key('\r',0);
            autocmd = e ? e+1 : NULL;
        }
        if(dos_update(t)==DOS_OFF){ snd_power(0); quit=1; }

        /* pick the tube source: the running core, or the DOS text screen */
        int cw,ch,cl;
        const uint8_t *src=corehost_running()?corehost_frame(&cw,&ch,&cl):NULL;
        if(src){ gpu_set_tube(g,src,cw,ch); k.crt_lines=cl; k.crt_cols=cw;
                 k.sharp_text=0.0f; }        /* game art: hard pixels */
        else   { gpu_set_tube(g,dos_render(),DOS_W,DOS_H);
                 k.crt_lines=400; k.crt_cols=DOS_W;
                 k.sharp_text=1.0f; }        /* text: even stroke weights */

        /* GL's origin is bottom-left; chassis_render draws top-down. */
        /* the activity LED follows the drive, with a little flicker so it
         * reads as head movement rather than a steady lamp */
        { /* floppy activity: flickers with head movement */
          float lv=snd_floppy_level();
          float fl=0.72f+0.28f*(float)sin(t*47.0)*(float)sin(t*23.0);
          gpu_set_led(g,0, L.fdd_led[0]/W, 1.0f-(L.fdd_led[1]+L.fdd_led[3])/H,
                      L.fdd_led[2]/W, L.fdd_led[3]/H,
                      lv*fl, 0.16f,1.0f,0.22f, 0);
          /* power: steady, and it comes up with the machine */
          static float pwr=0.0f;
          pwr += ((quit?0.0f:1.0f)-pwr)*0.02f;
          gpu_set_led(g,1, L.pwr_led[0]/W, 1.0f-(L.pwr_led[1]+L.pwr_led[3])/H,
                      L.pwr_led[2]/W, L.pwr_led[3]/H,
                      pwr, 0.20f,1.0f,0.26f, 1); }
        k.aperture_r = L.aperture_r;      /* match the chassis hole */
        gpu_draw(g, L.tube_x/W, 1.0f-(L.tube_y+L.tube_h)/H,
                    L.tube_w/W, L.tube_h/H, &k, t);
        { int ow,oh;
          const uint8_t *ov=ui_render(W,H,&ow,&oh);
          if(ov){ gpu_set_overlay(g,ov,ow,oh); gpu_draw_overlay(g); }
        if(!selftest){
            double fe=(SDL_GetTicksNS()-mach_fade0)/1e9;
            if(fe<MACH_FADE){
                float a=(float)(1.0-fe/MACH_FADE);
                gpu_draw_fade(g,a*a*(3.0f-2.0f*a));
            }
        }

          if(ui_visible()) SDL_ShowCursor(); else SDL_HideCursor(); }
        SDL_GL_SwapWindow(win);
        frame++;
        if(frame==1) dxm_log("first machine frame on screen");
        if(shot && frame>=(shot_frames?shot_frames:60)){
            int rw,rh; uint8_t *px=gpu_readback(g,&rw,&rh);
            write_bmp(shot,px,rw,rh); free(px);
            fprintf(stderr,"wrote %s (%dx%d)\n",shot,rw,rh);
            quit=1;
        }
    }
    ui_save(cfgpath);
    corehost_stop();
    SDL_Quit();
    return 0;
}
