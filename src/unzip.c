/* unzip.c — extracting a game's data archive.
 *
 * zlib does the decompression; the container is walked here, because the
 * part of ZIP we need is small: find the end-of-central-directory record,
 * walk the entries it points at, and inflate each one.
 *
 * The archive comes off the network, so the entry names in it are untrusted
 * input.  Anything that could escape the destination directory is refused
 * outright rather than sanitised - a name with ".." in it is not a mistake
 * to be corrected, it is an archive we should not be extracting. */
#include "unzip.h"
#include <zlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EOCD_SIG 0x06054b50u
#define CD_SIG   0x02014b50u
#define LFH_SIG  0x04034b50u

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1]<<8); }

/* Reject anything that is not a plain relative name in the destination. */
static int name_is_safe(const char *n) {
    if (!n || !*n) return 0;
    if (n[0] == '/' || n[0] == '\\') return 0;
    if (strstr(n, "..")) return 0;
    if (n[1] == ':') return 0;                       /* C:\ ... */
    for (const char *p = n; *p; p++)
        if ((unsigned char)*p < 0x20) return 0;
    return 1;
}

static int write_out(const char *dir, const char *name, const uint8_t *data,
                     size_t n, char *err, size_t errsz) {
    /* Flatten: DOS game data is a flat directory, and creating a tree from
     * an untrusted archive is a larger surface than this needs. */
    const char *base = name;
    for (const char *p = name; *p; p++)
        if (*p == '/' || *p == '\\') base = p + 1;
    if (!*base) return 0;                            /* a directory entry */
    char path[1400];
    snprintf(path, sizeof path, "%s%s", dir, base);
    FILE *f = fopen(path, "wb");
    if (!f) { snprintf(err, errsz, "cannot write %s", base); return -1; }
    int ok = n == 0 || fwrite(data, 1, n, f) == n;
    fclose(f);
    if (!ok) { snprintf(err, errsz, "short write on %s", base); return -1; }
    return 0;
}

static int inflate_raw(const uint8_t *in, size_t inn, uint8_t *out, size_t outn,
                       char *err, size_t errsz) {
    z_stream zs;
    memset(&zs, 0, sizeof zs);
    /* negative window bits: a raw deflate stream, with no zlib header */
    if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) {
        snprintf(err, errsz, "inflate init failed");
        return -1;
    }
    zs.next_in  = (Bytef *)in;  zs.avail_in  = (uInt)inn;
    zs.next_out = out;          zs.avail_out = (uInt)outn;
    int r = inflate(&zs, Z_FINISH);
    inflateEnd(&zs);
    if (r != Z_STREAM_END || zs.total_out != outn) {
        snprintf(err, errsz, "archive is corrupt");
        return -1;
    }
    return 0;
}

int unzip_extract(const char *zip, const char *destdir, int *n_out,
                  char *err, size_t errsz) {
    if (n_out) *n_out = 0;
    FILE *f = fopen(zip, "rb");
    if (!f) { snprintf(err, errsz, "cannot open archive"); return -1; }
    fseek(f, 0, SEEK_END);
    long fsz = ftell(f);
    if (fsz < 22 || fsz > 512L*1024*1024) {
        fclose(f); snprintf(err, errsz, "not an archive"); return -1;
    }
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc((size_t)fsz);
    if (!buf) { fclose(f); snprintf(err, errsz, "out of memory"); return -1; }
    size_t got = fread(buf, 1, (size_t)fsz, f);
    fclose(f);
    if (got != (size_t)fsz) { free(buf); snprintf(err,errsz,"short read"); return -1; }

    /* The EOCD is last, but a trailing comment can push it back - scan. */
    long e = -1;
    for (long i = fsz - 22; i >= 0 && i > fsz - 22 - 65536; i--)
        if (rd32(buf + i) == EOCD_SIG) { e = i; break; }
    if (e < 0) { free(buf); snprintf(err, errsz, "not a zip file"); return -1; }

    int      count = rd16(buf + e + 10);
    uint32_t cdoff = rd32(buf + e + 16);
    if ((long)cdoff >= fsz) { free(buf); snprintf(err,errsz,"zip is truncated"); return -1; }

    long p = (long)cdoff;
    int  written = 0;
    for (int i = 0; i < count; i++) {
        if (p + 46 > fsz || rd32(buf + p) != CD_SIG) {
            free(buf); snprintf(err, errsz, "zip directory is damaged"); return -1;
        }
        uint16_t method = rd16(buf + p + 10);
        uint32_t csize  = rd32(buf + p + 20);
        uint32_t usize  = rd32(buf + p + 24);
        uint16_t nlen   = rd16(buf + p + 28);
        uint16_t elen   = rd16(buf + p + 30);
        uint16_t clen   = rd16(buf + p + 32);
        uint32_t lho    = rd32(buf + p + 42);

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
        if (usize == 0 && (name[strlen(name)-1] == '/')) continue;   /* directory */
        if ((long)lho + 30 > fsz) continue;
        if (rd32(buf + lho) != LFH_SIG) continue;
        uint32_t dat = lho + 30 + rd16(buf + lho + 26) + rd16(buf + lho + 28);
        if ((long)dat + (long)csize > fsz) {
            free(buf); snprintf(err, errsz, "zip entry runs past the file"); return -1;
        }

        if (method == 0) {                                  /* stored */
            if (write_out(destdir, name, buf + dat, csize, err, errsz) != 0) {
                free(buf); return -1;
            }
        } else if (method == 8) {                           /* deflate */
            uint8_t *out = malloc(usize ? usize : 1);
            if (!out) { free(buf); snprintf(err,errsz,"out of memory"); return -1; }
            if (inflate_raw(buf + dat, csize, out, usize, err, errsz) != 0
             || write_out(destdir, name, out, usize, err, errsz) != 0) {
                free(out); free(buf); return -1;
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
    if (n_out) *n_out = written;
    return 0;
}
