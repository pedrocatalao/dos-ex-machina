/* dos.c — boot theater, the prompt, and the command set (SPEC §7).
 * The prompt is the UI: there is no other way to reach anything. */
#include "dos.h"
#include "font.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static char  scr[DOS_ROWS][DOS_COLS];
static int   cur_r, cur_c;
static char  line[128]; static int line_n;
static dos_state st;
static double t0, next_boot;
static int    boot_step;
static uint8_t fb[DOS_W*DOS_H*3];
static char   launch[32];
static int    launch_pending;
static double launch_at;      /* hold the launch until the drive is done */
static int    sky_installed = 1;   /* v0: data ships with the dev checkout */
static int    beep_pending;        /* POST beep, fired after the RAM count */
static double floppy_req;          /* seconds of drive activity wanted    */
static double now_t;               /* last dos_update time, for the logo  */
static int    mem_counting;        /* the memory test is spinning          */
static double mem_next;            /* next number update                   */
static long   mem_shown;           /* KB counted so far                    */
static int    mem_row, mem_col;    /* where to overwrite the digits        */

static void scroll(void){
    memmove(scr[0],scr[1],(DOS_ROWS-1)*DOS_COLS);
    memset(scr[DOS_ROWS-1],' ',DOS_COLS);
    cur_r=DOS_ROWS-1;
}
static void put(char ch){
    if(ch=='\n'){ cur_c=0; if(++cur_r>=DOS_ROWS) scroll(); return; }
    if(cur_c>=DOS_COLS){ cur_c=0; if(++cur_r>=DOS_ROWS) scroll(); }
    scr[cur_r][cur_c++]=ch;
}
static void say(const char *s){ while(*s) put(*s++); }
static void sayln(const char *s){ say(s); put('\n'); }
static void prompt(void){ say("C:\\>"); }

static const char *BOOT[] = {
  "DXM BIOS v1.0  (C) 2026 DOS ex Machina",
  "",
  "Main Processor  : 80486DX2  66 MHz",
  "Memory Test     : ",          /* counted live, see dos_update */
  "",
  "Fixed Disk 0    : DXM-VIRTUAL  512 MB",
  "Floppy Disk A   : 1.44 MB, 3.5 in.",
  "",
  "Starting DXM-DOS...",
  "",
  NULL
};

void dos_init(void){
    memset(scr,' ',sizeof scr);
    cur_r=cur_c=0; line_n=0; st=DOS_BOOT; boot_step=0; t0=-1; launch_pending=0;
    beep_pending=0; mem_counting=0; mem_shown=0;
}
void dos_core_exited(void){
    put('\n'); prompt(); st=DOS_PROMPT; line_n=0;
}
/* The game does not appear the instant you type its name: the drive spins
 * up and reads first, exactly as it would have.  dos_update() releases the
 * launch once that load has had time to run. */
const char *dos_launch_request(void){
    if(!launch_pending) return NULL;
    launch_pending=0; return launch;
}

