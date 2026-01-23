#ifndef EX_H
#define EX_H

#include "wee.h"

/* searchdo performs a forward/backward search using the last pattern. */
void searchdo(struct editor *e, int dir);

/* cmdexec runs the current cmdline (':' or '/' prompt). */
void cmdexec(struct editor *e);

#endif
