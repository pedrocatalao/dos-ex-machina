/* dosname: what DOS makes of a name, and whether it can reach it as it
 * stands.  The cases are the ones that actually turn up in archives -
 * a release folder with spaces and brackets, a name past eight, a second
 * dot - checked against what DOS_MakeName in the core does to a request. */
#include "catalogue/dosname.h"
#include "check.h"
#include <string.h>

static void made(const char *in, const char *want) {
    char got[DOS_NAME];
    dos_name(in, got, sizeof got);
    CHECK_STR(got, want);
}

int main(void) {
    /* spaces vanish, it goes upper case, the stem is cut to eight */
    made("GAME", "GAME");
    made("game", "GAME");
    made("My Game", "MYGAME");
    made("VERYLONGDIRNAME", "VERYLONG");
    made("data files", "DATAFILE");
    made("PREZ.EXE", "PREZ.EXE");
    made("prez.exe", "PREZ.EXE");

    /* the one off the screen: DOS reads it as PREHISTO.ORG and finds
     * nothing, which is what made CD fail */
    made("PREHISTORIK 2 [REPLAYERS.ORG]", "PREHISTO.ORG");

    /* an extension is kept, and cut to three */
    made("SOMETHING.LONGEXT", "SOMETHIN.LON");
    /* DOS refuses a second dot outright; this gives the nearest it can
     * reach rather than reproducing the refusal */
    made("a.b.c", "A.BC");
    /* nothing of it survives, and it still has to be called something */
    made("...", "X");
    made("", "X");
    made("!!!", "!!!"); /* punctuation DOS allows is punctuation it keeps */

    /* reachable: the name is already what DOS would ask for.  Case does not
     * count, because the core upper-cases both sides before comparing. */
    CHECK(dos_reachable("GAME"));
    CHECK(dos_reachable("game"));
    CHECK(dos_reachable("PREZ.EXE"));
    CHECK(dos_reachable("prez.exe"));
    CHECK(dos_reachable("README.TXT"));
    CHECK(dos_reachable("readme.txt"));
    CHECK(!dos_reachable("PREHISTORIK 2 [REPLAYERS.ORG]"));
    CHECK(!dos_reachable("My Game"));       /* a space DOS would drop */
    CHECK(!dos_reachable("replayers.nfo")); /* a stem past eight */
    CHECK(!dos_reachable("a.b.c"));         /* a second dot */

    /* whatever comes out is itself reachable: that is the point of it */
    const char *awkward[] = {"PREHISTORIK 2 [REPLAYERS.ORG]",
                             "My Game",
                             "a.b.c",
                             "...",
                             "replayers.nfo",
                             "wing commander ii"};
    for (size_t i = 0; i < sizeof awkward / sizeof awkward[0]; i++) {
        char out[DOS_NAME];
        dos_name(awkward[i], out, sizeof out);
        CHECK(dos_reachable(out));
        CHECK(strlen(out) <= DOS_NAME - 1);
    }
    return check_done("dosname");
}
