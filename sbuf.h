#ifndef SBUF_H
#define SBUF_H

#include "wee.h"

/* sbufsetlen resizes b and sets its length (always NUL-terminates). */
void sbufsetlen(struct editor *e, struct sbuf *b, size_t n);

/* sbuffree frees the buffer storage and resets fields. */
void sbuffree(struct editor *e, struct sbuf *b);

/* sbufins inserts n bytes from p into b at offset at. */
void sbufins(struct editor *e, struct sbuf *b, size_t at, const void *p, size_t n);

/* sbufdel deletes n bytes from b starting at offset at. */
void sbufdel(struct editor *e, struct sbuf *b, size_t at, size_t n);

#endif
