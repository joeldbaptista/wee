#ifndef EDIT_H
#define EDIT_H

#include "wee.h"

/* enterinsert switches to INSERT mode and starts a new insert group. */
void enterinsert(struct editor *e);

/* visual selection helpers. */
void vison(struct editor *e);
void visoff(struct editor *e);
int viswant(struct editor *e);
int visrange(struct editor *e, size_t *a, size_t *b);

/* yankset copies [a,b) into the yank buffer (optionally linewise). */
void yankset(struct editor *e, size_t a, size_t b, bool linewise);

/* bufdelrange deletes bytes in [a,b) from the main buffer and records undo. */
void bufdelrange(struct editor *e, size_t a, size_t b);

/* bufinsert inserts n bytes at into the main buffer and records undo. */
void bufinsert(struct editor *e, size_t at, const void *p, size_t n);

/* pasteafter inserts the yank buffer after the cursor (p). */
void pasteafter(struct editor *e);

/* delchar deletes the byte/codepoint at the cursor (x / DEL). */
void delchar(struct editor *e);

/* openbelow/openabove insert a newline and enter insert mode (o/O). */
void openbelow(struct editor *e);
void openabove(struct editor *e);

/* backspace deletes the previous codepoint in insert mode. */
void backspace(struct editor *e);

/* insbyte inserts a single byte at the cursor (insert mode). */
void insbyte(struct editor *e, int c);

/* insnl inserts a newline at the cursor (insert mode). */
void insnl(struct editor *e);

/* normreset clears pending count/operator state. */
void normreset(struct editor *e);

/* usecount returns the active count (defaults to 1). */
int usecount(struct editor *e);

/* applymotion resolves a motion (with count) and applies any pending operator. */
void applymotion(struct editor *e, int key);

/* cursor motion helpers (used by insert mode). */
size_t motionh(struct editor *e, size_t p);
size_t motionl(struct editor *e, size_t p);
size_t motionj(struct editor *e, size_t p);
size_t motionk(struct editor *e, size_t p);

/* applytextobjinner applies a pending operator to an inner text object (i?). */
void applytextobjinner(struct editor *e, int ch);

#endif
