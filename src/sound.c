/* sound.c — procedural machine ambience.  No samples: everything is
 * synthesised, so it costs nothing to ship and scales to any rate. */
#include "sound.h"
#include <math.h>
#include <string.h>

#define NHUM 3
#define NWHINE 4
/* The machine's sound: the generators and their envelopes, mixed on the audio thread */
static struct {
    int rate;
    /* deterministic noise source — same on every platform (SPEC §6.7 in spirit) */
    uint32_t rng;
    volatile int spk_left, spk_total;
    double spk_phase;
    double hum_ph[NHUM], hum_drift[NHUM];
    double wh_ph[NWHINE], wh_drift[NWHINE];
    float fan_lp1, fan_lp2, fan_hp, hiss_lp;
    float spin;   /* spindle: 0 stopped .. 1 at speed */
    float fan_env;   /* fan blades, slower to come up     */
    int powered;
    /* one-shots: the switch's clack and the degauss coil's thump */
    double rel_t, dg_t;   /* seconds since fired, <0 idle */
    float rel_lp;
    double fdd_left;   /* seconds of activity remaining */
    float fdd_env;
    double fdd_pos;   /* playback position, fractional */
    float fdd_lp;   /* low shelf for a heavier drive  */
} snd = { .rate=44100, .rng=0x1234567u, .rel_t=-1.0, .dg_t=-1.0 };

static float nrand(void){
    snd.rng ^= snd.rng<<13; snd.rng ^= snd.rng>>17; snd.rng ^= snd.rng<<5;
    return (float)((int32_t)snd.rng) / 2147483648.0f;
}

/* ---- PC speaker ---------------------------------------------------------
 * The 8253 channel 2 drove the cone with a raw SQUARE wave; the BIOS loaded
 * divisor 0x533, so 1193182/1331 = 896.5 Hz. */
#define PCSPK_HZ (1193182.0/1331.0)

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
static const float hum_hz[NHUM]  = {213.0f, 294.0f, 398.0f};
static const float hum_amp[NHUM] = {0.30f, 1.00f, 0.16f};

static const float whine_hz[NWHINE]  = {2640.0f, 3115.0f, 4180.0f, 5220.0f};
static const float whine_amp[NWHINE] = {1.00f, 0.58f, 0.30f, 0.13f};


void snd_init(int rate){ snd.rate = rate>0?rate:44100; }
void snd_power(int on){ snd.powered = on; }
void snd_relay(void){ snd.rel_t = 0.0; }
void snd_degauss(void){ snd.dg_t = 0.0; }
void snd_beep(double ms){
    snd.spk_total = snd.spk_left = (int)(snd.rate*ms/1000.0);
    snd.spk_phase = 0.0;
}
void snd_disk(double seconds){ (void)seconds; }   /* head seeks removed */

/* ---- 3.5" floppy drive --------------------------------------------------
 * SAMPLED, not synthesised.  The fan and hum are steady textures that must
 * sweep with spin-up and track the ambient knob, so synthesis is right for
 * them; a drive read is irregular mechanical noise that only plays in short
 * bursts, and three attempts at synthesising it never stopped sounding like
 * a buzz or a whistle.  The clip is baked into the binary (see
 * tools/mkpcm.py) so nothing is loaded at runtime. */
#include "gen/fdd_pcm.h"

void snd_floppy(double seconds){
    if(seconds > snd.fdd_left){
        if(snd.fdd_left <= 0.0) snd.fdd_pos = 0.0;   /* start of a fresh access */
        snd.fdd_left = seconds;
    }
}
float snd_floppy_level(void){ return snd.fdd_env; }


