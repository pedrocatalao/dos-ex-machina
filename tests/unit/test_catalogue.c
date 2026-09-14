/* catalogue: the reader, on the files in the repo and on what a curator
 * might write instead - a title with a field wrong, which must be dropped
 * on its own and noted, and a file that is not a catalogue at all. */
#include "check.h"
#include "catalogue.h"
#include <stdio.h>
#include <string.h>

#define SHA "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"

/* A title with everything right, with one field replaced. */
static void title(char *out, size_t n, const char *replace_key, const char *with) {
    struct {
        const char *key, *value;
    } f[] = {
        {"id", "\"KEEN4\""},
        {"name", "\"Commander Keen 4\""},
        {"creator", "\"id Software\""},
        {"year", "1991"},
        {"category", "\"GAMES\""},
        {"multiplayer", "false"},
        {"network", "false"},
        {"run", "\"KEEN4E.EXE\""},
        {"download",
         "{ \"url\": \"https://x/keen4.zip\", \"size\": 1000, \"sha256\": \"" SHA "\" }"},
        {"video", "\"EGA\""},
        {"sound", "[\"pcspeaker\", \"adlib\"]"},
        {"setup", "\"SETUP.EXE\""},
        {"artwork", "{ \"url\": \"https://x/keen4.png\" }"},
    };
    size_t k = (size_t)snprintf(out, n, "{");
    for (size_t i = 0; i < sizeof f / sizeof f[0]; i++) {
        const char *v = f[i].value;
        if (replace_key && !strcmp(f[i].key, replace_key)) {
            if (!with)
                continue; /* leave it out */
            v = with;
        }
        k += (size_t)snprintf(out + k, n - k, "%s\"%s\": %s", i ? ", " : "", f[i].key, v);
    }
    /* a key the good title has not got is added, so optional ones can be tried */
    int known = 0;
    for (size_t i = 0; i < sizeof f / sizeof f[0]; i++)
        known |= replace_key && !strcmp(f[i].key, replace_key);
    if (replace_key && with && !known)
        k += (size_t)snprintf(out + k, n - k, ", \"%s\": %s", replace_key, with);
    snprintf(out + k, n - k, "}");
}

/* where a catalogue goes, without which it is not one */
#define HEAD "\"id\": \"TEST\", \"drive\": \"D\", \"origin\": \"bundled\","

static int parse_one(cat_catalogue *c, const char *replace_key, const char *with) {
    char t[2048], json[2400];
    title(t, sizeof t, replace_key, with);
    snprintf(json, sizeof json, "{\"format\": 1, %s \"name\": \"T\", \"titles\": [%s]}", HEAD, t);
    return cat_parse(c, json);
}

