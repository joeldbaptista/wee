#ifndef TERM_H
#define TERM_H

#include "wee.h"

enum {
	TERM_KEYMAX = 32,
};

/*
 * key is the decoded result of one keypress.
 *
 * b[0..n) holds the raw input bytes read from the terminal.
 * key is either a wee key code (kesc, kup, ...) or a byte value (0..255).
 */
struct key {
	unsigned char b[TERM_KEYMAX];
	int n;
	int key;
};

/* rawon enables raw terminal mode and registers atexit cleanup. */
void rawon(void);

/* readkey reads one keypress (or returns knull on timeout/resize). */
int readkey(void);

/* readkeyex reads one keypress and returns its raw bytes plus decoded key. */
struct key readkeyex(void);

/* onsigwinch sets an internal resize flag (SIGWINCH handler). */
void onsigwinch(int sig);

/* setwinsz queries terminal size and updates e->screenrows/screencols/textrows. */
void setwinsz(struct editor *e);

/* winchtick applies a pending resize at a safe point. */
void winchtick(struct editor *e);

#endif
