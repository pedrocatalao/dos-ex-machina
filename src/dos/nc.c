/* nc.c — NC.EXE, the dual-pane navigator (see the note below). */
#include "internal.h"
#include <SDL3/SDL.h>      /* SDL_TimeToDateTime, for the table's dates */
#include "library.h"
#include "catalog.h"
#include "install.h"
#include "art.h"
#include "dxm_core.h"
#include "gen/road.h"
#include <ctype.h>

/* ---- NC.EXE: the dual-pane navigator --------------------------------
 * Norton Commander's shape, because it is the shape anyone who used one of
 * these machines already knows: a panel of what you can run on the left, a
 * panel describing the highlighted entry on the right, and the function-key
 * bar along the bottom.  Everything here draws straight into the character
 * grid and its attribute plane; the one exception is the artwork, which is
 * pixels and so is composited after the text in dos_render(). */
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
#define SC_F4  0x3E
#define SC_TAB 0x0F

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
    const char *version;               /* on disk if installed, else offered */
    const char *offered;               /* the catalogue's current release */
    int update;                        /* offered differs from what is on disk */
    int64_t played_ns;                 /* for the table's date column */
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
        e->played_ns=g->played_ns;
        e->version=g->version[0]?g->version:NULL;
        e->art=dxm_road; e->aw=DXM_ROAD_W; e->ah=DXM_ROAD_HT;
        /* An installed game still wants its picture AND its description -
         * the catalogue is the only place either lives, and they do not stop
         * being true once the game is on the disk. */
        e->cat=cat_find(g->id);
        if(e->cat){
            for(int k=0;k<CAT_DESC;k++)
                e->desc[k]=e->cat->desc[k][0]?e->cat->desc[k]:NULL;
            /* installed before the release was recorded: the catalogue's
             * word is the best there is */
            if(!e->version && e->cat->version[0]) e->version=e->cat->version;
            e->offered=e->cat->version[0]?e->cat->version:NULL;
            /* an update is a release on offer that is not the one on disk,
             * for a machine the catalogue has a build for */
            e->update=e->cat->have_module && e->version && e->offered
                      && strcmp(e->version,e->offered)!=0;
        }
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
        e->version=c->version[0]?c->version:NULL;
        /* A game with no build for this machine is listed but cannot be
         * fetched - saying so is more use than leaving it out. */
        e->note=c->have_module?NULL:"no build for this machine yet";
        e->available=c->have_module;
        for(int k=0;k<CAT_DESC;k++) e->desc[k]=c->desc[k][0]?c->desc[k]:NULL;
    }
}

static int nc_open, nc_sel;
static int nc_from_games;     /* the directory the prompt was in when NC opened */
static int nc_launched;       /* a game was started from NC: come back to it */
static int nc_focus;       /* 0 = the list, 1 = the description  */
static int nc_dtop;        /* first description line on screen   */
/* A dialog over the panels.  The navigator's keys go to it while it is up,
 * and it says what Enter and Esc mean. */
enum { DLG_NONE=0, DLG_HELP, DLG_DELETE, DLG_RESET, DLG_UPDATE, DLG_NOTE };
static int  nc_dlg;
static char nc_note[2][64];             /* what DLG_NOTE has to say */
static int    nc_flash;                 /* key-bar slot lit by a press, 1..10 */
static double nc_flash_until;
/* the acknowledgement: the slot lights for a moment, and if the key had
 * nothing to do the panel says why - a press that changes nothing on
 * screen is a press the user cannot tell from a key that does not work */
static void nc_flash_key(int slot){ nc_flash=slot; nc_flash_until=machine.now+0.18; }
static void nc_say(const char *l1,const char *l2){
    snprintf(nc_note[0],sizeof nc_note[0],"%s",l1);
    snprintf(nc_note[1],sizeof nc_note[1],"%s",l2?l2:"");
    nc_dlg=DLG_NOTE;
}
int nc_is_open(void){ return nc_open; }

