/* shell.c — the prompt: the command line, the command set, and MORE.
 * The prompt is the UI: there is no other way to reach anything (SPEC §7). */
#include "internal.h"
#include "disk.h"
#include "library.h"
#include "version.h"  /* VER reports the release */
#include "dxm_core.h" /* DXM_SC_ESC */
#include <ctype.h>

static char cmd[128];
static int cmd_n; /* the command being typed */
/* MORE.  A command whose output is piped to it goes into a buffer instead
 * of the screen, and comes out a screenful at a time: 23 lines, then
 * "-- More --" and a wait for a key.  Any key continues, Esc or Q stops.
 * Exactly what DOS did, which is why it is the answer to a file longer
 * than the screen rather than a scrollback the machine never had. */
static int page_want, page_on;
static char page_buf[16384];
static size_t page_len;
static const char *page_pos;
static void oline(const char *s) {
    if (!page_want) {
        term_sayln(s);
        return;
    }
    size_t l = strlen(s);
    if (page_len + l + 1 >= sizeof page_buf)
        return;
    memcpy(page_buf + page_len, s, l);
    page_len += l;
    page_buf[page_len++] = '\n';
    page_buf[page_len] = 0;
}
static void page_show(void) {
    int rows = 0;
    while (*page_pos && rows < 23) {
        const char *e = strchr(page_pos, '\n');
        char ln[256];
        size_t l = e ? (size_t)(e - page_pos) : strlen(page_pos);
        if (l >= sizeof ln)
            l = sizeof ln - 1;
        memcpy(ln, page_pos, l);
        ln[l] = 0;
        term_sayln(ln);
        page_pos = e ? e + 1 : page_pos + l;
        rows++;
    }
    if (*page_pos)
        term_say("-- More --");
    else {
        page_on = 0;
        shell_prompt();
    }
}
static void page_begin(void) {
    page_want = 0;
    if (!page_len) {
        return;
    }
    page_pos = page_buf;
    page_on = 1;
    page_show();
}
static void page_key(int ch) {
    if (ch == 27 || ch == 'q' || ch == 'Q' || ch == 3) {
        page_on = 0;
        term_put('\n');
        shell_prompt();
        return;
    }
    term_put('\n');
    page_show();
}
/* The machine has one subdirectory that matters: games install into
 * C:\GAMES, and running one from the prompt means going there first, the
 * way it would have.  NC reaches them wherever you are - it is a program
 * that browses the disk, not a shortcut around it. */
static int in_games;   /* 0 = C:\, 1 = C:\GAMES */
static int prompt_len; /* so backspace knows where the line starts */
void shell_prompt(void) {
    const char *p = in_games ? "C:\\GAMES>" : "C:\\>";
    prompt_len = (int)strlen(p);
    term_say(p);
}

void shell_init(void) {
    cmd_n = 0;
    in_games = 0;
    page_want = page_on = 0;
    page_len = 0;
}
void shell_reset_line(void) {
    cmd_n = 0;
}
void shell_set_dir(int g) {
    in_games = g;
}

