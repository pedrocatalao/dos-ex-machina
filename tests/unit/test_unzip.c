/* unzip: an archive built here by hand, stored entries only, extracted with
 * its tree; and the names that must be refused, since the archive is the
 * one untrusted thing the installer opens. */
#include "check.h"
#include "unzip.h"
#include "sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#    include <direct.h>
#    define MKDIR(p) _mkdir(p)
#else
#    define MKDIR(p) mkdir(p, 0755)
#endif

/* ---- a zip writer just big enough for the test -------------------------- */

static uint8_t zipbuf[65536];
static size_t zipn;
static uint8_t cdbuf[8192];
static size_t cdn;
static int entries;

static void w16(uint8_t *p, unsigned v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
static void w32(uint8_t *p, unsigned long v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t crc32_of(const uint8_t *d, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= d[i];
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return ~c;
}

static void zip_begin(void) {
    zipn = cdn = 0;
    entries = 0;
}

/* one stored entry: the local header and the data now, the central
 * directory entry saved for the end */
static void zip_add(const char *name, const char *data) {
    size_t nlen = strlen(name), dlen = strlen(data);
    uint32_t crc = crc32_of((const uint8_t *)data, dlen);
    uint8_t *l = zipbuf + zipn;
    size_t off = zipn;
    w32(l, 0x04034b50u);
    w16(l + 4, 20);
    w16(l + 6, 0);
    w16(l + 8, 0); /* stored */
    w16(l + 10, 0);
    w16(l + 12, 0);
    w32(l + 14, crc);
    w32(l + 18, dlen);
    w32(l + 22, dlen);
    w16(l + 26, (unsigned)nlen);
    w16(l + 28, 0);
    memcpy(l + 30, name, nlen);
    memcpy(l + 30 + nlen, data, dlen);
    zipn += 30 + nlen + dlen;

    uint8_t *c = cdbuf + cdn;
    w32(c, 0x02014b50u);
    w16(c + 4, 20);
    w16(c + 6, 20);
    w16(c + 8, 0);
    w16(c + 10, 0); /* stored */
    w16(c + 12, 0);
    w16(c + 14, 0);
    w32(c + 16, crc);
    w32(c + 20, dlen);
    w32(c + 24, dlen);
    w16(c + 28, (unsigned)nlen);
    w16(c + 30, 0);
    w16(c + 32, 0);
    w16(c + 34, 0);
    w16(c + 36, 0);
    w32(c + 38, 0);
    w32(c + 42, (unsigned long)off);
    memcpy(c + 46, name, nlen);
    cdn += 46 + nlen;
    entries++;
}

static void zip_end(const char *path) {
    size_t cdoff = zipn;
    memcpy(zipbuf + zipn, cdbuf, cdn);
    zipn += cdn;
    uint8_t *e = zipbuf + zipn;
    w32(e, 0x06054b50u);
    w16(e + 4, 0);
    w16(e + 6, 0);
    w16(e + 8, (unsigned)entries);
    w16(e + 10, (unsigned)entries);
    w32(e + 12, (unsigned long)cdn);
    w32(e + 16, (unsigned long)cdoff);
    w16(e + 20, 0);
    zipn += 22;
    FILE *f = fopen(path, "wb");
    if (f) {
        fwrite(zipbuf, 1, zipn, f);
        fclose(f);
    }
}

/* The same archive with a program in front of it, as a self-extracting zip
 * is: the offsets inside still count from the start of the zip. */
static void sfx_from(const char *zip, const char *path, size_t stub) {
    FILE *in = fopen(zip, "rb"), *out = fopen(path, "wb");
    if (in && out) {
        for (size_t i = 0; i < stub; i++)
            fputc("MZ"[i % 2], out);
        int c;
        while ((c = fgetc(in)) != EOF)
            fputc(c, out);
    }
    if (in)
        fclose(in);
    if (out)
        fclose(out);
}

static int file_says(const char *path, const char *want) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    char buf[256] = "";
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    buf[n] = 0;
    fclose(f);
    return strcmp(buf, want) == 0;
}

int main(void) {
    char zip[600], dir[600], path[700], err[160];
    snprintf(zip, sizeof zip, "%s/t.zip", TEST_TMP);
    snprintf(dir, sizeof dir, "%s/unzip/", TEST_TMP);
    snprintf(path, sizeof path, "%s/unzip", TEST_TMP);
    MKDIR(path);

    /* a tree, the way a packer leaves one */
    zip_begin();
    zip_add("game/", "");
    zip_add("game/README.TXT", "read me");
    zip_add("game/GAME.EXE", "MZ...");
    zip_add("game/data/", "");
    zip_add("game/data/LEVEL1.DAT", "level one");
    zip_end(zip);
    int n = -1;
    CHECK(unzip_extract(zip, dir, &n, err, sizeof err) == 0);
    CHECK(n == 3);
    snprintf(path, sizeof path, "%sgame/GAME.EXE", dir);
    CHECK(file_says(path, "MZ..."));
    snprintf(path, sizeof path, "%sgame/data/LEVEL1.DAT", dir);
    CHECK(file_says(path, "level one"));

    /* the names that must not be extracted, whatever else is in there */
    const char *bad[] = {"../escape.txt", "game/../../x", "/etc/passwd", "C:\\x",
                         "a/./b/../c",    "..",           "game/..\\up"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        zip_begin();
        zip_add("ok.txt", "fine");
        zip_add(bad[i], "no");
        zip_end(zip);
        int r = unzip_extract(zip, dir, &n, err, sizeof err);
        if (r == 0)
            fprintf(stderr, "extracted an archive with %s in it\n", bad[i]);
        CHECK(r != 0);
    }
    /* a plain dotted directory is fine: it is only the two dots */
    zip_begin();
    zip_add("v1.5/", "");
    zip_add("v1.5/a.txt", "a");
    zip_end(zip);
    CHECK(unzip_extract(zip, dir, &n, err, sizeof err) == 0);

    /* a zip that extracts itself: sixteen thousand bytes of program first */
    {
        char sfx[600];
        snprintf(sfx, sizeof sfx, "%s/t.exe", TEST_TMP);
        zip_begin();
        zip_add("OMF.EXE", "the game");
        zip_add("DATA/ARENA0.BK", "an arena");
        zip_end(zip);
        sfx_from(zip, sfx, 16088);
        CHECK(unzip_extract(sfx, dir, &n, err, sizeof err) == 0);
        CHECK(n == 2);
        snprintf(path, sizeof path, "%sDATA/ARENA0.BK", dir);
        CHECK(file_says(path, "an arena"));
    }

    /* not a zip */
    FILE *f = fopen(zip, "wb");
    if (f) {
        fputs("this is not an archive at all, not even close", f);
        fclose(f);
    }
    CHECK(unzip_extract(zip, dir, &n, err, sizeof err) != 0);
    CHECK(unzip_extract("/no/such/file.zip", dir, &n, err, sizeof err) != 0);

    /* sha256, on the vectors everyone uses */
    char hex[65];
    sha256 c;
    sha256_init(&c);
    sha256_update(&c, "abc", 3);
    sha256_final(&c, hex);
    CHECK_STR(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    sha256_init(&c);
    sha256_final(&c, hex);
    CHECK_STR(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    snprintf(path, sizeof path, "%sgame/README.TXT", dir);
    CHECK(sha256_matches(
              path, "8b1c2fbd4d8f9df6f0d6b6ed8d2b3e0c0000000000000000000000000000000000") == 0);
    CHECK(sha256_file(path, hex) == 0);
    CHECK(sha256_matches(path, hex));

    return check_done("unzip");
}
