/* dos.c — boot theater, the prompt, and the command set (SPEC §7).
 * The prompt is the UI: there is no other way to reach anything. */
#include "dos.h"
#include "font.h"
#include "library.h"
#include "catalog.h"
#include "install.h"
#include "dxm_core.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <ctype.h>

static char    scr[DOS_ROWS][DOS_COLS];
/* One VGA attribute per cell: low nibble foreground, high nibble background.
 * The boot screen and the prompt never leave 0x07, but a text-mode UI is
 * mostly colour - drawing Norton Commander in one grey would be pointless. */
static uint8_t att[DOS_ROWS][DOS_COLS];
static uint8_t cur_att = 0x07;
static int   cur_r, cur_c;
static char  line[128]; static int line_n;
static dos_state st;
static double t0, next_boot;
static int    boot_step;
static uint8_t fb[DOS_W*DOS_H*3];
static void nc_art(void);
static char   launch[32];
static int    launch_pending;
static double launch_at;      /* hold the launch until the drive is done */
static int    beep_pending;        /* POST beep, fired after the RAM count */
static double floppy_req;          /* seconds of drive activity wanted    */
static double now_t;               /* last dos_update time, for the logo  */
static int    mem_counting;        /* the memory test is spinning          */
static double mem_next;            /* next number update                   */
static long   mem_shown;           /* KB counted so far                    */
static int    mem_row, mem_col;    /* where to overwrite the digits        */