void snd_mix(int16_t *out,int nframes){
    double sr = snd.rate;
    for(int i=0;i<nframes;i++){
        float s = 0.0f;
        float target = snd.powered ? 1.0f : 0.0f;
        /* up slowly - a fan takes seconds to reach speed - but down faster:
         * with the mains gone the platter coasts and the blades stop */
        snd.spin    += (target-snd.spin)    * (float)(1.0/(sr*(snd.powered?2.2:1.2)));
        snd.fan_env += (target-snd.fan_env) * (float)(1.0/(sr*(snd.powered?4.0:1.5)));

        /* --- the switch: a sharp clack, a little ring, a low thud --- */
        if(snd.rel_t >= 0.0){
            float n = nrand();
            snd.rel_lp += (n - snd.rel_lp)*0.35f;
            float att = (float)exp(-snd.rel_t*420.0);              /* the contact */
            float ring = (float)(sin(snd.rel_t*6.28318530718*1350.0)*exp(-snd.rel_t*110.0));
            float thud = (float)(sin(snd.rel_t*6.28318530718*95.0)*exp(-snd.rel_t*38.0));
            s += snd.rel_lp*att*0.55f + ring*0.10f + thud*0.16f;
            snd.rel_t += 1.0/sr;
            if(snd.rel_t > 0.25) snd.rel_t = -1.0;
        }
        /* --- degauss: the coil's mains-frequency buzz, decaying, with a
         * thump as it kicks in --- */
        if(snd.dg_t >= 0.0){
            float env  = (float)exp(-snd.dg_t*7.0);
            float buzz = (float)(sin(snd.dg_t*6.28318530718*50.0) + 0.35*sin(snd.dg_t*6.28318530718*100.0));
            float thump= (float)(sin(snd.dg_t*6.28318530718*42.0)*exp(-snd.dg_t*22.0));
            s += buzz*0.075f*env + thump*0.20f;
            snd.dg_t += 1.0/sr;
            if(snd.dg_t > 1.0) snd.dg_t = -1.0;
        }

        if(snd.spin > 0.002f){
            /* --- body hum --- */
            float body = 0.0f;
            for(int k=0;k<NHUM;k++){
                snd.hum_drift[k] += (0.09 + 0.031*k)/sr;
                float det = 1.0f + 0.0018f*(float)sin((double)snd.hum_drift[k]*6.28318530718);
                snd.hum_ph[k] += (double)(hum_hz[k]*det*snd.spin)/sr;
                body += hum_amp[k]*(float)sin(snd.hum_ph[k]*6.28318530718);
            }
            s += body * 0.012f * snd.spin;

            /* --- spindle whine: the actual disk sound, up in the kHz --- */
            float wh = 0.0f;
            for(int k=0;k<NWHINE;k++){
                snd.wh_drift[k] += (0.23 + 0.07*k)/sr;
                float det = 1.0f + 0.0022f*(float)sin((double)snd.wh_drift[k]*6.28318530718);
                /* pitch rises with the platter, so spin-up sweeps upward */
                snd.wh_ph[k] += (double)(whine_hz[k]*det*(0.55f+0.45f*snd.spin))/sr;
                wh += whine_amp[k]*(float)sin(snd.wh_ph[k]*6.28318530718);
            }
            s += wh * 0.0042f * snd.spin*snd.spin;
        }

        if(snd.fan_env > 0.002f){
            /* --- fan: airy broadband, one pole at ~2.2 kHz, plus a little
             * body so it is not pure hiss --- */
            float n = nrand();
            snd.fan_lp1 += (n       - snd.fan_lp1)*0.150f;
            snd.fan_lp2 += (snd.fan_lp1 - snd.fan_lp2)*0.150f;
            snd.fan_hp  += (snd.fan_lp2 - snd.fan_hp )*0.010f;      /* DC / rumble trap */
            s += (snd.fan_lp2 - snd.fan_hp) * 0.034f * snd.fan_env;
            snd.hiss_lp += (n - snd.hiss_lp)*0.55f;
            s += snd.hiss_lp * 0.0025f * snd.fan_env;
        }

        /* --- 3.5" floppy drive (sampled) --- */
        if(snd.fdd_left > 0.0 || snd.fdd_env > 0.001f){
            if(snd.fdd_left > 0.0) snd.fdd_left -= 1.0/sr;
            float want = (snd.fdd_left > 0.0) ? 1.0f : 0.0f;
            snd.fdd_env += (want-snd.fdd_env) * (float)(1.0/(sr*0.045));

            /* Linear resample, looping seamlessly.  Playing the clip back
             * slightly slow drops its pitch - the mechanical way a bigger,
             * heavier drive sounds. */
            double step = (double)FDD_PCM_RATE / sr * 0.84;
            int i0 = (int)snd.fdd_pos;
            double fr = snd.fdd_pos - i0;
            int i1 = i0+1; if(i1>=FDD_PCM_LEN) i1=0;
            float v = (float)(fdd_pcm[i0]*(1.0-fr) + fdd_pcm[i1]*fr) / 32768.0f;
            snd.fdd_pos += step;
            if(snd.fdd_pos >= FDD_PCM_LEN) snd.fdd_pos -= FDD_PCM_LEN;

            /* and a gentle low shelf: keep the body, ease off the top */
            snd.fdd_lp += (v - snd.fdd_lp)*0.34f;
            s += (snd.fdd_lp*0.75f + v*0.25f) * 0.60f * snd.fdd_env;
        }

        /* --- PC speaker on top --- */
        if(snd.spk_left > 0){
            float env = 1.0f;
            int done = snd.spk_total - snd.spk_left;
            if(done < snd.rate/200)     env = done/(float)(snd.rate/200);
            if(snd.spk_left < snd.rate/73)  env = fminf(env, snd.spk_left/(float)(snd.rate/73));
            float sq = (snd.spk_phase - floor(snd.spk_phase) < 0.5) ? 1.0f : -1.0f;
            s += sq * env * 0.16f;
            snd.spk_phase += PCSPK_HZ/sr;
            snd.spk_left--;
        }

        int v = (int)(s * 32767.0f);
        int l = out[i*2]   + v; if(l>32767)l=32767; if(l<-32768)l=-32768;
        int r = out[i*2+1] + v; if(r>32767)r=32767; if(r<-32768)r=-32768;
        out[i*2]   = (int16_t)l;
        out[i*2+1] = (int16_t)r;
    }
}
