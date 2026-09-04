/* dos.c — boot theater, the prompt, and the command set (SPEC §7).
 * The prompt is the UI: there is no other way to reach anything. */
#include "dos.h"
#include "font.h"
#include "library.h"
#include "catalog.h"
#include "install.h"
#include "art.h"
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
/* The machine has one subdirectory that matters: games install into
 * C:\GAMES, and running one from the prompt means going there first, the
 * way it would have.  NC reaches them wherever you are - it is a program
 * that browses the disk, not a shortcut around it. */
static int  in_games;             /* 0 = C:\, 1 = C:\GAMES */
static int  prompt_len;           /* so backspace knows where the line starts */
static void prompt(void){
    const char *p = in_games ? "C:\\GAMES>" : "C:\\>";
    prompt_len = (int)strlen(p);
    say(p);
}

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
    cur_r=cur_c=0; line_n=0; in_games=0; st=DOS_BOOT; boot_step=0; t0=-1; launch_pending=0;
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
#define A_DLG   0x70    /* black on light grey - a dialog                  */
#define A_DLGK  0x74    /* red on light grey   - the keys a dialog takes   */
#define A_SHDW  0x08    /* dark grey on black  - the shadow it casts       */

/* the function keys, as plain DOS scancodes */
#define SC_F1  0x3B
#define SC_F2  0x3C
#define SC_F3  0x3D

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
        /* An installed game still wants its picture AND its description -
         * the catalogue is the only place either lives, and they do not stop
         * being true once the game is on the disk. */
        e->cat=cat_find(g->id);
        if(e->cat)
            for(int k=0;k<CAT_DESC;k++)
                e->desc[k]=e->cat->desc[k][0]?e->cat->desc[k]:NULL;
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
/* A dialog over the panels.  The navigator's keys go to it while it is up,
 * and it says what Enter and Esc mean. */
enum { DLG_NONE=0, DLG_HELP, DLG_DELETE, DLG_RESET, DLG_QUIT, DLG_NOTE };
static int  nc_dlg;
static char nc_note[2][64];             /* what DLG_NOTE has to say */
int dos_nc_open(void){ return nc_open; }

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

/* A grey box over the panels with a shadow to its lower right, the way
 * Norton's own dialogs sat.  Lines are centred; the last carries the keys. */
static void nc_dialog(const char *title,const char *const *lines,int n,
                      const char *keys,int left){
    int w=50, h=n+4, x=(DOS_COLS-w)/2, y=6;
    /* the shadow first, so the box paints over its inner edge */
    for(int r=y+1;r<y+h+1;r++) for(int c=x+2;c<x+w+2;c++)
        if(r<DOS_ROWS&&c<DOS_COLS) att[r][c]=A_SHDW;
    for(int r=y;r<y+h;r++) nfill(r,x,w,' ',A_DLG);
    cell(y,x,BX_TL,A_DLG); cell(y,x+w-1,BX_TR,A_DLG);
    cell(y+h-1,x,BX_BL,A_DLG); cell(y+h-1,x+w-1,BX_BR,A_DLG);
    nfill(y,x+1,w-2,BX_H,A_DLG); nfill(y+h-1,x+1,w-2,BX_H,A_DLG);
    for(int r=y+1;r<y+h-1;r++){ cell(r,x,BX_V,A_DLG); cell(r,x+w-1,BX_V,A_DLG); }
    if(title){ int tn=(int)strlen(title);
               nputs(y,x+(w-tn-2)/2+1,title,A_DLG);
               cell(y,x+(w-tn-2)/2,' ',A_DLG); cell(y,x+(w-tn-2)/2+1+tn,' ',A_DLG); }
    /* a table of keys reads left-aligned; a question reads centred */
    for(int k=0;k<n;k++){
        int ln=(int)strlen(lines[k]);
        nputs(y+2+k,left?x+3:x+(w-ln)/2,lines[k],A_DLG);
    }
    if(keys){ int kn=(int)strlen(keys); nputs(y+h-2,x+(w-kn)/2,keys,A_DLGK); }
}

