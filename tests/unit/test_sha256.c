/* sha256: the published test vectors, the streaming interface, and the two
 * file helpers the installer relies on to refuse a bad download. */
#include "check.h"
#include "sha256.h"
#include <stdlib.h>

static void digest(const char *msg, size_t n, char out[65]) {
    sha256 c;
    sha256_init(&c);
    sha256_update(&c, msg, n);
    sha256_final(&c, out);
}

int main(void) {
    char out[65];

    /* FIPS 180-2 vectors */
    digest("", 0, out);
    CHECK_STR(out, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    digest("abc", 3, out);
    CHECK_STR(out, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const char *two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    digest(two, strlen(two), out);
    CHECK_STR(out, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

    /* one million 'a', fed in uneven pieces so the block buffering is
     * exercised across every boundary */
    {
        sha256 c;
        sha256_init(&c);
        char chunk[997];
        memset(chunk, 'a', sizeof chunk);
        size_t left = 1000000;
        while (left) {
            size_t n = left < sizeof chunk ? left : sizeof chunk;
            sha256_update(&c, chunk, n);
            left -= n;
        }
        sha256_final(&c, out);
        CHECK_STR(out, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    }

    /* the file helpers, on a file we write here */
    {
        char path[1024];
        snprintf(path, sizeof path, "%s/sha256.txt", TEST_TMP);
        FILE *f = fopen(path, "wb");
        CHECK(f != NULL);
        if (f) {
            fputs("abc", f);
            fclose(f);
        }
        CHECK(sha256_file(path, out) == 0);
        CHECK_STR(out, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        CHECK(sha256_matches(path,
                             "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
        /* upper-case hex is the same claim */
        CHECK(sha256_matches(path,
                             "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD"));
        CHECK(!sha256_matches(path,
                              "0000000000000000000000000000000000000000000000000000000000000000"));
        /* no claim: nothing to refuse */
        CHECK(sha256_matches(path, ""));
        CHECK(sha256_matches(path, NULL));
        /* a file that is not there matches nothing */
        CHECK(sha256_file("/nonexistent/for/sure", out) != 0);
        CHECK(!sha256_matches("/nonexistent/for/sure",
                              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
        remove(path);
    }
    return check_done("sha256");
}
