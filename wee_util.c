#include "wee_util.h"

/*
 * misc helpers.
 *
 * this module provides tiny utilities shared across the editor.
 */

/* die prints an error, clears the screen, and exits(1). */
void
die(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
	exit(1);
}