static void nc_draw(void){
    memset(scr,' ',sizeof scr);
    memset(att,A_PANEL,sizeof att);

    /* ---- left panel: what you can run ---- */
    /* GAMES, not C:\GAMES - the panel is not a directory listing.  It shows
     * what is installed AND what could be, and only half of that is on the
     * disk that path would name. */
    nbox(NC_LX,NC_TOP,NC_LW,NC_BOT-NC_TOP+1,"GAMES");
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
        /* The header stays C:\GAMES because the installed ones really are
         * there.  What is NOT on the disk is marked instead, so the panel
         * still reads as a directory listing with a few things left to
         * fetch.  The mark goes after the name, where a listing puts a
         * file's attributes. */
        nputs(y,x+1,nc_rows[i].file,a);
        if(!nc_rows[i].installed){
            int ax=x+1+(int)strlen(nc_rows[i].file)+1;
            if(ax < x+cw-1) cell(y,ax,0x19,a);
        }
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
    /* Ten slots of eight cells - exactly the eighty columns.  The number
     * sits right-aligned in two cells so "10" takes no more room than "1",
     * and the label has six, which is what Norton's own bar gave it. */
    static const char *KEYS[10]={
        "Help  ","Delete","Reset ","      ","      ",
        "      ","      ","      ","      ","Quit  " };
    for(int k=0;k<10;k++){
        char num[3]; snprintf(num,sizeof num,"%2d",k+1);
        nputs(24,k*8,num,A_BARN);
        nputs(24,k*8+2,KEYS[k],A_BAR);
    }

    /* ---- whatever is asking a question sits on top of it all ---- */
    if(nc_dlg==DLG_HELP){
        static const char *const L[]={
            "ENTER      play the game, or download it",
            "ARROWS     move between games",
            "F2         delete the game from this machine",
            "F3         reset saved games and settings",
            "F10        switch the machine off",
            "ESC        back to the prompt",
        };
        nc_dialog("NC HELP",L,7,"press any key",1);
    } else if(nc_dlg==DLG_DELETE || nc_dlg==DLG_RESET){
        const nc_entry *e=&nc_rows[nc_sel];
        char l1[64], l2[64];
        if(nc_dlg==DLG_DELETE){
            snprintf(l1,sizeof l1,"Delete %s from this machine?",e->title);
            snprintf(l2,sizeof l2,"The game and everything it saved.");
        } else {
            snprintf(l1,sizeof l1,"Reset %s?",e->title);
            snprintf(l2,sizeof l2,"Saved games and settings go; the game stays.");
        }
        const char *L[]={l1,l2};
        nc_dialog(nc_dlg==DLG_DELETE?"DELETE":"RESET",L,2,"ENTER yes    ESC no",0);
    } else if(nc_dlg==DLG_QUIT){
        static const char *const L[]={ "Switch the machine off?" };
        nc_dialog("QUIT",L,1,"ENTER yes    ESC no",0);
    } else if(nc_dlg==DLG_NOTE){
        const char *L[]={nc_note[0],nc_note[1]};
        nc_dialog(NULL,L,nc_note[1][0]?2:1,"press any key",0);
    }
}

/* The artwork: pixels, so it goes on after the character cells. */
/* The picture is pixels laid over the cells, so it does not know what
 * has been drawn on top of it.  Ask the cell: under a dialog the pixel is
 * not painted at all, and under the dialog's shadow it is painted dark,
 * the way the shadow dims the text it falls on. */
static void art_px(int dx,int dy,int r,int g,int b,float al){
    int cr=(dy-DOS_PAD_Y)/16, cc=(dx-DOS_PAD_X)/8;
    if(cr>=0&&cr<DOS_ROWS&&cc>=0&&cc<DOS_COLS){
        uint8_t a=att[cr][cc];
        if(a==A_DLG||a==A_DLGK) return;
        if(a==A_SHDW){ r=r*35/100; g=g*35/100; b=b*35/100; }
    }
    uint8_t *q=fb+((size_t)dy*DOS_W+dx)*3;
    q[0]=(uint8_t)(q[0]*(1.0f-al)+r*al);
    q[1]=(uint8_t)(q[1]*(1.0f-al)+g*al);
    q[2]=(uint8_t)(q[2]*(1.0f-al)+b*al);
}

