/* unzip.c — extracting a title's archive.
 *
 * zlib does the decompression; the container is walked here, because the
 * part of ZIP needed is small: find the end-of-central-directory record,
 * walk the entries it points at, and inflate each one.
 *
 * The archive comes off the network, so the entry names in it are untrusted
 * input.  Anything that could escape the destination is refused outright
 * rather than sanitised - a name with ".." in it is not a mistake to be
 * corrected, it is an archive that should not be extracted.  The tree in the
 * archive is kept: which folder in it is the title is decided afterwards, by
 * looking for the file the catalogue says to run (install.c). */
#include "unzip.h"
#define ZLIB_CONST /* next_in is const, as our input is */
#include <zlib.h>
#include <stdint.h>
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

#define EOCD_SIG 0x06054b50u
#define CD_SIG 0x02014b50u
#define LFH_SIG 0x04034b50u
#define ZIP_MAX (256L * 1024 * 1024)

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)(p[0] | p[1] << 8);
}

/* A plain relative name, or nothing: no root, no drive, no "..", no
 * control characters, and no path element that is only dots. */
static int name_is_safe(const char *n) {
    if (!n || !*n)
        return 0;
    if (n[0] == '/' || n[0] == '\\')
        return 0;
    if (n[1] == ':')
        return 0; /* C:\ ... */
    const char *el = n;
    for (const char *p = n;; p++) {
        if (*p && (unsigned char)*p < 0x20)
            return 0;
        if (*p == '/' || *p == '\\' || !*p) {
            size_t len = (size_t)(p - el);
            if (len > 0 && len <= 2 && el[0] == '.' && (len == 1 || el[1] == '.'))
                return 0;
            if (!*p)
                break;
            el = p + 1;
        }
    }
    return 1;
}

/* Make every directory on the way to `path`'s last element. */
static void make_dirs(const char *dir, const char *name) {
    char path[1400];
    size_t base = strlen(dir);
    snprintf(path, sizeof path, "%s%s", dir, name);
    for (size_t i = base; path[i]; i++)
        if (path[i] == '/' || path[i] == '\\') {
            char c = path[i];
            path[i] = 0;
            MKDIR(path);
            path[i] = c;
        }
}

static int write_out(const char *dir, const char *name, const uint8_t *data, size_t n, char *err,
                     size_t errsz) {
    size_t len = strlen(name);
    if (name[len - 1] == '/' || name[len - 1] == '\\') {
        make_dirs(dir, name);
        char path[1400];
        snprintf(path, sizeof path, "%s%.*s", dir, (int)(len - 1), name);
        MKDIR(path);
        return 0; /* a directory entry */
    }
    make_dirs(dir, name);
    char path[1400];
    snprintf(path, sizeof path, "%s%s", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) {
        snprintf(err, errsz, "cannot write %s", name);
        return -1;
    }
    int ok = n == 0 || fwrite(data, 1, n, f) == n;
    fclose(f);
    if (!ok) {
        snprintf(err, errsz, "short write on %s", name);
        return -1;
    }
    return 0;
}

static int inflate_raw(const uint8_t *in, size_t inn, uint8_t *out, size_t outn, char *err,
                       size_t errsz) {
    z_stream zs;
    memset(&zs, 0, sizeof zs);
    /* negative window bits: a raw deflate stream, with no zlib header */
    if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) {
        snprintf(err, errsz, "inflate init failed");
        return -1;
    }
    zs.next_in = in;
    zs.avail_in = (uInt)inn;
    zs.next_out = out;
    zs.avail_out = (uInt)outn;
    int r = inflate(&zs, Z_FINISH);
    inflateEnd(&zs);
    if (r != Z_STREAM_END || zs.total_out != outn) {
        snprintf(err, errsz, "archive is corrupt");
        return -1;
    }
    return 0;
}

