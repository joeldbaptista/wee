#ifndef MODE_H
#define MODE_H

#include "term.h"

/* processkey reads a key and dispatches based on the current mode. */
void processkey(struct editor *e);

/* key handlers for each mode. */
void normkey(struct editor *e, int key);
void viskey(struct editor *e, int key);
void inskey(struct editor *e, struct key k);
void cmdkey(struct editor *e, struct key k);

#endif
