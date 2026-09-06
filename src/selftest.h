/* selftest.h — --selftest: launch a game, unwind it with Esc, relaunch it,
 * twice, in one process.  What proves PORTING §3.1 and §3.2 hold. */
#ifndef DXM_SELFTEST_H
#define DXM_SELFTEST_H
/* Drive the machine one step at machine time t.  Returns 1 when the test
 * has finished and printed its verdict. */
int selftest_step(double t);
#endif
