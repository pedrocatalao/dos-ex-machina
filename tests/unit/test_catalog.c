/* catalogue: the parser, on the real catalogue.json and on what a server
 * might send instead - malformed, truncated, empty, oversized. */
#include "check.h"
#include "catalog.h"
#include <stdio.h>
#include <stdlib.h>

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (buf && fread(buf, 1, (size_t)n, f) == (size_t)n)
        buf[n] = 0;
    else {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    return buf;
}

int main(void) {
    char *json = slurp(TEST_REPO "/catalogue.json");
    CHECK(json != NULL);
    if (json) {
        int n = cat_parse(json);
        CHECK(n >= 1);
        CHECK(cat_count() == n);
        const cat_game *g = cat_find("skyroads");
        CHECK(g != NULL);
        if (g) {
            CHECK_STR(g->title, "SkyRoads");
            CHECK_STR(g->by, "Bluemoon Interactive");
            CHECK(g->year == 1993);
            CHECK_STR(g->version, "1.5");
            CHECK(g->abi == 2);
            CHECK(g->desc[0][0] != 0);
            CHECK(strncmp(g->art.url, "https://", 8) == 0);
            CHECK(strlen(g->art.sha256) == 64);
            CHECK(g->art.size > 0);
            CHECK_STR(g->data_kind, "freeware");
            CHECK_STR(g->data_format, "zip");
            CHECK_STR(g->data_probe, "roads.lzs");
            CHECK(g->data.size > 0);
            /* the module for this platform: every shipped platform has one */
            CHECK(g->have_module);
            CHECK(strstr(g->module.url, cat_platform()) != NULL);
        }
        /* ids are compared exactly: the catalogue is only ever asked for an
         * id it handed out itself.  The library, which takes what was
         * typed at the prompt, is the case-insensitive one. */
        CHECK(cat_find("SKYROADS") == NULL);
        CHECK(cat_find("nothing") == NULL);
        CHECK(cat_at(0) == g);
        CHECK(cat_at(n) == NULL);
        CHECK(cat_at(-1) == NULL);

        /* a truncated download: an error, not a crash, and the list is
         * whatever could be read - never garbage */
        for (size_t cut = strlen(json) / 3; cut < strlen(json); cut += 97) {
            char *part = malloc(cut + 1);
            memcpy(part, json, cut);
            part[cut] = 0;
            int r = cat_parse(part);
            CHECK(r <= n);
            free(part);
        }
        free(json);
    }

    /* not a catalogue at all */
    CHECK(cat_parse("") < 0);
    CHECK(cat_parse("{}") < 0);
    CHECK(cat_parse("{\"games\": 7}") < 0);
    CHECK(cat_parse("{\"games\": [") <= 0);
    CHECK(cat_parse("[1,2,3]") < 0);
    CHECK(cat_parse("{\"games\": []}") == 0);
    CHECK(cat_count() == 0);
    /* a minimal game, with everything else missing */
    CHECK(cat_parse("{\"games\":[{\"id\":\"x\"}]}") == 1);
    CHECK(cat_find("x") != NULL && !cat_find("x")->have_module);

    /* more games than the machine holds: the first CAT_MAX are kept */
    {
        char big[8192];
        int off = snprintf(big, sizeof big, "{\"games\":[");
        for (int i = 0; i < CAT_MAX + 5; i++)
            off += snprintf(big + off, sizeof big - (size_t)off,
                            "%s{\"id\":\"g%d\",\"title\":\"G%d\"}", i ? "," : "", i, i);
        snprintf(big + off, sizeof big - (size_t)off, "]}");
        CHECK(cat_parse(big) == CAT_MAX);
        CHECK(cat_find("g0") != NULL);
        CHECK(cat_find("g31") != NULL);
        CHECK(cat_find("g32") == NULL);
    }
    return check_done("catalog");
}