static void cmd_dir(void) {
    term_sayln(" Volume in drive C is DXM-DOS");
    term_sayln(" Volume Serial Number is 1993-0C7E");
    term_sayln(in_games ? " Directory of C:\\GAMES" : " Directory of C:\\");
    term_put('\n');
    if (!in_games) {
        disk_entry ent[64];
        int n = disk_list(ent, 64);
        int files = 0, dirs = 0;
        long total = 0;
        for (int i = 0; i < n; i++) {
            const disk_entry *e = &ent[i];
            char nm[9] = "        ", ex[4] = "   ", sz[16], dt[10], tm[8], ln[80];
            const char *dot = strchr(e->name, '.');
            int nl = dot ? (int)(dot - e->name) : (int)strlen(e->name);
            memcpy(nm, e->name, (size_t)(nl > 8 ? 8 : nl));
            if (dot)
                memcpy(ex, dot + 1, strlen(dot + 1) > 3 ? 3 : strlen(dot + 1));
            if (e->is_dir) {
                sz[0] = 0;
                dirs++;
            } else { /* thousands separated, DOS style */
                char raw[16];
                snprintf(raw, sizeof raw, "%ld", e->size);
                int rl = (int)strlen(raw), o = 0;
                for (int k = 0; k < rl; k++) {
                    if (k && (rl - k) % 3 == 0)
                        sz[o++] = ',';
                    sz[o++] = raw[k];
                }
                sz[o] = 0;
                files++;
                total += e->size;
            }
            snprintf(dt, sizeof dt, "%s", e->date);
            snprintf(tm, sizeof tm, "%s", e->time);
            if (e->is_dir)
                snprintf(ln, sizeof ln, "%s     <DIR>        %s  %s", nm, dt, tm);
            else
                snprintf(ln, sizeof ln, "%s %s %11s  %s  %s", nm, ex, sz, dt, tm);
            term_sayln(ln);
        }
        term_put('\n');
        {
            char ln[80];
            char raw[16];
            snprintf(raw, sizeof raw, "%ld", total);
            char tot[24];
            int rl = (int)strlen(raw), o = 0;
            for (int k = 0; k < rl; k++) {
                if (k && (rl - k) % 3 == 0)
                    tot[o++] = ',';
                tot[o++] = raw[k];
            }
            tot[o] = 0;
            snprintf(ln, sizeof ln, "%9d file(s) %14s bytes", files, tot);
            term_sayln(ln);
            snprintf(ln, sizeof ln, "%9d dir(s)", dirs);
            term_sayln(ln);
        }
    } else {
        term_sayln(".            <DIR>           08-30-26  11:04a");
        term_sayln("..           <DIR>           08-30-26  11:04a");
        for (int i = 0; i < lib_count(); i++) {
            const lib_game *g = lib_at(i);
            char nm[16], ln[80];
            int k = 0;
            for (; g->id[k] && k < 8; k++)
                nm[k] = (char)toupper((unsigned char)g->id[k]);
            while (k < 8)
                nm[k++] = ' ';
            nm[8] = 0;
            snprintf(ln, sizeof ln, "%s EXE        114,688  03-15-93   1:43a", nm);
            term_sayln(ln);
        }
        term_put('\n');
        {
            char ln[80];
            snprintf(ln, sizeof ln, "        %d file(s)", lib_count());
            term_sayln(ln);
        }
    }
    term_sayln("                      536,870,912 bytes free");
}
static void cmd_help(void) {
    oline("DXM-DOS command reference");
    oline("");
    oline("DIR        List the files on this machine.");
    oline("CLS        Clear the screen.");
    oline("VER        Show the DOS version.");
    oline("TYPE file  Display a text file");
    oline("CD dir     Change directory. The games are in C:\\GAMES.");
    oline("NC         Browse the games in a dual-pane navigator.");
    oline("EXIT       Switch the machine off.");
}
static void run(char *s) {
    while (*s == ' ')
        s++;
    for (char *p = s; *p; p++)
        if (*p >= 'a' && *p <= 'z')
            *p -= 32;
    /* DOS took CD.. and CD\GAMES with no space, because CD did not need a
     * delimiter before a path.  Put one in before the splitter runs, rather
     * than teaching the splitter about one command. */
    if (s[0] == 'C' && s[1] == 'D' && (s[2] == '.' || s[2] == '\\' || s[2] == '/')) {
        memmove(s + 3, s + 2, strlen(s + 2) + 1);
        s[2] = ' ';
    }
    /* "| MORE" at the end of anything: the output is paged */
    page_want = 0;
    page_len = 0;
    page_buf[0] = 0;
    {
        char *bar = strchr(s, '|');
        if (bar) {
            char *m = bar + 1;
            while (*m == ' ')
                m++;
            if (!strncmp(m, "MORE", 4))
                page_want = 1;
            *bar = 0;
            while (bar > s && bar[-1] == ' ')
                *--bar = 0;
        }
    }
    /* MORE file, and MORE < file, are TYPE file */
    if (!strncmp(s, "MORE", 4) && (s[4] == ' ' || s[4] == 0)) {
        memmove(s, "TYPE", 4);
        char *lt = strchr(s, '<');
        if (lt)
            *lt = ' ';
    }
    /* TYPE pages by itself when a file is longer than the screen: the
     * pager only stops if there is more to show, so a short file simply
     * prints */
    if (!strncmp(s, "TYPE", 4) && (s[4] == ' ' || s[4] == 0))
        page_want = 1;
    char *sp = strchr(s, ' ');
    char *arg = NULL;
    if (sp) {
        *sp = 0;
        arg = sp + 1;
        while (*arg == ' ')
            arg++;
    }
    size_t n = strlen(s);
    if (n > 4 && !strcmp(s + n - 4, ".EXE"))
        s[n - 4] = 0;
    if (!*s)
        return;
    if (!strcmp(s, "DIR")) {
        machine.floppy_req = 0.7;
        cmd_dir();
    } else if (!strcmp(s, "CLS")) {
        term_clear();
        return;
    } else if (!strcmp(s, "HELP"))
        cmd_help();
    else if (!strcmp(s, "VER"))
        term_sayln("DXM-DOS Version " DXM_VERSION " (C) 2026");
    else if (!strcmp(s, "TYPE")) {
        static char buf[8192];
        if (!arg || !*arg)
            term_sayln("Required parameter missing");
        else {
            /* a path prefix is tolerated; only the root has files to show */
            int r = in_games ? -1 : disk_read(arg, buf, sizeof buf);
            if (r < 0) {
                term_say("File not found - ");
                term_sayln(arg);
            } else if (r > 0)
                term_sayln("This file cannot be displayed.");
            else {
                for (char *p = buf; *p;) {
                    char *e = strpbrk(p, "\r\n");
                    size_t len = e ? (size_t)(e - p) : strlen(p);
                    char line[256];
                    if (len >= sizeof line)
                        len = sizeof line - 1;
                    memcpy(line, p, len);
                    line[len] = 0;
                    oline(line);
                    p = e ? e + 1 : p + len;
                    if (e && *e == '\r' && *p == '\n')
                        p++;
                }
            }
        }
    } else if (!strcmp(s, "EXIT")) {
        machine.state = DOS_OFF;
        return;
    } else if (!strcmp(s, "NC")) {
        nc_open_panel(in_games);
        in_games = 1;
        return;
    } else if (!strcmp(s, "CD") || !strcmp(s, "CHDIR")) {
        if (!arg || !*arg) {
            term_sayln(in_games ? "C:\\GAMES" : "C:\\");
        } else if (!strcmp(arg, "\\") || !strcmp(arg, "/"))
            in_games = 0;
        else if (!strcmp(arg, "..")) {
            if (in_games)
                in_games = 0;
            else
                term_sayln("Invalid directory");
        } else if (!strcmp(arg, ".")) { /* stay put */
        } else if (!in_games && (!strcmp(arg, "GAMES") || !strcmp(arg, "\\GAMES")))
            in_games = 1;
        else if (in_games && !strcmp(arg, "\\GAMES")) { /* already there */
        } else
            term_sayln("Invalid directory");
    } else if (!strcmp(s, "FORMAT"))
        term_sayln("Nice try.");
    else {
        /* Anything else may be an installed game - but only from the
         * directory the games are actually in.  DOS did not search the disk
         * for you, and neither does this. */
        const lib_game *g = in_games ? lib_find(s) : NULL;
        if (!g && !in_games && lib_find(s))
            term_sayln("Bad command or file name - try CD GAMES");
        else if (!g)
            term_sayln("Bad command or file name");
        else if (!g->ready) {
            term_say("Cannot run ");
            term_say(g->title);
            term_sayln(":");
            term_sayln(g->note[0] ? g->note : "not ready");
        } else {
            dos_launch(g->id);
            return;
        }
    }
}

