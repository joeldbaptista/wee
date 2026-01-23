#ifndef LINES_H
#define LINES_H

#include "wee.h"

/* linesdirty marks the line-start cache as needing a rebuild. */
void linesdirty(struct editor *e);

/* linecount returns the number of lines in the buffer (>= 1). */
int linecount(struct editor *e);

/* linestart returns the offset of the start of the line containing at. */
size_t linestart(struct editor *e, size_t at);

/* lineend returns the offset of the end of the line containing at. */
size_t lineend(struct editor *e, size_t at);

/* off2row maps a byte offset to a 0-based row index. */
int off2row(struct editor *e, size_t off);

/* row2off maps a 0-based row index to its starting byte offset. */
size_t row2off(struct editor *e, int row);

/* off2col maps a byte offset to a display column (tabs expanded). */
int off2col(struct editor *e, size_t off);

/* offatcol maps a desired display column to a byte offset within [ls,le]. */
size_t offatcol(struct editor *e, size_t ls, size_t le, int want);

/* clampcur keeps E.cur in-range and on a utf-8 lead byte. */
void clampcur(struct editor *e);

/* numw returns the width of the line-number gutter (0 if disabled). */
int numw(struct editor *e);

#endif
