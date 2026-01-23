#ifndef STATUS_H
#define STATUS_H

#include "wee.h"

/* setstatus formats a transient status message displayed at the bottom. */
void setstatus(struct editor *e, const char *fmt, ...);

/* modestr returns a human-readable mode string for the status line. */
const char *modestr(struct editor *e);

#endif