int main(void) {
    cat_catalogue c;

    /* the repo's own catalogues read, and say where they go */
    {
        const struct {
            const char *file, *id;
            char drive;
        } own[] = {{"freeware.cat", "FREEWARE", 'C'}, {"shareware.cat", "SHAREWAR", 'D'}};
        for (size_t i = 0; i < sizeof own / sizeof own[0]; i++) {
            char path[512];
            snprintf(path, sizeof path, TEST_REPO "/catalogues/%s", own[i].file);
            CHECK(cat_read(&c, path) >= 0);
            CHECK(c.n_notes == 0);
            CHECK_STR(c.id, own[i].id);
            CHECK(c.drive == own[i].drive);
            CHECK(c.community == 0);
            for (int k = 0; k < c.n; k++)
                CHECK(cat_category_ok(c.titles[k].category));
        }
    }

    /* a good title, read in full */
    CHECK(parse_one(&c, NULL, NULL) == 1);
    CHECK(c.n_notes == 0);
    CHECK_STR(c.titles[0].id, "KEEN4");
    CHECK_STR(c.titles[0].category, "GAMES");
    CHECK(c.titles[0].year == 1991);
    CHECK(c.titles[0].multiplayer == 0 && c.titles[0].network == 0);
    CHECK_STR(c.titles[0].run, "KEEN4E.EXE");
    CHECK_STR(c.titles[0].setup, "SETUP.EXE");
    CHECK(c.titles[0].download.size == 1000);
    CHECK_STR(c.titles[0].download.sha256, SHA);
    CHECK_STR(c.titles[0].video, "EGA");
    CHECK_STR(c.titles[0].sound[1], "adlib");
    CHECK_STR(c.titles[0].artwork.url, "https://x/keen4.png");
    CHECK(c.titles[0].publisher[0] == 0);

    /* each fault drops the title, with a note, and nothing else */
    const char *faults[][2] = {
        {"id", NULL},
        {"id", "\"keen4\""},
        {"id", "\"TOOLONGID\""},
        {"name", NULL},
        {"creator", NULL},
        {"year", NULL},
        {"year", "\"1991\""},
        {"category", "\"game\""},
        {"category", "\"GAMES2\""},
        {"multiplayer", NULL},
        {"network", "\"no\""},
        {"run", NULL},
        {"download", NULL},
        {"download", "{ \"url\": \"ftp://x\", \"size\": 1, \"sha256\": \"" SHA "\" }"},
        {"download", "{ \"url\": \"https://x\", \"size\": 1, \"sha256\": \"abc\" }"},
        {"download", "{ \"url\": \"https://x\", \"sha256\": \"" SHA "\" }"},
        {"video", "\"HERCULES\""},
        {"artwork", "{ \"url\": \"x.png\" }"},
        {"archive", "\"../OMF21.EXE\""},
        {"archive", "\"/OMF21.EXE\""},
        {"archive", "\"C:OMF21.EXE\""},
        {"archive", "\"\""},
    };
    for (size_t i = 0; i < sizeof faults / sizeof faults[0]; i++) {
        int n = parse_one(&c, faults[i][0], faults[i][1]);
        if (n != 0 || c.n_notes != 1)
            fprintf(stderr, "fault %zu (%s = %s): kept %d, %d notes\n", i, faults[i][0],
                    faults[i][1] ? faults[i][1] : "(absent)", n, c.n_notes);
        CHECK(n == 0);
        CHECK(c.n_notes == 1);
    }

    /* an installer's archive, named inside the download */
    CHECK(parse_one(&c, "archive", "\"OMF/OMF21.EXE\"") == 1);
    CHECK_STR(c.titles[0].archive, "OMF/OMF21.EXE");

    /* a bad title does not take the good one after it */
    {
        char bad[2048], good[2048], json[4400];
        title(bad, sizeof bad, "year", NULL);
        title(good, sizeof good, "id", "\"KEEN5\"");
        snprintf(json, sizeof json, "{\"format\": 1, " HEAD " \"titles\": [%s, %s]}", bad, good);
        CHECK(cat_parse(&c, json) == 1);
        CHECK_STR(c.titles[0].id, "KEEN5");
        CHECK(c.n_notes == 1);
    }
    /* the same id twice in a category is dropped the second time */
    {
        char t[2048], json[4400];
        title(t, sizeof t, NULL, NULL);
        snprintf(json, sizeof json, "{\"format\": 1, " HEAD " \"titles\": [%s, %s]}", t, t);
        CHECK(cat_parse(&c, json) == 1);
        CHECK(c.n_notes == 1);
    }

    /* not a catalogue at all */
    CHECK(cat_parse(&c, "") == -1);
    CHECK(cat_parse(&c, "{\"format\": 1}") == -1);
    CHECK(cat_parse(&c, "{\"format\": 2, \"titles\": []}") == -1);
    CHECK(cat_parse(&c, "{\"format\": 1, \"titles\": [1, 2]}") == -1);
    CHECK(cat_parse(&c, "{\"format\": 1, \"titles\": [{\"id\": \"A\"") == -1);
    CHECK(cat_parse(&c, "{\"format\": 1, " HEAD " \"titles\": []}") == 0);
    CHECK(cat_read(&c, TEST_REPO "/no/such/file.cat") == -1);

    /* where it goes: each of these is not a catalogue the machine can put anywhere */
    {
        const char *heads[] = {
            "\"drive\": \"D\", \"origin\": \"bundled\",",                    /* no id */
            "\"id\": \"lower\", \"drive\": \"D\", \"origin\": \"bundled\",", /* not a DOS name */
            "\"id\": \"T\", \"origin\": \"bundled\",",                       /* no drive */
            "\"id\": \"T\", \"drive\": \"A\", \"origin\": \"bundled\",",     /* a floppy */
            "\"id\": \"T\", \"drive\": \"DE\", \"origin\": \"bundled\",",    /* two letters */
            "\"id\": \"T\", \"drive\": \"D\",",                              /* no origin */
            "\"id\": \"T\", \"drive\": \"D\", \"origin\": \"mine\",",        /* not an origin */
        };
        for (size_t i = 0; i < sizeof heads / sizeof heads[0]; i++) {
            char json[400];
            snprintf(json, sizeof json, "{\"format\": 1, %s \"titles\": []}", heads[i]);
            CHECK(cat_parse(&c, json) == -1);
            CHECK(c.n_notes == 1);
        }
        CHECK(cat_parse(&c, "{\"format\": 1, \"id\": \"T\", \"drive\": \"E\", "
                            "\"origin\": \"community\", \"titles\": []}") == 0);
        CHECK(c.drive == 'E' && c.community == 1);
    }

    /* the names */
    CHECK(cat_id_ok("KEEN4") && cat_id_ok("A") && cat_id_ok("SKYROADS") && cat_id_ok("X-COM"));
    CHECK(!cat_id_ok("") && !cat_id_ok("keen4") && !cat_id_ok("TOOLONGID") && !cat_id_ok("A B") &&
          !cat_id_ok("A.B") && !cat_id_ok("A/B"));

    return check_done("catalogue");
}
