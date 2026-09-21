/* where: the note of where each title ended up on this machine.  It is the
 * only answer to "is this installed", so it has to survive a restart, keep
 * its neighbours when one entry changes, and let go of everything belonging
 * to a catalogue when the catalogue goes. */
#include "catalogue/where.h"
#include "check.h"
#include <stdio.h>
#include <string.h>

#define FILE_AT TEST_TMP "/installed.cfg"

static const char *SKY = "C:\\DXM\\FREEWARE\\GAMES\\SKYROADS";

int main(void) {
    remove(FILE_AT);
    where_load(FILE_AT);
    CHECK(where_get("FREEWARE", "GAMES", "SKYROADS") == NULL); /* missing is empty, not a fault */

    where_set("FREEWARE", "GAMES", "SKYROADS", SKY, 1);
    where_set("MYGAMES", "GAMES", "PINBALL", "C:\\PINBALL", 0);
    CHECK_STR(where_get("FREEWARE", "GAMES", "SKYROADS"), SKY);
    CHECK(where_made("FREEWARE", "GAMES", "SKYROADS") == 1); /* the machine made it */
    CHECK(where_made("MYGAMES", "GAMES", "PINBALL") == 0);   /* it was pointed at this one */

    /* it is written as it is changed, so a restart finds it all */
    where_load(FILE_AT);
    CHECK_STR(where_get("MYGAMES", "GAMES", "PINBALL"), "C:\\PINBALL");
    CHECK(where_made("FREEWARE", "GAMES", "SKYROADS") == 1);

    /* the same title twice is one entry, and the others are undisturbed */
    where_set("MYGAMES", "GAMES", "PINBALL", "D:\\ARCADE\\PIN", 0);
    where_load(FILE_AT);
    CHECK_STR(where_get("MYGAMES", "GAMES", "PINBALL"), "D:\\ARCADE\\PIN");
    CHECK(where_get("FREEWARE", "GAMES", "SKYROADS") != NULL);

    /* the same id in two catalogues, and in two categories, are different
     * titles and do not stand on each other */
    where_set("OTHER", "GAMES", "PINBALL", "E:\\P", 0);
    where_set("MYGAMES", "TOOLS", "PINBALL", "F:\\P", 0);
    CHECK_STR(where_get("MYGAMES", "GAMES", "PINBALL"), "D:\\ARCADE\\PIN");
    CHECK_STR(where_get("OTHER", "GAMES", "PINBALL"), "E:\\P");
    CHECK_STR(where_get("MYGAMES", "TOOLS", "PINBALL"), "F:\\P");

    /* a title taken out of a catalogue */
    where_forget("MYGAMES", "GAMES", "PINBALL");
    where_load(FILE_AT);
    CHECK(where_get("MYGAMES", "GAMES", "PINBALL") == NULL);
    CHECK(where_get("MYGAMES", "TOOLS", "PINBALL") != NULL);

    /* and the catalogue itself: everything of its own goes, nothing else */
    where_forget_catalogue("MYGAMES");
    where_load(FILE_AT);
    CHECK(where_get("MYGAMES", "TOOLS", "PINBALL") == NULL);
    CHECK(where_get("OTHER", "GAMES", "PINBALL") != NULL);
    CHECK(where_get("FREEWARE", "GAMES", "SKYROADS") != NULL);

    /* forgetting what was never there is not a fault */
    where_forget("NOSUCH", "GAMES", "NOPE");
    where_forget_catalogue("NOSUCH");
    CHECK(where_get("FREEWARE", "GAMES", "SKYROADS") != NULL);

    /* a file of a shape this does not write is ignored a line at a time */
    {
        FILE *f = fopen(FILE_AT, "w");
        CHECK(f != NULL);
        fprintf(f, "# a comment\n"
                   "rubbish with no equals\n"
                   "A/B/C = made C:\\A\n"
                   "D/E/F = sideways C:\\D\n" /* neither made nor found */
                   "G/H/I = found C:\\G\n");
        fclose(f);
        where_load(FILE_AT);
        CHECK_STR(where_get("A", "B", "C"), "C:\\A");
        CHECK(where_get("D", "E", "F") == NULL);
        CHECK_STR(where_get("G", "H", "I"), "C:\\G");
        CHECK(where_made("A", "B", "C") == 1);
        CHECK(where_made("G", "H", "I") == 0);
    }
    return check_done("where");
}
