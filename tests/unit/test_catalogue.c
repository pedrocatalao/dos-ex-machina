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
    snprintf(out + k, n - k, "}");
}

static int parse_one(cat_catalogue *c, const char *replace_key, const char *with) {
    char t[2048], json[2400];
    title(t, sizeof t, replace_key, with);
    snprintf(json, sizeof json, "{\"format\": 1, \"name\": \"T\", \"titles\": [%s]}", t);
    return cat_parse(c, json);
}

int main(void) {
    cat_catalogue c;
    cat_list l;

    /* the repo's own files read, and the list names a file that exists */
    CHECK(cat_read_list(&l, TEST_REPO "/catalogues/catalogues.lst") >= 1);
    CHECK(l.n_notes == 0);
    CHECK_STR(l.entries[0].id, "FREEWARE");
    CHECK(l.entries[0].drive == 'C');
    CHECK(l.entries[0].community == 0);
    {
        char path[512];
        snprintf(path, sizeof path, TEST_REPO "/catalogues/%s", l.entries[0].file);
        CHECK(cat_read(&c, path) >= 0);
        CHECK(c.n_notes == 0);
        CHECK_STR(c.name, "Freeware");
        for (int i = 0; i < c.n; i++)
            CHECK(cat_category_ok(c.titles[i].category));
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
    };
    for (size_t i = 0; i < sizeof faults / sizeof faults[0]; i++) {
        int n = parse_one(&c, faults[i][0], faults[i][1]);
        if (n != 0 || c.n_notes != 1)
            fprintf(stderr, "fault %zu (%s = %s): kept %d, %d notes\n", i, faults[i][0],
                    faults[i][1] ? faults[i][1] : "(absent)", n, c.n_notes);
        CHECK(n == 0);
        CHECK(c.n_notes == 1);
    }

    /* a bad title does not take the good one after it */
    {
        char bad[2048], good[2048], json[4400];
        title(bad, sizeof bad, "year", NULL);
        title(good, sizeof good, "id", "\"KEEN5\"");
        snprintf(json, sizeof json, "{\"format\": 1, \"titles\": [%s, %s]}", bad, good);
        CHECK(cat_parse(&c, json) == 1);
        CHECK_STR(c.titles[0].id, "KEEN5");
        CHECK(c.n_notes == 1);
    }
    /* the same id twice in a category is dropped the second time */
    {
        char t[2048], json[4400];
        title(t, sizeof t, NULL, NULL);
        snprintf(json, sizeof json, "{\"format\": 1, \"titles\": [%s, %s]}", t, t);
        CHECK(cat_parse(&c, json) == 1);
        CHECK(c.n_notes == 1);
    }

    /* not a catalogue at all */
    CHECK(cat_parse(&c, "") == -1);
    CHECK(cat_parse(&c, "{\"format\": 1}") == -1);
    CHECK(cat_parse(&c, "{\"format\": 2, \"titles\": []}") == -1);
    CHECK(cat_parse(&c, "{\"format\": 1, \"titles\": [1, 2]}") == -1);
    CHECK(cat_parse(&c, "{\"format\": 1, \"titles\": [{\"id\": \"A\"") == -1);
    CHECK(cat_parse(&c, "{\"format\": 1, \"titles\": []}") == 0);
    CHECK(cat_read(&c, TEST_REPO "/no/such/file.cat") == -1);

    /* the list */
    CHECK(cat_parse_list(&l, "{\"format\": 1, \"catalogues\": ["
                             "{\"id\": \"A\", \"name\": \"A\", \"file\": \"a.cat\", \"origin\": "
                             "\"bundled\", \"drive\": \"D\"},"
                             "{\"id\": \"B\", \"name\": \"B\", \"file\": \"../b.cat\", \"origin\": "
                             "\"bundled\", \"drive\": \"E\"},"
                             "{\"id\": \"C\", \"name\": \"C\", \"file\": \"c.cat\", \"origin\": "
                             "\"mine\", \"drive\": \"E\"},"
                             "{\"id\": \"D\", \"name\": \"D\", \"file\": \"d.cat\", \"origin\": "
                             "\"community\", \"drive\": \"A\"},"
                             "{\"id\": \"A\", \"name\": \"A2\", \"file\": \"a2.cat\", \"origin\": "
                             "\"community\", \"drive\": \"F\"}"
                             "]}") == 1);
    CHECK(l.n_notes == 4);
    CHECK_STR(l.entries[0].file, "a.cat");
    CHECK(cat_parse_list(&l, "{\"catalogues\": []}") == -1);

    /* the names */
    CHECK(cat_id_ok("KEEN4") && cat_id_ok("A") && cat_id_ok("SKYROADS") && cat_id_ok("X-COM"));
    CHECK(!cat_id_ok("") && !cat_id_ok("keen4") && !cat_id_ok("TOOLONGID") && !cat_id_ok("A B") &&
          !cat_id_ok("A.B") && !cat_id_ok("A/B"));

    return check_done("catalogue");
}
