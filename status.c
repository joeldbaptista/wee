#include "status.h"

/*
 * status line helpers.
 *
 * keeps the transient status message and mode display logic.
 */

/* setstatus formats a transient status message displayed at the bottom. */
void
setstatus(struct editor *e, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(e->status, sizeof(e->status), fmt, ap);
	va_end(ap);
	e->statustime = time(NULL);
}

/* modestr returns a human-readable mode string for the status line. */
const char *
modestr(struct editor *e)
{
	switch (e->mode) {
	case mnormal:
		return "NORMAL";
	case minsert:
		return "INSERT";
	case mvisual:
		return "VISUAL";
	case mcmd:
		return "CMD";
	}
	return "?";
}
