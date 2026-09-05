/* disk.c — see disk.h. */
#include "disk.h"
#include "library.h"
#include "coreload.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static char root[LIB_PATH];      /* <pref>c<sep> */

/* The seed disk.  Text as it would have been typed; the two programs are
 * stubs - a few recognisable bytes and filler - there to be listed and to
 * refuse TYPE, the way a real one would have filled the screen with
 * garbage. */
static const char AUTOEXEC[] =
    "@ECHO OFF\r\n"
    "PROMPT $P$G\r\n"
    "PATH C:\\;C:\\GAMES\r\n"
    "SET BLASTER=A220 I5 D1 T4\r\n"
    "SET TEMP=C:\\TEMP\r\n"
    "LH C:\\DOS\\MOUSE.COM\r\n"
    "ECHO.\r\n"
    "ECHO Type NC to browse the games, or TYPE README.1ST for help.\r\n";
static const char CONFIGSYS[] =
    "DEVICE=C:\\DOS\\HIMEM.SYS\r\n"
    "DEVICE=C:\\DOS\\EMM386.EXE NOEMS\r\n"
    "DOS=HIGH,UMB\r\n"
    "FILES=30\r\n"
    "BUFFERS=20\r\n"
    "STACKS=9,256\r\n"
    "SHELL=C:\\COMMAND.COM C:\\ /P\r\n";
static const char README1ST[] =
    "DOS ex Machina - a 1993 PC that only runs games.\r\n"
    "\r\n"
    "NC          Browse the games.  ENTER plays one, or downloads it\r\n"
    "            if it is not here yet.  F1 in NC lists its keys.\r\n"
    "CD GAMES    The games live in C:\\GAMES; type a game's name to\r\n"
    "            run it from there.\r\n"
    "DIR, TYPE   Look around.  This disk is real: you can open its\r\n"
    "            files in an editor.  A seed file you delete is put\r\n"
    "            back at the next start.\r\n"
    "EXIT        Switch the machine off.\r\n"
    "\r\n"
    "The brightness and contrast knobs are under the right speaker.\r\n"
    "F1 at the prompt opens the CRT panel.  Ctrl+F10 hands the mouse\r\n"
    "to the game, or takes it back.\r\n";

typedef struct { const char *name; const char *text; int stub_size; } seed;
static const seed SEEDS[] = {
    { "COMMAND.COM",  NULL,      54645 },
    { "AUTOEXEC.BAT", AUTOEXEC,  0 },
    { "CONFIG.SYS",   CONFIGSYS, 0 },
    { "README.1ST",   README1ST, 0 },
    { "NC.EXE",       NULL,      41272 },
};
#define NSEEDS ((int)(sizeof SEEDS/sizeof SEEDS[0]))

static int exists(const char *p){ SDL_PathInfo st; return SDL_GetPathInfo(p,&st); }

static void write_seed(const seed *s){
    char path[LIB_PATH];
    snprintf(path,sizeof path,"%s%s",root,s->name);
    FILE *f=fopen(path,"wb"); if(!f) return;
    if(s->text) fputs(s->text,f);
    else {
        /* a stub program: a header line, then filler that is plainly not
         * text, so TYPE knows what it is looking at */
        char hdr[80]; snprintf(hdr,sizeof hdr,"DXM-DOS %s  (C) 2026 DOS ex Machina\r\n\x1a",s->name);
        fputs(hdr,f);
        for(int i=(int)strlen(hdr);i<s->stub_size;i++) fputc((i*37+11)&0xFF,f);
    }
    fclose(f);
}

void disk_init(void){
    snprintf(root,sizeof root,"%sc%c",lib_root(),DXM_SEP);
    { char d[LIB_PATH]; snprintf(d,sizeof d,"%sc",lib_root());
      if(!exists(d)) SDL_CreateDirectory(d); }
    for(int i=0;i<NSEEDS;i++){
        char path[LIB_PATH];
        snprintf(path,sizeof path,"%s%s",root,SEEDS[i].name);
        if(!exists(path)) write_seed(&SEEDS[i]);
    }
}

/* a host file name as DOS would show it: upper case, 8.3, truncated */
static void dos_name(const char *in,char *out){
    const char *dot=strrchr(in,'.');
    int n=0;
    for(const char *p=in;*p && p!=dot && n<8;p++) if(*p!=' ') out[n++]=(char)toupper((unsigned char)*p);
    if(dot && dot[1]){ out[n++]='.'; int e=0;
        for(const char *p=dot+1;*p && e<3;p++) out[n++]=(char)toupper((unsigned char)*p), e++; }
    out[n]=0;
}