static void cmd_dir(void){
    sayln(" Volume in drive C is DXM-DOS");
    sayln(" Volume Serial Number is 1993-0C7E");
    sayln(" Directory of C:\\");
    put('\n');
    sayln("COMMAND  COM        54,645  05-31-94   6:22a");
    sayln("AUTOEXEC BAT           435  05-31-94   6:22a");
    sayln("CONFIG   SYS           246  05-31-94   6:22a");
    sayln("README   TXT         1,204  08-30-26  11:04a");
    if(sky_installed)
        sayln("SKYROADS EXE       114,688  03-15-93   1:93a");
    put('\n');
    sayln(sky_installed ? "        5 file(s)         171,218 bytes"
                        : "        4 file(s)          56,530 bytes");
    sayln("                      536,870,912 bytes free");
}
static void cmd_help(void){
    sayln("DXM-DOS command reference");
    put('\n');
    sayln("DIR        List the files on this machine.");
    sayln("CLS        Clear the screen.");
    sayln("VER        Show the DOS version.");
    sayln("TYPE file  Display a text file.");
    sayln("SKYROADS   Run SkyRoads.");
    sayln("EXIT       Switch the machine off.");
}
static void run(char *s){
    while(*s==' ') s++;
    for(char *p=s;*p;p++) if(*p>='a'&&*p<='z') *p-=32;
    char *sp=strchr(s,' '); char *arg=NULL;
    if(sp){ *sp=0; arg=sp+1; while(*arg==' ') arg++; }
    size_t n=strlen(s);
    if(n>4 && !strcmp(s+n-4,".EXE")) s[n-4]=0;
    if(!*s) return;
    if(!strcmp(s,"DIR")){      floppy_req=0.7; cmd_dir(); }
    else if(!strcmp(s,"CLS")){ memset(scr,' ',sizeof scr); cur_r=cur_c=0; return; }
    else if(!strcmp(s,"HELP")) cmd_help();
    else if(!strcmp(s,"VER"))  sayln("DXM-DOS Version 1.0  (C) 2026");
    else if(!strcmp(s,"TYPE")){
        if(arg && !strcmp(arg,"README.TXT")){
            sayln("DOS ex Machina - a machine that only runs games.");
            sayln("Type SKYROADS to play.  Type EXIT to switch off.");
        } else { say("File not found - "); sayln(arg?arg:""); }
    }
    else if(!strcmp(s,"EXIT")) { st=DOS_OFF; return; }
    else if(!strcmp(s,"SKYROADS")){
        if(!sky_installed){ sayln("Bad command or file name"); }
        else { snprintf(launch,sizeof launch,"skyroads");
               floppy_req=2.6;              /* the drive reads the game */
               launch_at=-1.0;              /* armed; set on the next tick */
               st=DOS_RUNNING; return; }
    }
    else if(!strcmp(s,"FORMAT"))
        sayln("Nice try.");
    else sayln("Bad command or file name");
}

void dos_key(int ch,int sc){
    if(st!=DOS_PROMPT){ if(st==DOS_BOOT) next_boot=0; return; }
    if(ch=='\r'||ch=='\n'){
        put('\n'); line[line_n]=0;
        char tmp[128]; memcpy(tmp,line,sizeof tmp);
        line_n=0; run(tmp);
        if(st==DOS_PROMPT) prompt();
        return;
    }
    if(ch=='\b'){ if(line_n){ line_n--; if(cur_c>4) cur_c--; scr[cur_r][cur_c]=' '; } return; }
    if(ch>=32 && ch<127 && line_n<(int)sizeof line-1){ line[line_n++]=(char)ch; put((char)ch); }
    (void)sc;
}

/* The PC speaker POST beep: taken once, at power-on. */
int dos_take_beep(void){ int b=beep_pending; beep_pending=0; return b; }
double dos_take_floppy(void){ double f=floppy_req; floppy_req=0.0; return f; }

/* The RAM count spins in place: the BIOS printed the running total and
 * overwrote it, so we hold the cursor at the digits and rewrite them. */
#define MEM_TOTAL_KB 655360L
#define MEM_STEP_KB  8192L          /* the count visibly steps, not smooth */

static void mem_draw(long kb){
    char buf[24];
    snprintf(buf,sizeof buf,"%ld KB",kb);
    int c=mem_col;
    for(const char *p=buf;*p&&c<DOS_COLS;p++) scr[mem_row][c++]=*p;
    while(c<DOS_COLS && c<mem_col+12) scr[mem_row][c++]=' ';
}

