/* sound.c — procedural machine ambience.  No samples: everything is
 * synthesised, so it costs nothing to ship and scales to any rate. */
#include "sound.h"
#include <math.h>
#include <string.h>

static int    SR = 44100;

/* deterministic noise source — same on every platform (SPEC §6.7 in spirit) */
static uint32_t rng = 0x1234567u;
static float nrand(void){
    rng ^= rng<<13; rng ^= rng>>17; rng ^= rng<<5;
    return (float)((int32_t)rng) / 2147483648.0f;
}

/* ---- PC speaker ---------------------------------------------------------
 * The 8253 channel 2 drove the cone with a raw SQUARE wave; the BIOS loaded
 * divisor 0x533, so 1193182/1331 = 896.5 Hz. */
#define PCSPK_HZ (1193182.0/1331.0)
static volatile int spk_left, spk_total;
static double       spk_phase;

/* ---- machine hum --------------------------------------------------------
 * Measured from a reference recording of a real desktop PC humming
 * (Goertzel over the steady section):
 *
 *   partials   293 Hz  0 dB | 213 -6 | 164 -8 | 178 -8 | 129 -15
 *              331 -16 | 351 -17 | 421 -19
 *   bands      125-500 Hz dominates; 2-8 kHz sits 20-34 dB down
 *
 * So the sound is a TONAL CLUSTER in the low mids - fan blade tones, the
 * transformer and the spindle beating against each other - not the
 * broadband hiss this replaces.  Nearby partials (164/178) beat slowly,
 * which is what gives it life. */
/* Three distinct sources, because they behave differently:
 *
 *  BODY HUM  low-mid cluster around 294 Hz - the case and transformer.
 *            Quiet; it is the floor, not the sound.
 *  SPINDLE   a 3.5" drive's audible whine lives in the KILOHERTZ, not down
 *            near its 60 Hz rotation - what you hear is high-order bearing
 *            and platter noise.  This is the "disk" sound.
 *  FAN       broadband air, centred well above the body hum, and it FADES
 *            IN as the blades come up to speed rather than appearing.      */
#define NHUM 3
static const float hum_hz[NHUM]  = {213.0f, 294.0f, 398.0f};
static const float hum_amp[NHUM] = {0.30f, 1.00f, 0.16f};

#define NWHINE 4
static const float whine_hz[NWHINE]  = {2640.0f, 3115.0f, 4180.0f, 5220.0f};
static const float whine_amp[NWHINE] = {1.00f, 0.58f, 0.30f, 0.13f};

static double hum_ph[NHUM], hum_drift[NHUM];
static double wh_ph[NWHINE], wh_drift[NWHINE];
static float  fan_lp1, fan_lp2, fan_hp, hiss_lp;
static float  spin = 0.0f;        /* spindle: 0 stopped .. 1 at speed */
static float  fan_env = 0.0f;     /* fan blades, slower to come up     */
static int    powered = 0;

void snd_init(int rate){ SR = rate>0?rate:44100; }
void snd_power(int on){ powered = on; }
void snd_beep(double ms){
    spk_total = spk_left = (int)(SR*ms/1000.0);
    spk_phase = 0.0;
}
void snd_disk(double seconds){ (void)seconds; }   /* head seeks removed */

/* ---- 3.5" floppy drive --------------------------------------------------
 * SAMPLED, not synthesised.  The fan and hum are steady textures that must
 * sweep with spin-up and track the ambient knob, so synthesis is right for
 * them; a drive read is irregular mechanical noise that only plays in short
 * bursts, and three attempts at synthesising it never stopped sounding like
 * a buzz or a whistle.  The clip is baked into the binary (see
 * tools/mkpcm.py) so nothing is loaded at runtime. */
#include "fdd_pcm.h"
static double fdd_left;          /* seconds of activity remaining */
static float  fdd_env;
static double fdd_pos;           /* playback position, fractional */
static float  fdd_lp;            /* low shelf for a heavier drive  */

void snd_floppy(double seconds){
    if(seconds > fdd_left){
        if(fdd_left <= 0.0) fdd_pos = 0.0;   /* start of a fresh access */
        fdd_left = seconds;
    }
}
float snd_floppy_level(void){ return fdd_env; }


