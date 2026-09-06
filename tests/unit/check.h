/* check.h — the whole test framework.  A CHECK that fails prints where and
 * what, and the test's exit status says whether any did.  No fixtures, no
 * runners, no registration: a test is a main() that calls CHECK. */
#ifndef DXM_CHECK_H
#define DXM_CHECK_H
#include <stdio.h>
#include <string.h>

static int checks_run, checks_failed;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        checks_run++;                                                                              \
        if (!(cond)) {                                                                             \
            checks_failed++;                                                                       \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond);               \
        }                                                                                          \
    } while (0)

#define CHECK_STR(got, want)                                                                       \
    do {                                                                                           \
        checks_run++;                                                                              \
        const char *g_ = (got), *w_ = (want);                                                      \
        if (!g_ || strcmp(g_, w_) != 0) {                                                          \
            checks_failed++;                                                                       \
            fprintf(stderr, "%s:%d: got \"%s\", wanted \"%s\"\n", __FILE__, __LINE__,              \
                    g_ ? g_ : "(null)", w_);                                                       \
        }                                                                                          \
    } while (0)

/* The last line of every test. */
static int check_done(const char *name) {
    fprintf(stderr, "%s: %d checks, %d failed\n", name, checks_run, checks_failed);
    return checks_failed ? 1 : 0;
}
#endif