/* panel geometry, in cells */
#define NC_TOP    0
#define NC_BOT    21
#define NC_LX     0
#define NC_LW     40
#define NC_RX     40
#define NC_RW     40
#define NC_LIST_T 1                    /* the column headings            */
#define NC_LIST_B 19                   /* last list row                  */
/* the table's columns, as cell offsets inside the panel frame */
#define NC_C_NAME 1
#define NC_C_VER  16
#define NC_C_PLAY 27
#define NC_SEP1   15
#define NC_SEP2   26
#define NC_ART_T  1                    /* art rows in the right panel    */
#define NC_ART_B  12
#define NC_DESC_T 14

/* a framed box, with its title inlaid in the top run */
static void nbox(int x,int y,int w,int h,const char *title,int active){
    term_cell(y,x,BX_TL,A_PANEL); term_cell(y,x+w-1,BX_TR,A_PANEL);
    term_cell(y+h-1,x,BX_BL,A_PANEL); term_cell(y+h-1,x+w-1,BX_BR,A_PANEL);
    term_fill_row(y,x+1,w-2,BX_H,A_PANEL);
    term_fill_row(y+h-1,x+1,w-2,BX_H,A_PANEL);
    for(int r=y+1;r<y+h-1;r++){
        term_cell(r,x,BX_V,A_PANEL); term_cell(r,x+w-1,BX_V,A_PANEL);
        term_fill_row(r,x+1,w-2,' ',A_PANEL);
    }
    if(title){
        int n=(int)strlen(title);
        int tx=x+(w-n-2)/2;
        term_cell(y,tx-1,' ',A_PANEL);
        term_puts(y,tx,title,active?A_SEL:A_HEAD);
        term_cell(y,tx+n,' ',A_PANEL);
    }
}
static void nrule(int x,int y,int w){          /* a divider across a panel */
    term_cell(y,x,BX_TE,A_PANEL); term_cell(y,x+w-1,BX_TW,A_PANEL);
    term_fill_row(y,x+1,w-2,BX_H,A_PANEL);
}

/* DOS wrote its dates day-month-year with two-digit years, and so does
 * this: a 1993 machine would not know what else to do with a 2026. */
static void nc_date(int64_t ns,char *out,size_t n){
    SDL_DateTime dt;
    if(ns<=0 || !SDL_TimeToDateTime(ns,&dt,true)){ out[0]=0; return; }
    snprintf(out,n,"%02d-%02d-%02d",dt.day,dt.month,dt.year%100);
}

/* A grey box over the panels with a shadow to its lower right, the way
 * Norton's own dialogs sat.  Lines are centred; the last carries the keys. */
static void nc_dialog(const char *title,const char *const *lines,int n,
                      const char *keys,int left){
    int w=50, h=n+4, x=(DOS_COLS-w)/2, y=6;
    /* the shadow first, so the box paints over its inner edge */
    for(int r=y+1;r<y+h+1;r++) for(int c=x+2;c<x+w+2;c++)
        term_set_attr(r,c,A_SHDW);
    for(int r=y;r<y+h;r++) term_fill_row(r,x,w,' ',A_DLG);
    term_cell(y,x,BX_TL,A_DLG); term_cell(y,x+w-1,BX_TR,A_DLG);
    term_cell(y+h-1,x,BX_BL,A_DLG); term_cell(y+h-1,x+w-1,BX_BR,A_DLG);
    term_fill_row(y,x+1,w-2,BX_H,A_DLG); term_fill_row(y+h-1,x+1,w-2,BX_H,A_DLG);
    for(int r=y+1;r<y+h-1;r++){ term_cell(r,x,BX_V,A_DLG); term_cell(r,x+w-1,BX_V,A_DLG); }
    if(title){ int tn=(int)strlen(title);
               term_puts(y,x+(w-tn-2)/2+1,title,A_DLG);
               term_cell(y,x+(w-tn-2)/2,' ',A_DLG); term_cell(y,x+(w-tn-2)/2+1+tn,' ',A_DLG); }
    /* a table of keys reads left-aligned; a question reads centred */
    for(int k=0;k<n;k++){
        int ln=(int)strlen(lines[k]);
        term_puts(y+2+k,left?x+3:x+(w-ln)/2,lines[k],A_DLG);
    }
    if(keys){ int kn=(int)strlen(keys); term_puts(y+h-2,x+(w-kn)/2,keys,A_DLGK); }
}