static void scroll(void){
    memmove(scr[0],scr[1],(DOS_ROWS-1)*DOS_COLS);
    memmove(att[0],att[1],(DOS_ROWS-1)*DOS_COLS);
    memset(scr[DOS_ROWS-1],' ',DOS_COLS);
    memset(att[DOS_ROWS-1],cur_att,DOS_COLS);
    cur_r=DOS_ROWS-1;
}
static void put(char ch){
    if(ch=='\n'){ cur_c=0; if(++cur_r>=DOS_ROWS) scroll(); return; }
    if(cur_c>=DOS_COLS){ cur_c=0; if(++cur_r>=DOS_ROWS) scroll(); }
    att[cur_r][cur_c]=cur_att;
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
    memset(att,0x07,sizeof att);
    cur_att=0x07;
    cur_r=cur_c=0; line_n=0; st=DOS_BOOT; boot_step=0; t0=-1; launch_pending=0;
    beep_pending=0; mem_counting=0; mem_shown=0;
}
void dos_core_failed(void){
    put('\n');
    sayln("Cannot run that program.");
    prompt(); st=DOS_PROMPT; line_n=0;
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

/* ---- NC.EXE: the dual-pane navigator --------------------------------
 * Norton Commander's shape, because it is the shape anyone who used one of
 * these machines already knows: a panel of what you can run on the left, a
 * panel describing the highlighted entry on the right, and the function-key
 * bar along the bottom.  Everything here draws straight into the character
 * grid and its attribute plane; the one exception is the artwork, which is
 * pixels and so is composited after the text in dos_render(). */
#include "dxm_road.h"

/* CP437 line drawing, named so the layout below reads as a drawing */
#define BX_H  0xC4
#define BX_V  0xB3
#define BX_TL 0xDA
#define BX_TR 0xBF
#define BX_BL 0xC0
#define BX_BR 0xD9
#define BX_TE 0xC3      /* tee, opening right */
#define BX_TW 0xB4      /* tee, opening left  */

/* Norton's palette: everything lives on blue, the highlight is a cyan bar,
 * and the key bar at the foot is black on cyan. */
#define A_PANEL 0x1B    /* light cyan on blue - the frames and plain text */
#define A_TEXT  0x17    /* light grey on blue - description body          */
#define A_NAME  0x1F    /* white on blue      - entries                   */
#define A_HEAD  0x1E    /* yellow on blue     - panel titles              */
#define A_SEL   0x30    /* black on cyan      - the selection bar         */
#define A_DIM   0x18    /* dark grey on blue  - what is not installed     */
#define A_BAR   0x30    /* black on cyan      - the function key bar      */
#define A_BARN  0x07    /* grey on black      - the key NUMBER            */

typedef struct {
    const char *file;          /* as it appears in the panel   */
    const char *cmd;           /* what dos runs for it         */
    const char *title;
    const char *by;
    const char *note;          /* why it cannot run, if it cannot */
    const char *desc[CAT_DESC];
    const cat_game *cat;       /* non-NULL if it can be downloaded */
    int available;             /* a build exists for this machine  */
    int year;
    const uint8_t *art; int aw, ah;
    int installed;
} nc_entry;

/* Entries come from the library, not from a table here: DXM links no game,
 * so what the panel lists is whatever is installed on the machine.  The
 * catalogue will add rows for games that are NOT installed yet, which is
 * why `installed` is a field rather than an assumption. */
static nc_entry nc_rows[LIB_MAX];
static int      nc_n;

/* The panel lists what is installed AND what could be.  Installed first,
 * then whatever the catalogue offers that is not already here - so the
 * machine's own contents lead, and the shop is underneath. */
static void nc_rows_build(void){
    nc_n=0;
    for(int i=0;i<lib_count() && nc_n<LIB_MAX;i++){
        const lib_game *g=lib_at(i);
        nc_entry *e=&nc_rows[nc_n++];
        memset(e,0,sizeof *e);
        e->file=g->id; e->cmd=g->id; e->title=g->title;
        e->by=g->by; e->year=g->year; e->installed=g->ready;
        e->art=dxm_road; e->aw=DXM_ROAD_W; e->ah=DXM_ROAD_HT;
        e->note=g->note;
    }
    for(int i=0;i<cat_count() && nc_n<LIB_MAX;i++){
        const cat_game *c=cat_at(i);
        if(lib_find(c->id)) continue;             /* already on the machine */
        nc_entry *e=&nc_rows[nc_n++];
        memset(e,0,sizeof *e);
        e->file=c->id; e->cmd=c->id; e->title=c->title;
        e->by=c->by; e->year=c->year;
        e->art=dxm_road; e->aw=DXM_ROAD_W; e->ah=DXM_ROAD_HT;
        e->cat=c;
        /* A game with no build for this machine is listed but cannot be
         * fetched - saying so is more use than leaving it out. */
        e->note=c->have_module?NULL:"no build for this machine yet";
        e->available=c->have_module;
        for(int k=0;k<CAT_DESC;k++) e->desc[k]=c->desc[k][0]?c->desc[k]:NULL;
    }
}

static int nc_open, nc_sel;

/* panel geometry, in cells */
#define NC_TOP    0
#define NC_BOT    21
#define NC_LX     0
#define NC_LW     40
#define NC_RX     40
#define NC_RW     40
#define NC_LIST_T 1                    /* first list row                 */
#define NC_LIST_B 19                   /* last list row                  */
#define NC_COLS   3                    /* the panel is multi-column      */
#define NC_ART_T  1                    /* art rows in the right panel    */
#define NC_ART_B  12
#define NC_DESC_T 14

static void cell(int r,int c,int ch,uint8_t a){
    if(r<0||r>=DOS_ROWS||c<0||c>=DOS_COLS) return;
    scr[r][c]=(char)ch; att[r][c]=a;
}
static void nfill(int r,int c,int n,int ch,uint8_t a){
    for(int k=0;k<n;k++) cell(r,c+k,ch,a);
}
static void nputs(int r,int c,const char *s,uint8_t a){
    for(int k=0;s[k];k++) cell(r,c+k,(unsigned char)s[k],a);
}
/* a framed box, with its title inlaid in the top run */
static void nbox(int x,int y,int w,int h,const char *title){
    cell(y,x,BX_TL,A_PANEL); cell(y,x+w-1,BX_TR,A_PANEL);
    cell(y+h-1,x,BX_BL,A_PANEL); cell(y+h-1,x+w-1,BX_BR,A_PANEL);
    nfill(y,x+1,w-2,BX_H,A_PANEL);
    nfill(y+h-1,x+1,w-2,BX_H,A_PANEL);
    for(int r=y+1;r<y+h-1;r++){
        cell(r,x,BX_V,A_PANEL); cell(r,x+w-1,BX_V,A_PANEL);
        nfill(r,x+1,w-2,' ',A_PANEL);
    }
    if(title){
        int n=(int)strlen(title);
        int tx=x+(w-n-2)/2;
        cell(y,tx-1,' ',A_PANEL);
        nputs(y,tx,title,A_HEAD);
        cell(y,tx+n,' ',A_PANEL);
    }
}
static void nrule(int x,int y,int w){          /* a divider across a panel */
    cell(y,x,BX_TE,A_PANEL); cell(y,x+w-1,BX_TW,A_PANEL);
    nfill(y,x+1,w-2,BX_H,A_PANEL);
}

static void nc_draw(void){
    memset(scr,' ',sizeof scr);
    memset(att,A_PANEL,sizeof att);

    /* ---- left panel: what you can run ---- */
    nbox(NC_LX,NC_TOP,NC_LW,NC_BOT-NC_TOP+1,"C:\\GAMES");
    { int rows=NC_LIST_B-NC_LIST_T+1;
      int cw=(NC_LW-2)/NC_COLS;                /* 12 cells a column */
      /* the column separators, which is what makes it read as a panel
       * rather than as three lists that happen to be side by side */
      for(int k=1;k<NC_COLS;k++){
        int cx=NC_LX+1+k*cw-1;
        for(int r=NC_LIST_T;r<=NC_LIST_B;r++) cell(r,cx,BX_V,A_PANEL);
      }
      for(int i=0;i<nc_n;i++){
        int colk=i/rows, rowk=i%rows;
        if(colk>=NC_COLS) break;
        int x=NC_LX+1+colk*cw, y=NC_LIST_T+rowk;
        uint8_t a=(i==nc_sel)?A_SEL:(nc_rows[i].installed?A_NAME:A_DIM);
        nfill(y,x,cw-1,' ',a);
        nputs(y,x+1,nc_rows[i].file,a);
      }
    }
    nrule(NC_LX,NC_BOT-1,NC_LW);
    { char b[48];
      snprintf(b,sizeof b," %d file(s)",nc_n);
      nputs(NC_BOT,NC_LX+2,b,A_PANEL); }

    /* ---- right panel: the highlighted entry ---- */
    if(nc_n==0){
        nbox(NC_RX,NC_TOP,NC_RW,NC_BOT-NC_TOP+1,"NOTHING INSTALLED");
        nputs(NC_DESC_T,  NC_RX+2,"No games on this machine.",A_TEXT);
        nputs(NC_DESC_T+2,NC_RX+2,"Put a .dxm module and its",A_TEXT);
        nputs(NC_DESC_T+3,NC_RX+2,"data under:",A_TEXT);
        nputs(NC_DESC_T+5,NC_RX+2,"  games/<name>/",A_NAME);
    } else {
    const nc_entry *e=&nc_rows[nc_sel];
    nbox(NC_RX,NC_TOP,NC_RW,NC_BOT-NC_TOP+1,e->title);
    /* the art well is painted black here; the pixels land on top of it in
     * dos_render(), which is the only place this program is not text */
    for(int r=NC_ART_T;r<=NC_ART_B;r++) nfill(r,NC_RX+1,NC_RW-2,' ',0x00);
    nrule(NC_RX,NC_ART_B+1,NC_RW);
    /* The description, then one line saying what Enter does here.  An entry
     * with no account of itself is the worst case: it is on screen, so
     * something has to explain it. */
    int row=NC_DESC_T;
    /* Wrap here rather than making the catalogue count columns.  A catalogue
     * that has to know the panel width is a catalogue that breaks the first
     * time the panel changes. */
    { const int W=NC_RW-4;
      char line[NC_RW]; int n=0;
      for(int k=0;k<CAT_DESC;k++){
        const char *d=e->desc[k];
        if(!d) continue;
        const char *w=d;
        while(*w){
            while(*w==' ') w++;
            const char *end=w;
            while(*end && *end!=' ') end++;
            int len=(int)(end-w);
            if(len>W) len=W;                       /* a word longer than the
                                                      pane: cut it, do not
                                                      lose the line */
            if(n && n+1+len>W){
                if(row<NC_BOT-4) nputs(row++,NC_RX+2,line,A_TEXT);
                n=0;
            }
            if(n) line[n++]=' ';
            memcpy(line+n,w,(size_t)len); n+=len; line[n]=0;
            w=end;
        }
        if(n){ if(row<NC_BOT-4) nputs(row++,NC_RX+2,line,A_TEXT); n=0; line[0]=0; }
      }
    }
    if(e->note && e->note[0]){
        if(!e->installed && !e->available) nputs(row++,NC_RX+2,"UNAVAILABLE",A_HEAD);
        nputs(row++,NC_RX+2,e->note,A_TEXT);
    }
    { inst_status is; install_poll(&is);
      int busy = is.state==INST_RUNNING && !strcmp(is.id,e->file);
      if(busy){
          /* the bar, and the bytes under it - a percentage on its own tells
           * you nothing about whether anything is still moving */
          char bar[34]; int w=30;
          int on=(int)(is.frac*w+0.5); if(on>w)on=w; if(on<0)on=0;
          for(int k=0;k<w;k++) bar[k]=(char)(k<on?0xDB:0xB0);
          bar[w]=0;
          nputs(NC_BOT-4,NC_RX+2,is.stage,A_HEAD);
          nputs(NC_BOT-3,NC_RX+2,bar,A_NAME);
          { char b[48];
            if(is.total>0) snprintf(b,sizeof b,"%.0f%%  %.1f of %.1f MB",
                                    is.frac*100.0,is.got/1048576.0,is.total/1048576.0);
            else           snprintf(b,sizeof b,"%.1f MB",is.got/1048576.0);
            nputs(NC_BOT-2,NC_RX+2,b,A_TEXT); }
      } else if(is.state==INST_FAILED && !strcmp(is.id,e->file)){
          nputs(NC_BOT-3,NC_RX+2,"DOWNLOAD FAILED",A_HEAD);
          nputs(NC_BOT-2,NC_RX+2,is.err,A_TEXT);
      } else if(e->installed){
          nputs(NC_BOT-2,NC_RX+2,"ENTER to play",A_NAME);
      } else if(e->available){
          char b[48];
          double mb=(e->cat->module.size+e->cat->data.size)/1048576.0;
          snprintf(b,sizeof b,"ENTER to download  (%.1f MB)",mb);
          nputs(NC_BOT-2,NC_RX+2,b,A_NAME);
      }
    }
    nrule(NC_RX,NC_BOT-1,NC_RW);
    { char foot[64];
      if(e->year) snprintf(foot,sizeof foot,"%s, %d",e->by,e->year);
      else        snprintf(foot,sizeof foot,"%s",e->by);
      nputs(NC_BOT,NC_RX+2,foot,A_PANEL); }
    }

    /* ---- the command line, and the key bar ---- */
    nfill(22,0,DOS_COLS,' ',0x07);
    nputs(23,0,"C:\\>",0x07);
    nfill(23,4,DOS_COLS-4,' ',0x07);
    static const char *KEYS[10]={
        "Help","Info","View","    ","    ","    ","    ","    ","    ","Quit" };
    { int x=0;
      for(int k=0;k<10;k++){
        char num[3]; snprintf(num,sizeof num,"%d",k+1);
        nputs(24,x,num,A_BARN); x+=(k==9)?2:1;
        nputs(24,x,KEYS[k],A_BAR);
        nfill(24,x+4,1,' ',A_BAR);
        x+=5;
      }
      while(x<DOS_COLS) nfill(24,x++,1,' ',A_BAR); }
}

/* The artwork: pixels, so it goes on after the character cells. */
static void nc_art(void){
    const nc_entry *e=&nc_rows[nc_sel];
    if(!e->art) return;
    int bx=DOS_PAD_X+(NC_RX+1)*8, by=DOS_PAD_Y+NC_ART_T*16;
    int bw=(NC_RW-2)*8, bh=(NC_ART_B-NC_ART_T+1)*16;
    /* fit inside the well, whole-pixel scaled: a game's art is pixels and
     * resampling it to a fractional size is the one thing that would make
     * it look like a photograph of a screen rather than the screen */
    int sc=1;
    while((sc+1)*e->aw<=bw && (sc+1)*e->ah<=bh) sc++;
    int dw=e->aw*sc, dh=e->ah*sc;
    int x0=bx+(bw-dw)/2, y0=by+(bh-dh)/2;
    for(int y=0;y<dh;y++){
        int dy=y0+y; if(dy<0||dy>=DOS_H) continue;
        for(int x=0;x<dw;x++){
            int dx=x0+x; if(dx<0||dx>=DOS_W) continue;
            const uint8_t *sp=e->art+((size_t)(y/sc)*e->aw+(x/sc))*4;
            float al=sp[3]/255.0f;
            if(al<=0.004f) continue;
            uint8_t *q=fb+((size_t)dy*DOS_W+dx)*3;
            for(int k=0;k<3;k++)
                q[k]=(uint8_t)(q[k]*(1.0f-al)+sp[k]*al);
        }
    }
}

/* Arrow keys walk the panel in COLUMN-MAJOR order, because that is how the
 * entries are laid out: down moves within a column, right moves a column. */
static void nc_key(int ch,int sc){
    int rows=NC_LIST_B-NC_LIST_T+1;
    if(sc==DXM_SC_ESC || ch==27){
        nc_open=0;
        memset(scr,' ',sizeof scr); memset(att,0x07,sizeof att);
        cur_att=0x07; cur_r=cur_c=0; prompt();
        return;
    }
    if(sc==DXM_SC_DOWN)      { if(nc_sel+1<nc_n) nc_sel++; }
    else if(sc==DXM_SC_UP)   { if(nc_sel>0) nc_sel--; }
    else if(sc==DXM_SC_RIGHT){ if(nc_sel+rows<nc_n) nc_sel+=rows; }
    else if(sc==DXM_SC_LEFT) { if(nc_sel-rows>=0) nc_sel-=rows; }
    else if(sc==DXM_SC_ENTER || ch=='\r' || ch=='\n'){
        const nc_entry *e=&nc_rows[nc_sel];
        inst_status is; install_poll(&is);
        if(!e->installed && e->available && is.state!=INST_RUNNING){
            install_clear();
            install_start(e->cat);
            floppy_req=1.4;               /* the drive answers, as it would */
            nc_draw();
            return;
        }
        if(e->installed){
            nc_open=0;
            memset(scr,' ',sizeof scr); memset(att,0x07,sizeof att);
            cur_att=0x07; cur_r=cur_c=0;
            snprintf(launch,sizeof launch,"%s",e->cmd);
            floppy_req=2.6;
            launch_at=-1.0;
            st=DOS_RUNNING;
            return;
        }
    }
    nc_draw();
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
    sayln("NC       EXE        41,272  06-08-93  10:14a");
    /* One line per installed game.  DIR reports what is on the machine, so
     * it has to come from the same place the navigator's list does. */
    for(int i=0;i<lib_count();i++){
        const lib_game *g=lib_at(i);
        char nm[16], ln[80]; int k=0;
        for(;g->id[k] && k<8;k++) nm[k]=(char)toupper((unsigned char)g->id[k]);
        while(k<8) nm[k++]=' ';
        nm[8]=0;
        snprintf(ln,sizeof ln,"%s EXE       114,688  03-15-93   1:93a",nm);
        sayln(ln);
    }
    put('\n');
    { char ln[80];
      snprintf(ln,sizeof ln,"        %d file(s)",5+lib_count());
      sayln(ln); }
    sayln("                      536,870,912 bytes free");
}
static void cmd_help(void){
    sayln("DXM-DOS command reference");
    put('\n');
    sayln("DIR        List the files on this machine.");
    sayln("CLS        Clear the screen.");
    sayln("VER        Show the DOS version.");
    sayln("TYPE file  Display a text file.");
    sayln("NC         Browse the games in a dual-pane navigator.");
    for(int i=0;i<lib_count();i++){
        const lib_game *g=lib_at(i);
        char nm[16], ln[96]; int k=0;
        for(;g->id[k] && k<10;k++) nm[k]=(char)toupper((unsigned char)g->id[k]);
        while(k<10) nm[k++]=' ';
        nm[10]=0;
        snprintf(ln,sizeof ln,"%s Run %s.",nm,g->title);
        sayln(ln);
    }
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
    else if(!strcmp(s,"CLS")){ memset(scr,' ',sizeof scr);
                               memset(att,0x07,sizeof att);
                               cur_att=0x07; cur_r=cur_c=0; return; }
    else if(!strcmp(s,"HELP")) cmd_help();
    else if(!strcmp(s,"VER"))  sayln("DXM-DOS Version 1.0  (C) 2026");
    else if(!strcmp(s,"TYPE")){
        if(arg && !strcmp(arg,"README.TXT")){
            sayln("DOS ex Machina - a machine that only runs games.");
            sayln("Type NC to browse the games.  Type EXIT to switch off.");
        } else { say("File not found - "); sayln(arg?arg:""); }
    }
    else if(!strcmp(s,"EXIT")) { st=DOS_OFF; return; }
    else if(!strcmp(s,"NC")){ lib_scan(); nc_rows_build();
                              nc_open=1; nc_sel=0; nc_draw(); return; }
    else if(!strcmp(s,"FORMAT"))
        sayln("Nice try.");
    else {
        /* Anything else may be an installed game.  Typing its name is still
         * how you run it - the navigator is a convenience over the same
         * mechanism, not a replacement for it. */
        const lib_game *g=lib_find(s);
        if(!g)             sayln("Bad command or file name");
        else if(!g->ready){ say("Cannot run "); say(g->title); sayln(":");
                            sayln(g->note[0]?g->note:"not ready"); }
        else { snprintf(launch,sizeof launch,"%s",g->id);
               floppy_req=2.6;              /* the drive reads the game */
               launch_at=-1.0;              /* armed; set on the next tick */
               st=DOS_RUNNING; return; }
    }
}

void dos_key(int ch,int sc){
    if(st!=DOS_PROMPT){ if(st==DOS_BOOT) next_boot=0; return; }
    if(nc_open){ nc_key(ch,sc); return; }
    if(ch=='\r'||ch=='\n'){
        put('\n'); line[line_n]=0;
        char tmp[128]; memcpy(tmp,line,sizeof tmp);
        line_n=0; run(tmp);
        /* NC owns the whole screen once it opens, so the prompt must not be
         * printed over it - the cursor is wherever the command line left it,
         * and nc_draw does not move it. */
        if(st==DOS_PROMPT && !nc_open) prompt();
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
    if(nc_open){
        /* A fresh catalogue, or a finished install, changes what the panel
         * should say - and a running one changes it every frame. */
        if(cat_refresh_collect()>0) nc_rows_build();
        inst_status is; install_poll(&is);
        static inst_state was=INST_IDLE;
        if(is.state==INST_DONE && was!=INST_DONE){
            lib_scan(); nc_rows_build();
            floppy_req=0.6;
        }
        if(is.state==INST_RUNNING || is.state!=was) nc_draw();
        was=is.state;
    }
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

/* the 16 VGA text colours, as the DAC actually produced them */
static const uint8_t VGA16[16][3]={
  {0x00,0x00,0x00},{0x00,0x00,0xAA},{0x00,0xAA,0x00},{0x00,0xAA,0xAA},
  {0xAA,0x00,0x00},{0xAA,0x00,0xAA},{0xAA,0x55,0x00},{0xAA,0xAA,0xAA},
  {0x55,0x55,0x55},{0x55,0x55,0xFF},{0x55,0xFF,0x55},{0x55,0xFF,0xFF},
  {0xFF,0x55,0x55},{0xFF,0x55,0xFF},{0xFF,0xFF,0x55},{0xFF,0xFF,0xFF}};

const uint8_t *dos_render(void){
    memset(fb,0,sizeof fb);
    static double blink; blink+=1.0;
    for(int r=0;r<DOS_ROWS;r++)
      for(int c=0;c<DOS_COLS;c++){
        const uint8_t *g=font_glyph((unsigned char)scr[r][c]);
        const uint8_t *fg=VGA16[att[r][c]&0x0F];
        const uint8_t *bg=VGA16[(att[r][c]>>4)&0x07];
        for(int j=0;j<8;j++){
            uint8_t bits=g[j];
            for(int i=0;i<8;i++){
                const uint8_t *col=(bits&(0x80>>i))?fg:bg;
                for(int d=0;d<2;d++){          /* 8x8 rendered at 8x16 */
                    int y=DOS_PAD_Y+r*16+j*2+d, x=DOS_PAD_X+c*8+i;
                    uint8_t *p=fb+((size_t)y*DOS_W+x)*3;
                    p[0]=col[0]; p[1]=col[1]; p[2]=col[2];
                }
            }
        }
      }
    if(nc_open) nc_art();
    if(st==DOS_BOOT) draw_badge(now_t);      /* POST only */
    if(st==DOS_PROMPT && !nc_open && ((int)(blink/28)&1)){
        for(int j=0;j<14;j++) for(int i=0;i<8;i++){
            int y=DOS_PAD_Y+cur_r*16+j, x=DOS_PAD_X+cur_c*8+i;
            if(y<DOS_H&&x<DOS_W){ uint8_t *p=fb+((size_t)y*DOS_W+x)*3;
                                  p[0]=0xAA;p[1]=0xAA;p[2]=0xAA; }
        }
    }
    return fb;
}