int unzip_extract(const char *zip, const char *destdir, int *n_out, char *err, size_t errsz) {
    if (n_out)
        *n_out = 0;
    FILE *f = fopen(zip, "rb");
    if (!f) {
        snprintf(err, errsz, "cannot open archive");
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long fsz = ftell(f);
    if (fsz < 22 || fsz > ZIP_MAX) {
        fclose(f);
        snprintf(err, errsz, "not an archive");
        return -1;
    }
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc((size_t)fsz);
    if (!buf) {
        fclose(f);
        snprintf(err, errsz, "out of memory");
        return -1;
    }
    size_t got = fread(buf, 1, (size_t)fsz, f);
    fclose(f);
    if (got != (size_t)fsz) {
        free(buf);
        snprintf(err, errsz, "short read");
        return -1;
    }

    /* The EOCD is last, but a trailing comment can push it back - scan. */
    long e = -1;
    for (long i = fsz - 22; i >= 0 && i > fsz - 22 - 65536; i--)
        if (rd32(buf + i) == EOCD_SIG) {
            e = i;
            break;
        }
    if (e < 0) {
        free(buf);
        snprintf(err, errsz, "not a zip file");
        return -1;
    }

    int count = rd16(buf + e + 10);
    uint32_t cdsize = rd32(buf + e + 12);
    uint32_t cdoff = rd32(buf + e + 16);
    /* A zip that extracts itself has a program in front of it, and the
     * offsets it records were written before the program was put there: the
     * central directory ends where the end record starts, so where it really
     * begins, less where it says it begins, is how far everything moved. */
    long shift = e - (long)cdsize - (long)cdoff;
    if (shift < 0 || shift >= fsz)
        shift = 0;
    cdoff += (uint32_t)shift;
    if ((long)cdoff >= fsz) {
        free(buf);
        snprintf(err, errsz, "zip is truncated");
        return -1;
    }

    long p = (long)cdoff;
    int written = 0;
    for (int i = 0; i < count; i++) {
        if (p + 46 > fsz || rd32(buf + p) != CD_SIG) {
            free(buf);
            snprintf(err, errsz, "zip directory is damaged");
            return -1;
        }
        uint16_t method = rd16(buf + p + 10);
        uint32_t csize = rd32(buf + p + 20);
        uint32_t usize = rd32(buf + p + 24);
        uint16_t nlen = rd16(buf + p + 28);
        uint16_t elen = rd16(buf + p + 30);
        uint16_t clen = rd16(buf + p + 32);
        uint32_t lho = rd32(buf + p + 42) + (uint32_t)shift;

        char name[512];
        size_t take = nlen < sizeof name - 1 ? nlen : sizeof name - 1;
        memcpy(name, buf + p + 46, take);
        name[take] = 0;
        p += 46 + nlen + elen + clen;

        if (!name_is_safe(name)) {
            free(buf);
            snprintf(err, errsz, "archive contains an unsafe path");
            return -1;
        }
        if (name[strlen(name) - 1] == '/' || name[strlen(name) - 1] == '\\') {
            write_out(destdir, name, NULL, 0, err, errsz); /* a directory */
            continue;
        }
        if ((long)lho + 30 > fsz)
            continue;
        if (rd32(buf + lho) != LFH_SIG)
            continue;
        uint32_t dat = lho + 30 + rd16(buf + lho + 26) + rd16(buf + lho + 28);
        if ((long)dat + (long)csize > fsz) {
            free(buf);
            snprintf(err, errsz, "zip entry runs past the file");
            return -1;
        }

        if (method == 0) { /* stored */
            if (write_out(destdir, name, buf + dat, csize, err, errsz) != 0) {
                free(buf);
                return -1;
            }
        } else if (method == 8) { /* deflate */
            uint8_t *out = malloc(usize ? usize : 1);
            if (!out) {
                free(buf);
                snprintf(err, errsz, "out of memory");
                return -1;
            }
            if (inflate_raw(buf + dat, csize, out, usize, err, errsz) != 0 ||
                write_out(destdir, name, out, usize, err, errsz) != 0) {
                free(out);
                free(buf);
                return -1;
            }
            free(out);
        } else {
            free(buf);
            snprintf(err, errsz, "unsupported compression in archive");
            return -1;
        }
        written++;
    }
    free(buf);
    if (n_out)
        *n_out = written;
    return 0;
}
