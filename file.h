#ifndef FILE_H
#define FILE_H

#include "wee.h"

/* filenew resets state for a new, empty buffer. */
void filenew(struct editor *e);

/* fileopen loads path into the buffer (or creates an empty new file). */
void fileopen(struct editor *e, const char *path);

/* filesave writes the current buffer to E.filename (atomic via .tmp). */
void filesave(struct editor *e);

#endif
