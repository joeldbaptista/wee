#ifndef UNDO_H
#define UNDO_H

#include "wee.h"

/* undoclear frees undo history and resets the stack. */
void undoclear(struct editor *e);

/* undopushins records an insertion for undo (optionally merged). */
void undopushins(struct editor *e, size_t at, const void *p, size_t n, size_t cur, bool merge);

/* undopushdel records a deletion for undo. */
void undopushdel(struct editor *e, size_t at, const void *p, size_t n, size_t cur);

/* undodo applies the last undo entry. */
void undodo(struct editor *e);

#endif
