/* theatre.c — see theatre.h. */
#include "theatre.h"
#include "sound.h"
#include "log.h"
#include <math.h>

/* The machine comes up out of the same black the splash left behind, so
 * the two read as one continuous power-on rather than a cut. */
static const double MACH_FADE=0.70;
static const double WARM=1.6;       /* the tube's warm-up, seconds */
static const double OFF_END=1.1;    /* power-off, from the switch to the end */

void theatre_power_on(theatre *th,int selftest,int deterministic){
    th->fade0=app_now_ns();
    th->off_t0=-1.0;
    th->drive_until=0.0;
    th->pwr=0.0f;
    th->selftest=selftest;
    th->deterministic=deterministic;
    snd_relay(); snd_degauss();
}

void theatre_power_off(theatre *th,double t){
    th->off_t0=t; snd_power(0); snd_relay();
    dxm_log("power off");
}

void theatre_drive(theatre *th,double seconds,double t){
    snd_floppy(seconds);
    if(t+seconds>th->drive_until) th->drive_until=t+seconds;
}

int theatre_frame(theatre *th,gpu *g,const dxm_layout *L,int W,int H,double t){
    int done=0;
    /* the tube's state this frame: warming up, steady, or dying */
    { float rh=1.0f, rv=1.0f, gain=1.0f;
      double fe=(app_now_ns()-th->fade0)/1e9;
      if(th->off_t0>=0.0){
          double o=t-th->off_t0;
          if(o<0.10){                       /* vertical collapse */
              float p=(float)(o/0.10); p=p*p;
              rv=1.0f-0.985f*p; gain=1.0f+1.4f*p;
          } else if(o<0.18){                /* the line shrinks to a dot */
              float p=(float)((o-0.10)/0.08);
              rv=0.015f; rh=1.0f-0.98f*p; gain=2.4f-0.6f*p;
          } else {                          /* the dot fades */
              float p=(float)((o-0.18)/0.45); if(p>1.0f) p=1.0f;
              rv=0.015f; rh=0.02f; gain=1.8f*(1.0f-p)*(1.0f-p);
          }
          if(o>=OFF_END) done=1;
      } else if(fe<WARM && !th->selftest){
          /* the raster opens quickly and then creeps the last of the
           * way, the way a cold tube settles: a cubic ease-OUT, all
           * the speed at the start and none at the end */
          float u=1.0f-(float)(fe/WARM);
          float p=1.0f-u*u*u;
          rh=0.96f+0.04f*p; rv=0.90f+0.10f*p; gain=p*p;
      }
      gpu_set_tube_power(g,rh,rv,gain); }

    /* GL's origin is bottom-left; chassis_render draws top-down. */
    /* The activity LED follows the drive, with a little flicker so it
     * reads as head movement rather than a steady lamp.  Its level comes
     * from the sound's own envelope, which the audio thread advances in
     * real time; under the fixed-step clock that would make the LED the
     * one thing in the frame that differs between runs, so there it is
     * simply on while the drive was asked to run. */
    { float lv = th->deterministic ? (t<th->drive_until ? 1.0f : 0.0f)
                                   : snd_floppy_level();
      float fl=0.72f+0.28f*(float)sin(t*47.0)*(float)sin(t*23.0);
      gpu_set_led(g,0, L->fdd_led[0]/W, 1.0f-(L->fdd_led[1]+L->fdd_led[3])/H,
                  L->fdd_led[2]/W, L->fdd_led[3]/H,
                  lv*fl, 0.16f,1.0f,0.22f, 0, 2.0f);
      /* power: steady, and it comes up with the machine */
      th->pwr += ((th->off_t0>=0.0?0.0f:1.0f)-th->pwr)*(th->off_t0>=0.0?0.25f:0.02f);
      gpu_set_led(g,1, L->pwr_led[0]/W, 1.0f-(L->pwr_led[1]+L->pwr_led[3])/H,
                  L->pwr_led[2]/W, L->pwr_led[3]/H,
                  th->pwr, 0.20f,1.0f,0.26f, 1,
                  1.0f-L->pwr_shelf/H); }
    return done;
}

void theatre_room(const theatre *th,gpu *g,double t){
    if(!th->selftest){
        double fe=(app_now_ns()-th->fade0)/1e9;
        if(fe<MACH_FADE){
            float a=(float)(1.0-fe/MACH_FADE);
            gpu_draw_fade(g,a*a*(3.0f-2.0f*a));
        }
    }
    if(th->off_t0>=0.0){
        /* the room goes dark after the tube has, not with it */
        double o=t-th->off_t0, f0=0.6;
        if(o>f0){ float a=(float)((o-f0)/(OFF_END-f0)); if(a>1.0f)a=1.0f;
                  gpu_draw_fade(g,a*a*(3.0f-2.0f*a)); }
    }
}