/* A key at the prompt: MORE takes it if it is waiting, Enter runs the
 * line, Backspace erases, anything printable is typed. */
void shell_key(int ch, int sc) {
    if (page_on) {
        if (ch || sc == DXM_SC_ESC)
            page_key(sc == DXM_SC_ESC ? 27 : ch);
        return;
    }
    if (ch == '\r' || ch == '\n') {
        term_put('\n');
        cmd[cmd_n] = 0;
        char tmp[128];
        memcpy(tmp, cmd, sizeof tmp);
        cmd_n = 0;
        run(tmp);
        if (page_want)
            page_begin();
        /* NC owns the whole screen once it opens, so the prompt must not be
         * printed over it - the cursor is wherever the command line left it,
         * and nc_draw does not move it.  Nor while MORE is waiting. */
        if (machine.state == DOS_PROMPT && !nc_is_open() && !page_on)
            shell_prompt();
        return;
    }
    if (ch == '\b') {
        if (cmd_n) {
            cmd_n--;
            if (term_col() > prompt_len)
                term_set_cursor(term_row(), term_col() - 1);
            term_poke(term_row(), term_col(), ' ');
        }
        return;
    }
    if (ch >= 32 && ch < 127 && cmd_n < (int)sizeof cmd - 1) {
        cmd[cmd_n++] = (char)ch;
        term_put((char)ch);
    }
    (void)sc;
}
