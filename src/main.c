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
#include "dxm_core.h"
#include "crt.h"

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
static void audio_cb(void *ud,SDL_AudioStream *st,int add,int total){
    (void)ud;(void)total;
    if(add<=0) return;
    static int16_t buf[4096];
    int frames=add/4; if(frames>2048) frames=2048;
    corehost_audio(buf,frames);
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

int main(int argc,char **argv){
    int windowed=0, shot_frames=0, selftest=0; const char *shot=NULL; const char *autocmd=NULL;
    float ambient=0.4f;             /* room light: 0 dark room .. 1 bright */
    int win_w=1600, win_h=900;
    for(int i=1;i<argc;i++){
        if(!strcmp(argv[i],"--windowed")) windowed=1;
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
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);
    SDL_WindowFlags fl=SDL_WINDOW_OPENGL|SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if(!windowed) fl|=SDL_WINDOW_FULLSCREEN;
    SDL_Window *win=SDL_CreateWindow("DOS ex Machina",win_w,win_h,fl);
    if(!win){ fprintf(stderr,"window: %s\n",SDL_GetError()); return 1; }
    SDL_GLContext ctx=SDL_GL_CreateContext(win);
    if(!ctx){ fprintf(stderr,"GL context: %s\n",SDL_GetError()); return 1; }
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
    gpu *g=gpu_create(W,H);
    dxm_layout L=chassis_layout(W,H);
    uint8_t *chas=chassis_render(&L,W,H);
    gpu_set_chassis(g,chas,W,H);

    /* The core renders at 44100 Hz (skyroads audio.c SAMPLE_RATE).  The
     * stream must be opened at the CORE's rate - SDL3 resamples to whatever
     * the hardware wants.  Opening at 48000 played everything 8.8%% fast. */
    SDL_AudioSpec as={SDL_AUDIO_S16,2,44100};
    SDL_AudioStream *ast=SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&as,audio_cb,NULL);
    if(ast) SDL_ResumeAudioStreamDevice(ast);

    dos_init();

    gpu_knobs k={0.5f,1.0f,ambient,0.55f,0.45f,0.85f,DXM_WARP,0.015f,400};
    Uint64 t_start=SDL_GetTicksNS();
    int frame=0, quit=0;
    while(!quit){
        SDL_Event e;
        while(SDL_PollEvent(&e)){
            if(e.type==SDL_EVENT_QUIT) quit=1;
            else if(e.type==SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
                    e.type==SDL_EVENT_WINDOW_DISPLAY_CHANGED){
                int nw,nh; SDL_GetWindowSizeInPixels(win,&nw,&nh);
                if(nw>0 && nh>0 && (nw!=W || nh!=H)){
                    W=nw; H=nh;
                    gpu_resize(g,W,H);
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
                    if(e.key.key==SDLK_F5||e.key.key==SDLK_F6){
                        k.ambient+=(e.key.key==SDLK_F6)?0.05f:-0.05f;
                        if(k.ambient<0)k.ambient=0;
                        if(k.ambient>1)k.ambient=1;
                        fprintf(stderr,"[dxm] ambient = %.2f\n",k.ambient);
                    }
                    else if(e.key.key=='\r') dos_key('\r',sc);
                    else if(e.key.key==SDLK_BACKSPACE) dos_key('\b',sc);
                    else if(e.key.key>=32&&e.key.key<127) dos_key((int)e.key.key,sc);
                }
            }
        }
        double t=(SDL_GetTicksNS()-t_start)/1e9;
        if(selftest){
            static int stage=0, mark=0, runs=0, fail=0;
            int F=frame-mark;
            switch(stage){
            case 0: if(dos_update(t)==DOS_PROMPT){
                        for(const char *q="SKYROADS";*q;q++) dos_key(*q,0);
                        dos_key('\r',0); mark=frame; stage=1; }
                    break;
            case 1: if(F>260){
                        if(!corehost_running()){
                            printf("FAIL: core did not start (run %d)\n",runs+1); fail=1; stage=4; }
                        else { printf("  run %d: core running\n",runs+1); mark=frame; stage=2; }
                    } break;
            case 2: /* Esc: intro -> menu -> plat_exit -> longjmp -> unwind */
                    if(F%40==0)  corehost_push_key(DXM_SC_ESC,1,27);
                    if(F%40==20) corehost_push_key(DXM_SC_ESC,0,0);
                    if(!corehost_running()){
                        printf("  run %d: core unwound, host ALIVE (PORTING 3.1)\n",runs+1);
                        runs++; mark=frame; stage=(runs<2)?3:4;
                    } else if(F>700){ printf("FAIL: core never unwound\n"); fail=1; stage=4; }
                    break;
            case 3: if(F>90){
                        for(const char *q="SKYROADS";*q;q++) dos_key(*q,0);
                        dos_key('\r',0); mark=frame; stage=1; }
                    break;
            case 4: printf(fail?"SELFTEST FAIL\n"
                               :"SELFTEST PASS: launch, unwind, relaunch (PORTING 3.1 + 3.2)\n");
                    quit=1; break;
            }
        }

        /* core lifecycle */
        if(corehost_running()==0 && dos_update(t)==DOS_RUNNING) dos_core_exited();
        const char *req=dos_launch_request();
        if(req){
            const dxm_core_info *info=sky_core_info();
            const char *dd=getenv("DXM_DATA");
            corehost_start(info, dd?dd:"/Users/pedro/Git/skyroads-mac/data");
        }
        if(autocmd && dos_update(t)==DOS_PROMPT){       /* wait for the prompt */
            for(const char *q=autocmd;*q;q++) dos_key(*q,0);
            dos_key('\r',0); autocmd=NULL;
        }
        if(dos_update(t)==DOS_OFF) quit=1;

        /* pick the tube source: the running core, or the DOS text screen */
        int cw,ch,cl;
        const uint8_t *src=corehost_running()?corehost_frame(&cw,&ch,&cl):NULL;
        if(src){ gpu_set_tube(g,src,cw,ch); k.crt_lines=cl; }
        else   { gpu_set_tube(g,dos_render(),DOS_W,DOS_H); k.crt_lines=400; }

        /* GL's origin is bottom-left; chassis_render draws top-down. */
        gpu_draw(g, L.tube_x/W, 1.0f-(L.tube_y+L.tube_h)/H,
                    L.tube_w/W, L.tube_h/H, &k, t);
        SDL_GL_SwapWindow(win);
        frame++;
        if(shot && frame>=(shot_frames?shot_frames:60)){
            int rw,rh; uint8_t *px=gpu_readback(g,&rw,&rh);
            write_bmp(shot,px,rw,rh); free(px);
            fprintf(stderr,"wrote %s (%dx%d)\n",shot,rw,rh);
            quit=1;
        }
    }
    corehost_stop();
    SDL_Quit();
    return 0;
}