void snd_mix(int16_t *out,int nframes){
    double sr = SR;
    for(int i=0;i<nframes;i++){
        float s = 0.0f;
        float target = powered ? 1.0f : 0.0f;
        spin    += (target-spin)    * (float)(1.0/(sr*2.2));  /* platter  */
        fan_env += (target-fan_env) * (float)(1.0/(sr*4.0));  /* fade in  */

        if(spin > 0.002f){
            /* --- body hum --- */
            float body = 0.0f;
            for(int k=0;k<NHUM;k++){
                hum_drift[k] += (0.09 + 0.031*k)/sr;
                float det = 1.0f + 0.0018f*(float)sin(hum_drift[k]*6.28318530718);
                hum_ph[k] += (hum_hz[k]*det*spin)/sr;
                body += hum_amp[k]*(float)sin(hum_ph[k]*6.28318530718);
            }
            s += body * 0.012f * spin;

            /* --- spindle whine: the actual disk sound, up in the kHz --- */
            float wh = 0.0f;
            for(int k=0;k<NWHINE;k++){
                wh_drift[k] += (0.23 + 0.07*k)/sr;
                float det = 1.0f + 0.0022f*(float)sin(wh_drift[k]*6.28318530718);
                /* pitch rises with the platter, so spin-up sweeps upward */
                wh_ph[k] += (whine_hz[k]*det*(0.55f+0.45f*spin))/sr;
                wh += whine_amp[k]*(float)sin(wh_ph[k]*6.28318530718);
            }
            s += wh * 0.0042f * spin*spin;
        }

        if(fan_env > 0.002f){
            /* --- fan: airy broadband, one pole at ~2.2 kHz, plus a little
             * body so it is not pure hiss --- */
            float n = nrand();
            fan_lp1 += (n       - fan_lp1)*0.150f;
            fan_lp2 += (fan_lp1 - fan_lp2)*0.150f;
            fan_hp  += (fan_lp2 - fan_hp )*0.010f;      /* DC / rumble trap */
            s += (fan_lp2 - fan_hp) * 0.034f * fan_env;
            hiss_lp += (n - hiss_lp)*0.55f;
            s += hiss_lp * 0.0025f * fan_env;
        }

        /* --- 3.5" floppy drive (sampled) --- */
        if(fdd_left > 0.0 || fdd_env > 0.001f){
            if(fdd_left > 0.0) fdd_left -= 1.0/sr;
            float want = (fdd_left > 0.0) ? 1.0f : 0.0f;
            fdd_env += (want-fdd_env) * (float)(1.0/(sr*0.045));

            /* Linear resample, looping seamlessly.  Playing the clip back
             * slightly slow drops its pitch - the mechanical way a bigger,
             * heavier drive sounds. */
            double step = (double)FDD_PCM_RATE / sr * 0.84;
            int i0 = (int)fdd_pos;
            double fr = fdd_pos - i0;
            int i1 = i0+1; if(i1>=FDD_PCM_LEN) i1=0;
            float v = (float)(fdd_pcm[i0]*(1.0-fr) + fdd_pcm[i1]*fr) / 32768.0f;
            fdd_pos += step;
            if(fdd_pos >= FDD_PCM_LEN) fdd_pos -= FDD_PCM_LEN;

            /* and a gentle low shelf: keep the body, ease off the top */
            fdd_lp += (v - fdd_lp)*0.34f;
            s += (fdd_lp*0.75f + v*0.25f) * 0.60f * fdd_env;
        }

        /* --- PC speaker on top --- */
        if(spk_left > 0){
            float env = 1.0f;
            int done = spk_total - spk_left;
            if(done < SR/200)     env = done/(float)(SR/200);
            if(spk_left < SR/73)  env = fminf(env, spk_left/(float)(SR/73));
            float sq = (spk_phase - floor(spk_phase) < 0.5) ? 1.0f : -1.0f;
            s += sq * env * 0.16f;
            spk_phase += PCSPK_HZ/sr;
            spk_left--;
        }

        int v = (int)(s * 32767.0f);
        int l = out[i*2]   + v; if(l>32767)l=32767; if(l<-32768)l=-32768;
        int r = out[i*2+1] + v; if(r>32767)r=32767; if(r<-32768)r=-32768;
        out[i*2]   = (int16_t)l;
        out[i*2+1] = (int16_t)r;
    }
}