static void nc_art(void){
    if(nc_n==0) return;
    const nc_entry *e=&nc_rows[nc_sel];
    const uint8_t *px=e->art; int aw=e->aw, ah=e->ah;
    /* The catalogue's picture if it has arrived; the road mark until it
     * does, so the well is never simply empty. */
    if(e->cat){
        int w,h;
        const uint8_t *got=art_get(&e->cat->art,&w,&h);
        if(got){ px=got; aw=w; ah=h; }
    }
    if(!px) return;
    int bx=DOS_PAD_X+(NC_RX+1)*8, by=DOS_PAD_Y+NC_ART_T*16;
    int bw=(NC_RW-2)*8, bh=(NC_ART_B-NC_ART_T+1)*16;
    /* Real artwork is far bigger than the well, so it is scaled DOWN by an
     * integer step with a box filter - a game screenshot reduced by point
     * sampling drops every other scanline and comes apart. */
    if(aw>bw||ah>bh){
        /* the SMALLEST step that fits - anything larger throws away
         * resolution for nothing.  The earlier version kept stepping while
         * the result was still half the well, which overshot every time. */
        int step=1;
        while((aw/step>bw || ah/step>bh) && step<32) step++;
        int dw=aw/step, dh=ah/step;
        int x0=bx+(bw-dw)/2, y0=by+(bh-dh)/2;
        for(int y=0;y<dh;y++){
            int dy=y0+y; if(dy<0||dy>=DOS_H) continue;
            for(int x=0;x<dw;x++){
                int dx=x0+x; if(dx<0||dx>=DOS_W) continue;
                int r=0,g=0,b=0,n=0;
                for(int v=0;v<step;v++)
                  for(int u=0;u<step;u++){
                    const uint8_t *sp=px+((size_t)(y*step+v)*aw+(x*step+u))*4;
                    r+=sp[0]; g+=sp[1]; b+=sp[2]; n++;
                  }
                art_px(dx,dy,r/n,g/n,b/n,1.0f);
            }
        }
        return;
    }
    /* smaller than the well: whole-pixel up-scale, so it stays pixels */
    int sc=1;
    while((sc+1)*aw<=bw && (sc+1)*ah<=bh) sc++;
    int dw=aw*sc, dh=ah*sc;
    int x0=bx+(bw-dw)/2, y0=by+(bh-dh)/2;
    for(int y=0;y<dh;y++){
        int dy=y0+y; if(dy<0||dy>=DOS_H) continue;
        for(int x=0;x<dw;x++){
            int dx=x0+x; if(dx<0||dx>=DOS_W) continue;
            const uint8_t *sp=px+((size_t)(y/sc)*aw+(x/sc))*4;
            float al=sp[3]/255.0f;
            if(al<=0.004f) continue;
            art_px(dx,dy,sp[0],sp[1],sp[2],al);
        }
    }
}

/* Arrow keys walk the panel in COLUMN-MAJOR order, because that is how the
 * entries are laid out: down moves within a column, right moves a column. */
