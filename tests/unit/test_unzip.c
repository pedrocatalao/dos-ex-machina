/* unzip: a small archive with a stored, a deflated and an empty entry; an
 * archive that tries to escape the destination; and bytes that are not an
 * archive at all. */
#include "check.h"
#include "unzip.h"
#include "sha256.h"
#include <stdlib.h>

static int file_size(const char *path, long *size) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fclose(f);
    return 0;
}

int main(void) {
    char dest[1024], path[1200], err[128];
    snprintf(dest, sizeof dest, "%s/", TEST_TMP); /* the extractor wants the separator */
    long size;
    int n = -1;

    /* small.zip: hello.txt (stored), dir/nested.bin (deflated, 5000 bytes
     * of a pattern), empty.txt.  Entries are flattened: DOS game data is a
     * flat directory, and a tree from an untrusted archive is more surface
     * than that needs. */
    err[0] = 0;
    CHECK(unzip_extract(TEST_FIXTURES "/small.zip", dest, &n, err, sizeof err) == 0);
    CHECK(n == 3);
    snprintf(path, sizeof path, "%shello.txt", dest);
    CHECK(file_size(path, &size) == 0 && size == 6);
    {
        FILE *f = fopen(path, "rb");
        char buf[16] = {0};
        if (f) {
            size_t got = fread(buf, 1, sizeof buf - 1, f);
            buf[got] = 0;
            fclose(f);
        }
        CHECK_STR(buf, "hello\n");
    }
    snprintf(path, sizeof path, "%snested.bin", dest);
    CHECK(file_size(path, &size) == 0 && size == 5000);
    {
        char sum[65];
        CHECK(sha256_file(path, sum) == 0);
        CHECK_STR(sum, "34398b85297bf7d9dfb59b8d511d8bbb44ab23e891570e4395e7871475fc8afb");
    }
    snprintf(path, sizeof path, "%sempty.txt", dest);
    CHECK(file_size(path, &size) == 0 && size == 0);
    /* escape.zip holds one entry named ../escape.txt.  The whole archive
     * is refused with a reason - an archive that tries this is not one to
     * extract any of - and nothing is written, nowhere. */
    n = -1;
    err[0] = 0;
    CHECK(unzip_extract(TEST_FIXTURES "/escape.zip", dest, &n, err, sizeof err) != 0);
    CHECK(strstr(err, "unsafe") != NULL);
    CHECK(n == 0);
    snprintf(path, sizeof path, "%s../escape.txt", dest);
    CHECK(file_size(path, &size) != 0);
    snprintf(path, sizeof path, "%sescape.txt", dest);
    CHECK(file_size(path, &size) != 0);

    /* not an archive */
    err[0] = 0;
    CHECK(unzip_extract(TEST_FIXTURES "/notazip.bin", dest, &n, err, sizeof err) != 0);
    CHECK(err[0] != 0);
    /* a path that does not exist */
    err[0] = 0;
    CHECK(unzip_extract(TEST_FIXTURES "/missing.zip", dest, &n, err, sizeof err) != 0);
    CHECK(err[0] != 0);

    snprintf(path, sizeof path, "%shello.txt", dest);
    remove(path);
    snprintf(path, sizeof path, "%snested.bin", dest);
    remove(path);
    snprintf(path, sizeof path, "%sempty.txt", dest);
    remove(path);
    return check_done("unzip");
}