static void nc_draw(void){
    term_fill(' ',A_PANEL);

    /* ---- left panel: what you can run ---- */
    /* GAMES, not C:\GAMES - the panel is not a directory listing.  It shows
     * what is installed AND what could be, and only half of that is on the
     * disk that path would name. */
    nbox(NC_LX,NC_TOP,NC_LW,NC_BOT-NC_TOP+1,"GAMES",nc_focus==0);
    /* One game a row, with when it arrived and when it last ran - the
     * "full" view of the real thing, where a file had a date beside it.
     * The headings sit in the first row; the list scrolls under them. */
    { int rows=NC_LIST_B-NC_LIST_T;             /* rows for entries */
      static int top=0;
      if(nc_sel<top) top=nc_sel;
      if(nc_sel>=top+rows) top=nc_sel-rows+1;
      if(top<0) top=0;
      term_puts(NC_LIST_T,NC_LX+NC_C_NAME+1,"Name",A_HEAD);
      term_puts(NC_LIST_T,NC_LX+NC_C_VER,"Version",A_HEAD);
      term_puts(NC_LIST_T,NC_LX+NC_C_PLAY,"Last played",A_HEAD);
      for(int r=NC_LIST_T;r<=NC_LIST_B;r++){
        term_cell(r,NC_LX+NC_SEP1,BX_V,A_PANEL);
        term_cell(r,NC_LX+NC_SEP2,BX_V,A_PANEL);
      }
      for(int k=0;k<rows;k++){
        int i=top+k; if(i>=nc_n) break;
        int y=NC_LIST_T+1+k;
        const nc_entry *e=&nc_rows[i];
        /* the bar belongs to the focused panel: with the description
         * active the list keeps its place but shows no cursor */
        uint8_t a=(i==nc_sel&&nc_focus==0)?A_SEL:(e->installed?A_NAME:A_DIM);
        term_fill_row(y,NC_LX+1,NC_LW-2,' ',a);
        term_cell(y,NC_LX+NC_SEP1,BX_V,a); term_cell(y,NC_LX+NC_SEP2,BX_V,a);
        /* What is NOT on the disk is marked after its name, where a
         * listing puts a file's attributes; its version is the one on
         * offer, and it has never been played here. */
        /* the name as DIR would show it: upper case, the way the disk
         * holds it */
        { char up[16]; int u=0;
          for(;e->file[u] && u<15;u++) up[u]=(char)toupper((unsigned char)e->file[u]);
          up[u]=0;
          term_puts(y,NC_LX+NC_C_NAME+1,up,a); }
        if(e->version){
            term_puts(y,NC_LX+NC_C_VER,e->version,a);
            /* a newer release on offer: the mark points up, the way the
             * not-yet-fetched mark points down */
            if(e->update) term_cell(y,NC_LX+NC_C_VER+(int)strlen(e->version)+1,0x18,a);
        }
        if(!e->installed){
            int ax=NC_LX+NC_C_NAME+1+(int)strlen(e->file)+1;
            if(ax<NC_LX+NC_SEP1) term_cell(y,ax,0x19,a);
            continue;
        }
        char d[12];
        nc_date(e->played_ns,d,sizeof d);
        term_puts(y,NC_LX+NC_C_PLAY,e->played_ns>0?d:"never",a);
      }
    }
    nrule(NC_LX,NC_BOT-1,NC_LW);
    { char b[48];
      snprintf(b,sizeof b," %d file(s)",nc_n);
      term_puts(NC_BOT,NC_LX+2,b,A_PANEL); }

    /* ---- right panel: the highlighted entry ---- */
    if(nc_n==0){
        nbox(NC_RX,NC_TOP,NC_RW,NC_BOT-NC_TOP+1,"NOTHING INSTALLED",nc_focus==1);
        term_puts(NC_DESC_T,  NC_RX+2,"No games on this machine.",A_TEXT);
        term_puts(NC_DESC_T+2,NC_RX+2,"Put a .dxm module and its",A_TEXT);
        term_puts(NC_DESC_T+3,NC_RX+2,"data under:",A_TEXT);
        term_puts(NC_DESC_T+5,NC_RX+2,"  games/<name>/",A_NAME);
    } else {
    const nc_entry *e=&nc_rows[nc_sel];
    nbox(NC_RX,NC_TOP,NC_RW,NC_BOT-NC_TOP+1,e->title,nc_focus==1);
    /* the art well is painted black here; the pixels land on top of it in
     * dos_render(), which is the only place this program is not text */
    for(int r=NC_ART_T;r<=NC_ART_B;r++) term_fill_row(r,NC_RX+1,NC_RW-2,' ',0x00);
    nrule(NC_RX,NC_ART_B+1,NC_RW);
    /* The description, then one line saying what Enter does here.  An entry
     * with no account of itself is the worst case: it is on screen, so
     * something has to explain it. */
    /* Wrap here rather than making the catalogue count columns.  A catalogue
     * that has to know the panel width is a catalogue that breaks the first
     * time the panel changes.  The wrapped lines go into a list first, and
     * a window of it is shown: with Tab on the panel, up and down move the
     * window, since four rows is not much room for a description. */
    enum { DL_MAX=48 };
    static char  dl[DL_MAX][NC_RW]; static uint8_t da[DL_MAX];
    int nl=0;
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
                if(nl<DL_MAX){ memcpy(dl[nl],line,(size_t)n+1); da[nl++]=A_TEXT; }
                n=0;
            }
            if(n) line[n++]=' ';
            memcpy(line+n,w,(size_t)len); n+=len; line[n]=0;
            w=end;
        }
        if(n){ if(nl<DL_MAX){ memcpy(dl[nl],line,(size_t)n+1); da[nl++]=A_TEXT; }
               n=0; line[0]=0; }
      }
    }
    if(e->note && e->note[0]){
        if(!e->installed && !e->available && nl<DL_MAX){
            snprintf(dl[nl],NC_RW,"UNAVAILABLE"); da[nl++]=A_HEAD; }
        if(nl<DL_MAX){ snprintf(dl[nl],NC_RW,"%s",e->note); da[nl++]=A_TEXT; }
    }
    { const int avail=NC_BOT-4-NC_DESC_T;         /* rows the window has */
      if(nc_dtop>nl-avail) nc_dtop=nl-avail;
      if(nc_dtop<0) nc_dtop=0;
      for(int k=0;k<avail && nc_dtop+k<nl;k++)
          term_puts(NC_DESC_T+k,NC_RX+2,dl[nc_dtop+k],da[nc_dtop+k]);
      /* what is out of view is said on the frame, where a scroll bar
       * would go, so the reader knows there is more */
      if(nc_dtop>0)          term_cell(NC_DESC_T,        NC_RX+NC_RW-1,0x18,A_HEAD);
      if(nc_dtop+avail<nl)   term_cell(NC_DESC_T+avail-1,NC_RX+NC_RW-1,0x19,A_HEAD);
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
          term_puts(NC_BOT-4,NC_RX+2,is.stage,A_HEAD);
          term_puts(NC_BOT-3,NC_RX+2,bar,A_NAME);
          { char b[48];
            if(is.total>0) snprintf(b,sizeof b,"%.0f%%  %.1f of %.1f MB",
                                    is.frac*100.0,is.got/1048576.0,is.total/1048576.0);
            else           snprintf(b,sizeof b,"%.1f MB",is.got/1048576.0);
            term_puts(NC_BOT-2,NC_RX+2,b,A_TEXT); }
      } else if(is.state==INST_FAILED && !strcmp(is.id,e->file)){
          term_puts(NC_BOT-3,NC_RX+2,"DOWNLOAD FAILED",A_HEAD);
          term_puts(NC_BOT-2,NC_RX+2,is.err,A_TEXT);
      } else if(e->installed){
          if(e->update){
              char b[48];
              double mb=(e->cat->module.size+e->cat->data.size)/1048576.0;
              snprintf(b,sizeof b,"F4 to update to %s  (%.1f MB)",e->offered,mb);
              term_puts(NC_BOT-3,NC_RX+2,b,A_HEAD);
          }
          term_puts(NC_BOT-2,NC_RX+2,"ENTER to play",A_NAME);
      } else if(e->available){
          char b[48];
          double mb=(e->cat->module.size+e->cat->data.size)/1048576.0;
          snprintf(b,sizeof b,"ENTER to download  (%.1f MB)",mb);
          term_puts(NC_BOT-2,NC_RX+2,b,A_NAME);
      }
    }
    nrule(NC_RX,NC_BOT-1,NC_RW);
    { char foot[64];
      if(e->year) snprintf(foot,sizeof foot,"%s, %d",e->by,e->year);
      else        snprintf(foot,sizeof foot,"%s",e->by);
      term_puts(NC_BOT,NC_RX+2,foot,A_PANEL); }
    }

    /* ---- the command line, and the key bar ---- */
    term_fill_row(22,0,DOS_COLS,' ',0x07);
    /* the panel IS the games directory, so that is where the command
     * line sits - and where the prompt is left when the panel closes */
    term_puts(23,0,"C:\\GAMES>",0x07);
    term_fill_row(23,9,DOS_COLS-9,' ',0x07);
    /* Ten slots of eight cells - exactly the eighty columns.  The number
     * sits right-aligned in two cells so "10" takes no more room than "1",
     * and the label has six, which is what Norton's own bar gave it. */
    static const char *KEYS[10]={
        "Help  ","Delete","Reset ","Update","      ",
        "      ","      ","      ","      ","Quit  " };
    for(int k=0;k<10;k++){
        char num[3]; snprintf(num,sizeof num,"%2d",k+1);
        int lit=(nc_flash==k+1);
        term_puts(24,k*8,num,lit?A_BAR:A_BARN);
        term_puts(24,k*8+2,KEYS[k],lit?A_NAME:A_BAR);
    }

    /* ---- whatever is asking a question sits on top of it all ---- */
    if(nc_dlg==DLG_HELP){
        static const char *const L[]={
            "ENTER      play the game, or download it",
            "ARROWS     move/scroll up and down",
            "TAB        switch panels (left/right)",
            "F2         delete the game from this machine",
            "F3         reset saved games and settings",
            "F4         update it to the release on offer",
            "F10, ESC   back to the prompt",
        };
        nc_dialog("NC HELP",L,8,"press any key",1);
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
    } else if(nc_dlg==DLG_UPDATE){
        const nc_entry *e=&nc_rows[nc_sel];
        char l1[64];
        snprintf(l1,sizeof l1,"Update %s from %s to %s?",e->title,e->version,e->offered);
        const char *L[]={l1,"Saved games are kept; settings may not be."};
        nc_dialog("UPDATE",L,2,"ENTER yes    ESC no",0);
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
        uint8_t a=term_attr_at(cr,cc);
        if(a==A_DLG||a==A_DLGK) return;
        if(a==A_SHDW){ r=r*35/100; g=g*35/100; b=b*35/100; }
    }
    uint8_t *q=term_fb()+((size_t)dy*DOS_W+dx)*3;
    q[0]=(uint8_t)(q[0]*(1.0f-al)+r*al);
    q[1]=(uint8_t)(q[1]*(1.0f-al)+g*al);
    q[2]=(uint8_t)(q[2]*(1.0f-al)+b*al);
}

