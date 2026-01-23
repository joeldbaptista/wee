#ifndef WEE_H
#define WEE_H

#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE 1
#endif

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/*
 * core types for wee.
 *
 * wee is a small vi-ish editor. the file lives in a single growable byte buffer
 * (buf). cursor and motions operate on byte offsets into that buffer.
 */

enum {
	knull = 0,
	kesc = 27,
	kenter = 13,
	kbs = 127,
	kdel = 1000,
	khome,
	kend,
	kpgup,
	kpgdn,
	kup,
	kdown,
	kleft,
	kright,
};

enum mode {
	mnormal,
	minsert,
	mvisual,
	mcmd,
};

enum {
	tabstop = 8,
};

/* simple growable byte buffer used for the file, yank, cmdline, and undo text. */
struct sbuf {
	char *s;
	size_t len;
	size_t cap;
};

/*
 * undo is a stack of edits.
 * for inserts we store the inserted bytes so undo can delete them.
 * for deletes we store the removed bytes so undo can reinsert them.
 */
struct undo {
	int kind; /* 'i' insert, 'd' delete */
	size_t at;
	size_t cur;
	int grp;
	struct sbuf text;
};

/* editor state. most fields are manipulated directly for simplicity. */
struct editor {
	int screenrows;
	int screencols;
	int textrows;

	enum mode mode;
	enum mode prevmode;
	char *filename;
	bool dirty;

	struct sbuf buf;
	/* byte offset into buf (kept on utf-8 lead bytes). */
	size_t cur;
	size_t vmark;

	int rowoff; /* top line number (0-based) */
	int coloff; /* left column (0-based) */

	struct sbuf yank;
	bool yankline;

	int count;
	int op; /* 'd', 'y', 'c' or 0 */

	char status[128];
	time_t statustime;

	struct sbuf cmd;
	char cmdpre;
	struct sbuf search;
	bool shownum;
	bool shownumrel;

	/* cached index of line start offsets in E.buf (rebuilt lazily). */
	size_t *linest;
	int linelen;
	int linecap;
	bool linedirty;

	struct undo *undo;
	int undolen;
	int undocap;
	int insgrp;
};

#endif