static void nc_key(int ch,int sc){
    int rows=NC_LIST_B-NC_LIST_T+1;
    if(nc_dlg){
        int yes=(sc==DXM_SC_ENTER || ch=='\r' || ch=='\n' || ch=='y' || ch=='Y');
        int no =(sc==DXM_SC_ESC || ch==27 || ch=='n' || ch=='N');
        int was=nc_dlg;
        if(was==DLG_HELP || was==DLG_NOTE){ nc_dlg=DLG_NONE; nc_draw(); return; }
        if(!yes && !no) return;
        nc_dlg=DLG_NONE;
        if(yes && was==DLG_QUIT){
            nc_open=0;
            memset(scr,' ',sizeof scr); memset(att,0x07,sizeof att);
            cur_att=0x07; cur_r=cur_c=0;
            st=DOS_OFF;
            return;
        }
        if(yes && was==DLG_DELETE){
            const nc_entry *e=&nc_rows[nc_sel];
            install_clear();
            int left=lib_remove(e->file);
            lib_scan(); nc_rows_build();
            if(nc_sel>=nc_n) nc_sel=nc_n?nc_n-1:0;
            if(left){ snprintf(nc_note[0],sizeof nc_note[0],"Some files could not be removed.");
                      snprintf(nc_note[1],sizeof nc_note[1],"Try again after restarting DXM.");
                      nc_dlg=DLG_NOTE; }
            floppy_req=0.8;                 /* the drive does the work */
        }
        if(yes && was==DLG_RESET){
            const nc_entry *e=&nc_rows[nc_sel];
            char err[96];
            if(lib_reset(e->file,err,sizeof err)==0){
                snprintf(nc_note[0],sizeof nc_note[0],"%s is as it was installed.",e->title);
                nc_note[1][0]=0;
            } else {
                snprintf(nc_note[0],sizeof nc_note[0],"Could not reset %s:",e->title);
                snprintf(nc_note[1],sizeof nc_note[1],"%s",err);
            }
            nc_dlg=DLG_NOTE;
            lib_scan(); nc_rows_build();
            floppy_req=1.2;
        }
        nc_draw();
        return;
    }
    if(sc==SC_F1){ nc_dlg=DLG_HELP; nc_draw(); return; }
    if(sc==DXM_SC_F10){ nc_dlg=DLG_QUIT; nc_draw(); return; }
    if(sc==SC_F2 || sc==SC_F3){
        /* only for something that is actually on the machine */
        if(nc_n && nc_rows[nc_sel].installed){
            inst_status is; install_poll(&is);
            if(is.state!=INST_RUNNING){ nc_dlg=(sc==SC_F2)?DLG_DELETE:DLG_RESET; nc_draw(); }
        }
        return;
    }
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
    sayln(in_games ? " Directory of C:\\GAMES" : " Directory of C:\\");
    put('\n');
    if(!in_games){
        sayln("COMMAND  COM        54,645  05-31-94   6:22a");
        sayln("AUTOEXEC BAT           435  05-31-94   6:22a");
        sayln("CONFIG   SYS           246  05-31-94   6:22a");
        sayln("README   TXT         1,204  08-30-26  11:04a");
        sayln("NC       EXE        41,272  06-08-93  10:14a");
        sayln("GAMES        <DIR>           08-30-26  11:04a");
        put('\n');
        sayln("        5 file(s)          97,802 bytes");
        sayln("        1 dir(s)");
    } else {
        sayln(".            <DIR>           08-30-26  11:04a");
        sayln("..           <DIR>           08-30-26  11:04a");
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
          snprintf(ln,sizeof ln,"        %d file(s)",lib_count());
          sayln(ln); }
    }
    sayln("                      536,870,912 bytes free");
}
static void cmd_help(void){
    sayln("DXM-DOS command reference");
    put('\n');
    sayln("DIR        List the files on this machine.");
    sayln("CLS        Clear the screen.");
    sayln("VER        Show the DOS version.");
    sayln("TYPE file  Display a text file.");
    sayln("CD dir     Change directory.  The games are in C:\\GAMES.");
    sayln("NC         Browse the games in a dual-pane navigator.");
    for(int i=0;i<lib_count();i++){
        const lib_game *g=lib_at(i);
        char nm[16], ln[96]; int k=0;
        for(;g->id[k] && k<10;k++) nm[k]=(char)toupper((unsigned char)g->id[k]);
        while(k<10) nm[k++]=' ';
        nm[10]=0;
        snprintf(ln,sizeof ln,"%s Run %s (from C:\\GAMES).",nm,g->title);
        sayln(ln);
    }
    sayln("EXIT       Switch the machine off.");
}
static void run(char *s){
    while(*s==' ') s++;
    for(char *p=s;*p;p++) if(*p>='a'&&*p<='z') *p-=32;
    /* DOS took CD.. and CD\GAMES with no space, because CD did not need a
     * delimiter before a path.  Put one in before the splitter runs, rather
     * than teaching the splitter about one command. */
    if(s[0]=='C'&&s[1]=='D'&&(s[2]=='.'||s[2]=='\\'||s[2]=='/')){
        memmove(s+3,s+2,strlen(s+2)+1);
        s[2]=' ';
    }
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
            sayln("Type NC to browse the games, or CD GAMES to run one.");
        } else { say("File not found - "); sayln(arg?arg:""); }
    }
    else if(!strcmp(s,"EXIT")) { st=DOS_OFF; return; }
    else if(!strcmp(s,"NC")){ lib_scan(); nc_rows_build();
                              nc_open=1; nc_sel=0; nc_draw(); return; }
    else if(!strcmp(s,"CD")||!strcmp(s,"CHDIR")){
        if(!arg||!*arg){ sayln(in_games?"C:\\GAMES":"C:\\"); }
        else if(!strcmp(arg,"\\")||!strcmp(arg,"/")) in_games=0;
        else if(!strcmp(arg,"..")){
            if(in_games) in_games=0;
            else sayln("Invalid directory");
        }
        else if(!strcmp(arg,".")) { /* stay put */ }
        else if(!in_games && (!strcmp(arg,"GAMES")||!strcmp(arg,"\\GAMES")))
            in_games=1;
        else if(in_games && !strcmp(arg,"\\GAMES")) { /* already there */ }
        else sayln("Invalid directory");
    }
    else if(!strcmp(s,"FORMAT"))
        sayln("Nice try.");
    else {
        /* Anything else may be an installed game - but only from the
         * directory the games are actually in.  DOS did not search the disk
         * for you, and neither does this. */
        const lib_game *g=in_games?lib_find(s):NULL;
        if(!g && !in_games && lib_find(s))
            sayln("Bad command or file name - try CD GAMES");
        else if(!g)        sayln("Bad command or file name");
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
    if(ch=='\b'){ if(line_n){ line_n--; if(cur_c>prompt_len) cur_c--; scr[cur_r][cur_c]=' '; } return; }
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
        /* The catalogue is fetched on a thread, so it arrives AFTER the panel
         * has already been painted.  Rebuilding the rows is therefore only
         * half of it: without the redraw the text screen keeps the empty list
         * it was drawn with, and the catalogue appears only on the NEXT run,
         * off the disk cache.  The other two rows_build sites already
         * repaint - this one did not, which is why a first run on a machine
         * with no cache showed an empty navigator until a key was pressed. */
        if(cat_refresh_collect()>0){ nc_rows_build(); nc_draw(); }
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