void nc_draw_art(void){
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

/* Up and down walk the table a row at a time; left and right a page. */
void nc_key(int ch,int sc){
    int rows=NC_LIST_B-NC_LIST_T;           /* a page of the table */
    if(nc_dlg){
        int yes=(sc==DXM_SC_ENTER || ch=='\r' || ch=='\n' || ch=='y' || ch=='Y');
        int no =(sc==DXM_SC_ESC || ch==27 || ch=='n' || ch=='N');
        int was=nc_dlg;
        if(was==DLG_HELP || was==DLG_NOTE){ nc_dlg=DLG_NONE; nc_draw(); return; }
        if(!yes && !no) return;
        nc_dlg=DLG_NONE;
        if(yes && was==DLG_DELETE){
            const nc_entry *e=&nc_rows[nc_sel];
            install_clear();
            int left=lib_remove(e->file);
            lib_scan(); nc_rows_build();
            if(nc_sel>=nc_n) nc_sel=nc_n?nc_n-1:0;
            if(left){ snprintf(nc_note[0],sizeof nc_note[0],"Some files could not be removed.");
                      snprintf(nc_note[1],sizeof nc_note[1],"Try again after restarting DXM.");
                      nc_dlg=DLG_NOTE; }
            machine.floppy_req=0.8;                 /* the drive does the work */
        }
        if(yes && was==DLG_UPDATE){
            const nc_entry *e=&nc_rows[nc_sel];
            const lib_game *g=lib_find(e->file);
            if(g) lib_unload(g);            /* the download overwrites game.dxm */
            install_clear();
            install_start(e->cat);          /* the same path as a first install */
            machine.floppy_req=1.4;
        }
        if(yes && was==DLG_RESET){
            const nc_entry *e=&nc_rows[nc_sel];
            char err[96];
            int r=lib_reset(e->file,err,sizeof err);
            if(r==0){
                snprintf(nc_note[0],sizeof nc_note[0],"%s is as it was installed.",e->title);
                nc_note[1][0]=0;
                nc_dlg=DLG_NOTE;
            } else if(r>0 && e->cat && e->cat->have_module){
                /* installed before the archive was kept: fetch the data
                 * again over what is there - the same panel shows the
                 * progress, and the archive is kept this time */
                const lib_game *g=lib_find(e->file);
                if(g) lib_unload(g);
                install_clear();
                install_start_data(e->cat);
            } else {
                snprintf(nc_note[0],sizeof nc_note[0],"Could not reset %s:",e->title);
                /* the note holds 63 characters; a longer error is cut, not
                 * wrapped, and %.63s says so where GCC can see it */
                snprintf(nc_note[1],sizeof nc_note[1],"%.63s",r>0?"no archive, and nothing to fetch":err);
                nc_dlg=DLG_NOTE;
            }
            lib_scan(); nc_rows_build();
            machine.floppy_req=1.2;
        }
        nc_draw();
        return;
    }
    if(sc==SC_F1){ nc_flash_key(1); nc_dlg=DLG_HELP; nc_draw(); return; }
    if(sc==SC_F2 || sc==SC_F3 || sc==SC_F4){
        nc_flash_key(sc==SC_F2?2:sc==SC_F3?3:4);
        const nc_entry *e=nc_n?&nc_rows[nc_sel]:NULL;
        inst_status is; install_poll(&is);
        char l1[64];
        if(!e)                       nc_say("There is nothing here to act on.",NULL);
        else if(is.state==INST_RUNNING) nc_say("A download is running.","Wait for it to finish.");
        else if(!e->installed){
            snprintf(l1,sizeof l1,"%s is not installed.",e->title);
            nc_say(l1,"ENTER downloads it.");
        }
        else if(sc==SC_F4 && !e->update){
            snprintf(l1,sizeof l1,"%s is already up to date.",e->title);
            nc_say(l1,e->version?e->version:NULL);
        }
        else nc_dlg=(sc==SC_F2)?DLG_DELETE:(sc==SC_F3)?DLG_RESET:DLG_UPDATE;
        nc_draw();
        return;
    }
    /* F10 and Esc both leave the panel, back to the prompt */
    if(sc==DXM_SC_F10) nc_flash_key(10);
    if(sc==DXM_SC_ESC || ch==27 || sc==DXM_SC_F10){
        nc_open=0; shell_set_dir(nc_from_games);   /* back where it was opened */
        term_clear(); shell_prompt();
        return;
    }
    if(sc==SC_TAB){ if(nc_n) nc_focus=!nc_focus; nc_draw(); return; }
    if(nc_focus==1){
        /* the description has the keys: up and down move its window a
         * line, left and right a page; nc_draw clamps to what there is */
        const int avail=NC_BOT-4-NC_DESC_T;
        if(sc==DXM_SC_DOWN)       nc_dtop++;
        else if(sc==DXM_SC_UP)    { if(nc_dtop>0) nc_dtop--; }
        else if(sc==DXM_SC_RIGHT) nc_dtop+=avail;
        else if(sc==DXM_SC_LEFT)  { nc_dtop-=avail; if(nc_dtop<0) nc_dtop=0; }
    }
    else if(sc==DXM_SC_DOWN)      { if(nc_sel+1<nc_n){ nc_sel++; nc_dtop=0; } }
    else if(sc==DXM_SC_UP)   { if(nc_sel>0){ nc_sel--; nc_dtop=0; } }
    else if(sc==DXM_SC_RIGHT){ nc_sel+=rows; if(nc_sel>=nc_n) nc_sel=nc_n?nc_n-1:0; nc_dtop=0; }
    else if(sc==DXM_SC_LEFT) { nc_sel-=rows; if(nc_sel<0) nc_sel=0; nc_dtop=0; }
    else if(sc==DXM_SC_ENTER || ch=='\r' || ch=='\n'){
        const nc_entry *e=&nc_rows[nc_sel];
        inst_status is; install_poll(&is);
        if(!e->installed && e->available && is.state!=INST_RUNNING){
            install_clear();
            install_start(e->cat);
            machine.floppy_req=1.4;               /* the drive answers, as it would */
            nc_draw();
            return;
        }
        if(e->installed){
            /* the same launch the prompt gives: the panel goes, the
             * command appears as if typed, and the drive reads while the
             * screen waits - and the machine remembers to come back */
            nc_open=0; nc_launched=1;
            term_clear();
            shell_prompt();
            { char up[16]; int k=0;
              for(;e->cmd[k] && k<15;k++) up[k]=(char)toupper((unsigned char)e->cmd[k]);
              up[k]=0; term_sayln(up); }
            dos_launch(e->cmd);
            return;
        }
    }
    nc_draw();
}

/* The panel opens over the prompt: the library is rescanned, the rows
 * rebuilt, the selection at the top, and the prompt's directory kept so
 * leaving goes back to it. */
void nc_open_panel(int from_games){
    lib_scan(); nc_rows_build();
    nc_from_games=from_games;
    nc_open=1; nc_sel=0; nc_focus=0; nc_dtop=0;
    nc_draw();
}

/* Each frame while the panel is up. */
void nc_update(double t){
    if(nc_flash && t>=nc_flash_until){ nc_flash=0; nc_draw(); }
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
        machine.floppy_req=0.6;
    }
    if(is.state==INST_RUNNING || is.state!=was) nc_draw();
    was=is.state;
}

/* A game started from the navigator has ended: come back to it, on the
 * same entry, with whatever the game left on disk reflected. */
int nc_resume_after_game(void){
    if(!nc_launched) return 0;
    nc_launched=0;
    lib_scan(); nc_rows_build();
    if(nc_sel>=nc_n) nc_sel=nc_n?nc_n-1:0;
    nc_open=1; nc_draw();
    return 1;
}