typedef struct { disk_entry *out; int n, max; } lister;
static SDL_EnumerationResult SDLCALL on_entry(void *ud,const char *dirname,const char *fname){
    lister *l=(lister *)ud;
    if(l->n>=l->max) return SDL_ENUM_SUCCESS;
    char full[LIB_PATH]; snprintf(full,sizeof full,"%s%c%s",dirname,DXM_SEP,fname);
    SDL_PathInfo st; if(!SDL_GetPathInfo(full,&st)) return SDL_ENUM_CONTINUE;
    if(fname[0]=='.') return SDL_ENUM_CONTINUE;         /* dotfiles are not DOS */
    disk_entry *e=&l->out[l->n++];
    dos_name(fname,e->name);
    e->is_dir=(st.type==SDL_PATHTYPE_DIRECTORY);
    e->size=(long)st.size; e->mtime_ns=st.modify_time;
    return SDL_ENUM_CONTINUE;
}
static int rank(const disk_entry *e){
    for(int i=0;i<NSEEDS;i++) if(!strcmp(e->name,SEEDS[i].name)) return i;
    return NSEEDS;
}
static int cmp(const void *a,const void *b){
    const disk_entry *x=a,*y=b;
    int rx=rank(x), ry=rank(y);
    if(rx!=ry) return rx-ry;
    return strcmp(x->name,y->name);
}
int disk_list(disk_entry *out,int max){
    lister l={out,0,max};
    char d[LIB_PATH]; snprintf(d,sizeof d,"%sc",lib_root());
    SDL_EnumerateDirectory(d,on_entry,&l);
    qsort(out,(size_t)l.n,sizeof *out,cmp);
    /* GAMES: the library's directory, shown here as the disk's */
    if(l.n<max){
        char g[LIB_PATH]; snprintf(g,sizeof g,"%sgames",lib_root());
        SDL_PathInfo st;
        disk_entry *e=&out[l.n++];
        snprintf(e->name,sizeof e->name,"GAMES"); e->is_dir=1; e->size=0;
        e->mtime_ns=SDL_GetPathInfo(g,&st)?st.modify_time:0;
    }
    return l.n;
}

/* find the host file whose DOS name matches */
typedef struct { const char *want; char found[LIB_PATH]; } finder;
static SDL_EnumerationResult SDLCALL on_find(void *ud,const char *dirname,const char *fname){
    finder *f=(finder *)ud; char dn[13]; dos_name(fname,dn);
    if(!strcmp(dn,f->want)){ snprintf(f->found,sizeof f->found,"%s%c%s",dirname,DXM_SEP,fname); return SDL_ENUM_SUCCESS; }
    return SDL_ENUM_CONTINUE;
}
static int find_file(const char *dosname,char *out,size_t n){
    char want[13]; dos_name(dosname,want);
    finder f={want,{0}};
    char d[LIB_PATH]; snprintf(d,sizeof d,"%sc",lib_root());
    SDL_EnumerateDirectory(d,on_find,&f);
    if(!f.found[0]) return -1;
    snprintf(out,n,"%s",f.found); return 0;
}

int disk_read(const char *dosname,char *buf,size_t n){
    char path[LIB_PATH];
    if(find_file(dosname,path,sizeof path)!=0) return -1;
    FILE *f=fopen(path,"rb"); if(!f) return -1;
    size_t got=fread(buf,1,n-1,f); fclose(f);
    /* text is what a 1993 disk called text: printable, tabs and line ends,
     * and a Ctrl-Z end marker.  Anything else is a program. */
    for(size_t i=0;i<got;i++){
        unsigned char ch=(unsigned char)buf[i];
        if(ch==0x1A){ got=i; break; }
        if(ch<32 && ch!='\t' && ch!='\r' && ch!='\n') return 1;
    }
    buf[got]=0;
    return 0;
}

int disk_autoexec_echo(char (*lines)[80],int max){
    static char buf[4096];
    if(disk_read("AUTOEXEC.BAT",buf,sizeof buf)!=0) return 0;
    int n=0;
    for(char *p=buf; *p && n<max; ){
        char *e=strpbrk(p,"\r\n"); size_t len=e?(size_t)(e-p):strlen(p);
        char line[256]; if(len>=sizeof line) len=sizeof line-1;
        memcpy(line,p,len); line[len]=0;
        char *q=line; while(*q==' '||*q=='@') q++;
        if(!SDL_strncasecmp(q,"ECHO",4) && (q[4]==' '||q[4]=='.'||q[4]==0)){
            const char *txt=q+4;
            if(*txt=='.') txt++;                       /* ECHO. is a blank line */
            else { while(*txt==' ') txt++;
                   if(!SDL_strcasecmp(txt,"OFF")||!SDL_strcasecmp(txt,"ON")){ goto next; } }
            snprintf(lines[n++],80,"%s",txt);
        }
        next:
        p=e?e+1:p+len; if(e && *e=='\r' && *p=='\n') p++;
    }
    return n;
}
