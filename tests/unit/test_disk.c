/* disk: the virtual C:\ - what DIR lists, what TYPE can read, how names
 * are matched, and what AUTOEXEC echoes at boot. */
#include "check.h"
#include "disk.h"

static const disk_entry *find(const disk_entry *e, int n, const char *name) {
    for (int i = 0; i < n; i++)
        if (!strcmp(e[i].name, name))
            return &e[i];
    return NULL;
}

int main(void) {
    disk_entry e[16];
    int n = disk_list(e, 16);
    CHECK(n == 6);
    const disk_entry *d;
    CHECK((d = find(e, n, "GAMES")) != NULL && d->is_dir);
    CHECK((d = find(e, n, "COMMAND.COM")) != NULL && !d->is_dir && d->size == 54645);
    CHECK((d = find(e, n, "AUTOEXEC.BAT")) != NULL && d->size > 0);
    CHECK((d = find(e, n, "README.1ST")) != NULL && d->size > 500);
    CHECK((d = find(e, n, "CONFIG.SYS")) != NULL);
    CHECK((d = find(e, n, "NC.EXE")) != NULL);
    /* every entry carries a date and a time, as DIR prints them */
    for (int i = 0; i < n; i++)
        CHECK(e[i].date && strlen(e[i].date) == 8 && e[i].time && strlen(e[i].time) == 6);
    /* a smaller buffer is honoured */
    CHECK(disk_list(e, 2) == 2);

    char buf[8192];
    /* TYPE: text files read, programs refuse, unknown names are not there */
    CHECK(disk_read("AUTOEXEC.BAT", buf, sizeof buf) == 0);
    CHECK(strstr(buf, "@ECHO OFF") == buf);
    CHECK(strstr(buf, "SET BLASTER=A220") != NULL);
    /* names match the way DOS matched them: any case, a root prefix allowed */
    CHECK(disk_read("autoexec.bat", buf, sizeof buf) == 0);
    CHECK(disk_read("C:\\AUTOEXEC.BAT", buf, sizeof buf) == 0);
    CHECK(disk_read("\\config.sys", buf, sizeof buf) == 0);
    CHECK(strstr(buf, "HIMEM.SYS") != NULL);
    CHECK(disk_read("README.1ST", buf, sizeof buf) == 0);
    CHECK(strstr(buf, "DOS EX MACHINA") != NULL);
    CHECK(disk_read("COMMAND.COM", buf, sizeof buf) == 1);
    CHECK(disk_read("NC.EXE", buf, sizeof buf) == 1);
    CHECK(disk_read("NOPE.TXT", buf, sizeof buf) == -1);
    CHECK(disk_read("GAMES", buf, sizeof buf) == -1);
    /* a short buffer is cut, not overrun */
    CHECK(disk_read("README.1ST", buf, 10) == 0);
    CHECK(strlen(buf) == 9);

    /* the ECHO lines of AUTOEXEC.BAT, in order, without the command */
    char lines[8][80];
    int k = disk_autoexec_echo(lines, 8);
    CHECK(k >= 2);
    CHECK(k >= 1 && strstr(lines[0], "type HELP") != NULL);
    CHECK(k >= 2 && strstr(lines[1], "If you know") != NULL);
    for (int i = 0; i < k; i++)
        CHECK(strncmp(lines[i], "ECHO", 4) != 0);
    /* a limit of one line is respected */
    CHECK(disk_autoexec_echo(lines, 1) == 1);
    return check_done("disk");
}