dos_state dos_update(double t){
    now_t=t;
    if(t0<0){ t0=t; next_boot=t+0.35; }

    /* loading pause between the command and the game taking over */
    if(st==DOS_RUNNING && !launch_pending && launch_at<0.0)
        launch_at = t + 2.3;
    if(st==DOS_RUNNING && launch_at>0.0 && t>=launch_at){
        launch_pending=1; launch_at=0.0;
    }

    if(mem_counting){
        if(t>=mem_next){
            mem_shown+=MEM_STEP_KB;
            if(mem_shown>=MEM_TOTAL_KB){
                mem_shown=MEM_TOTAL_KB;
                mem_draw(mem_shown);
                { int c=mem_col+11; const char *ok=" OK";
                  for(const char *p=ok;*p&&c<DOS_COLS;p++) scr[mem_row][c++]=*p; }
                mem_counting=0;
                cur_c=0; if(++cur_r>=DOS_ROWS) scroll();   /* close the line */
                beep_pending=1;           /* POST beep AFTER the RAM check */
                next_boot=t+0.45;
            } else {
                mem_draw(mem_shown);
                mem_next=t+0.010;
            }
        }
        return st;                        /* boot text pauses while counting */
    }

    if(st==DOS_BOOT && t>=next_boot){
        if(BOOT[boot_step]){
            const char *ln=BOOT[boot_step++];
            say(ln);
            if(strstr(ln,"Floppy Disk A")) floppy_req=1.1;  /* BIOS seeks A: */
            if(strstr(ln,"Memory Test")){ /* start the live count here */
                mem_row=cur_r; mem_col=cur_c;
                mem_counting=1; mem_shown=0; mem_next=t;
                return st;
            }
            put('\n');
            next_boot=t+0.16;
        }
        else { st=DOS_PROMPT; prompt(); }
    }
    return st;
}

/* The DXM mark, shown top-right during POST the way a 486 showed its BIOS
 * or power-management badge.  Baked in by tools/mklogo.py with the white
 * background keyed to alpha so it composites onto the black screen. */
#include "dxm_logo.h"
static void draw_badge(double t){
    if(t0<0) return;
    float a=(float)((t-t0-0.55)/1.2);            /* fade in */
    if(a<=0.0f) return;
    if(a>1.0f) a=1.0f;
    int x0=DOS_W-DXM_LOGO_W-14, y0=10;
    for(int y=0;y<DXM_LOGO_HT;y++){
        int dy=y0+y; if(dy<0||dy>=DOS_H) continue;
        for(int x=0;x<DXM_LOGO_W;x++){
            int dx=x0+x; if(dx<0||dx>=DOS_W) continue;
            const uint8_t *sp=dxm_logo+((size_t)y*DXM_LOGO_W+x)*4;
            float al=sp[3]/255.0f*a;
            if(al<=0.004f) continue;
            uint8_t *q=fb+((size_t)dy*DOS_W+dx)*3;
            for(int k=0;k<3;k++)
                q[k]=(uint8_t)(q[k]*(1.0f-al)+sp[k]*al);
        }
    }
}

const uint8_t *dos_render(void){
    memset(fb,0,sizeof fb);
    static double blink; blink+=1.0;
    for(int r=0;r<DOS_ROWS;r++)
      for(int c=0;c<DOS_COLS;c++){
        const uint8_t *g=font_glyph((unsigned char)scr[r][c]);
        for(int j=0;j<8;j++){
            uint8_t bits=g[j];
            for(int i=0;i<8;i++) if(bits&(0x80>>i)){
                for(int d=0;d<2;d++){          /* 8x8 rendered at 8x16 */
                    int y=r*16+j*2+d, x=c*8+i;
                    uint8_t *p=fb+((size_t)y*DOS_W+x)*3;
                    p[0]=0x33; p[1]=0xF0; p[2]=0x55;   /* P1 green phosphor */
                }
            }
        }
      }
    if(st==DOS_BOOT) draw_badge(now_t);      /* POST only */
    if(st==DOS_PROMPT && ((int)(blink/28)&1)){
        for(int j=0;j<14;j++) for(int i=0;i<8;i++){
            int y=cur_r*16+j, x=cur_c*8+i;
            if(y<DOS_H&&x<DOS_W){ uint8_t *p=fb+((size_t)y*DOS_W+x)*3;
                                  p[0]=0x33;p[1]=0xF0;p[2]=0x55; }
        }
    }
    return fb;
}
