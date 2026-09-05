/* disk.c — see disk.h. */
#include "disk.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* The disk.  Text as it would have been typed; the two programs have a
 * size and a date and refuse TYPE, and that is all a program needs to be
 * here for. */
static const char AUTOEXEC[] =
    "@ECHO OFF\r\n"
    "PROMPT $P$G\r\n"
    "PATH C:\\;C:\\GAMES\r\n"
    "SET BLASTER=A220 I5 D1 T4\r\n"
    "SET TEMP=C:\\TEMP\r\n"
    "LH C:\\DOS\\MOUSE.COM\r\n"
    "ECHO - For a list of available commands, type HELP.\r\n"
    "ECHO - If you know, you know.\r\n"
    "ECHO.\r\n";
static const char CONFIGSYS[] =
    "DEVICE=C:\\DOS\\HIMEM.SYS\r\n"
    "DEVICE=C:\\DOS\\EMM386.EXE NOEMS\r\n"
    "DOS=HIGH,UMB\r\n"
    "FILES=30\r\n"
    "BUFFERS=20\r\n"
    "STACKS=9,256\r\n"
    "SHELL=C:\\COMMAND.COM C:\\ /P\r\n";
static const char README1ST[] =
    "\r\n"
    " _|_|_|    _|      _|  _|      _|\r\n"
    " _|    _|    _|  _|    _|_|  _|_|      DOS EX MACHINA\r\n"
    " _|    _|      _|      _|  _|  _|      READ ME FIRST\r\n"
    " _|    _|    _|  _|    _|      _|\r\n"
    " _|_|_|    _|      _|  _|      _|\r\n"
    "\r\n"
    "You remember this machine.  Maybe not this one exactly, but\r\n"
    "one like it: beige, warm, a little too loud, in a room that\r\n"
    "was yours or a friend\'s or a parent\'s.  You remember the fan\r\n"
    "starting, the drive grinding, the prompt coming up, and the\r\n"
    "moment when a game filled the screen and nothing else in the\r\n"
    "world mattered for an hour.\r\n"
    "\r\n"
    "This is about that.  Not the hardware. The feeling.\r\n"
    "\r\n"
    "So the games here are the real ones, running as they ran,\r\n"
    "behind glass that curves and glows the way it did.  The case\r\n"
    "has its scuffs.  The fan runs.  The drive seeks.  It was built\r\n"
    "by hand, with care, by someone who was there, for anyone who\r\n"
    "was too, and for anyone who wasn\'t, and wonders how it was.\r\n"
    "\r\n"
    "Switch it on.  Stay a while.\r\n"
    "\r\n"
    "                      -------------\r\n"
    "\r\n"
    "Type NC to see the games.  ENTER plays one, or fetches it if\r\n"
    "it is not on the disk yet.\r\n"
    "\r\n"
    "Type HELP for the command reference.\r\n"
    "\r\n"
    "The knobs under the right speaker adjust the picture.  F1 at the\r\n"
    "prompt opens the rest of the controls.  EXIT switches off.\r\n";

/* `ours`: rewritten at every start, because it is DXM's text and should
 * say what the current build says.  The others are the user's once they
 * exist, and are only put back if they go missing. */
typedef struct { const char *name; const char *text; long size; const char *date, *time; } file;
static const file FILES[] = {
    { "COMMAND.COM",  NULL,      54645, "05-31-94", " 6:22a" },
    { "AUTOEXEC.BAT", AUTOEXEC,  0,     "05-31-94", " 6:22a" },
    { "CONFIG.SYS",   CONFIGSYS, 0,     "05-31-94", " 6:22a" },
    { "README.1ST",   README1ST, 0,     "09-02-26", "11:04a" },
    { "NC.EXE",       NULL,      41272, "06-08-93", "10:14a" },
};
#define NFILES ((int)(sizeof FILES/sizeof FILES[0]))

int disk_list(disk_entry *out,int max){
    int n=0;
    for(int i=0;i<NFILES && n<max;i++){
        const file *f=&FILES[i];
        out[n].name=f->name; out[n].is_dir=0;
        out[n].size=f->text?(long)strlen(f->text):f->size;
        out[n].date=f->date; out[n].time=f->time; n++;
    }
    if(n<max){ out[n].name="GAMES"; out[n].is_dir=1; out[n].size=0;
               out[n].date="09-02-26"; out[n].time=" 1:27p"; n++; }
    return n;
}

static const file *find(const char *dosname){
    /* a leading C:\ or \ is tolerated */
    if(!SDL_strncasecmp(dosname,"C:\\",3)) dosname+=3;
    if(*dosname=='\\') dosname++;
    for(int i=0;i<NFILES;i++)
        if(!SDL_strcasecmp(dosname,FILES[i].name)) return &FILES[i];
    return NULL;
}

int disk_read(const char *dosname,char *buf,size_t n){
    const file *f=find(dosname);
    if(!f) return -1;
    if(!f->text) return 1;
    snprintf(buf,n,"%s",f->text);
    return 0;
}

int disk_autoexec_echo(char (*lines)[80],int max){
    char buf[4096];
    if(disk_read("AUTOEXEC.BAT",buf,sizeof buf)!=0) return 0;
    int n=0;
    for(char *p=buf; *p && n<max; ){
        char *e=strpbrk(p,"\r\n"); size_t len=e?(size_t)(e-p):strlen(p);
        char line[256]; if(len>=sizeof line) len=sizeof line-1;
        memcpy(line,p,len); line[len]=0;
        char *q=line; while(*q==' '||*q=='@') q++;
        if(!SDL_strncasecmp(q,"ECHO",4) && (q[4]==' '||q[4]=='.'||q[4]==0)){
            const char *txt=q+4;
            int skip=0;
            if(*txt=='.') txt++;                       /* ECHO. is a blank line */
            else { while(*txt==' ') txt++;
                   if(!SDL_strcasecmp(txt,"OFF")||!SDL_strcasecmp(txt,"ON")) skip=1; }
            if(!skip){
                /* the batch escape: ECHO ^| prints a bare | */
                char *o=lines[n]; int k=0;
                for(const char *q2=txt; *q2 && k<79; q2++){ if(*q2=='^' && q2[1]) q2++; o[k++]=*q2; }
                o[k]=0; n++;
            }
        }
        p=e?e+1:p+len; if(e && *e=='\r' && *p=='\n') p++;
    }
    return n;
}
