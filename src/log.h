/* log.h — startup and runtime diagnostics.
 *
 * Every line goes to stderr AND to dxm.log in the preferences directory.
 * stderr alone is useless for the case that matters: a machine where DXM
 * was double-clicked and sits on the splash.  There is no console to read,
 * and the report that comes back is "it hangs".  The file says how far it
 * got and how long each step took, from either thread. */
#ifndef DXM_LOG_H
#define DXM_LOG_H
void log_open(const char *pref_dir);          /* dxm.log under pref_dir */
void log_close(void);
void dxm_log(const char *fmt,...) __attribute__((format(printf,1,2)));
#endif
