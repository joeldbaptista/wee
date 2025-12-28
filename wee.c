#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/*
 * wee is a small vi-ish editor.
 *
 * the whole file lives in a single growable byte buffer (E.buf).
 * cursor and motions operate on byte offsets into that buffer.
 * lines are separated by '\n'. the screen is redrawn on each key.
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
 * grp is used to coalesce typed inserts within one INSERT session.
 */
struct undo {
	int kind; /* 'i' insert, 'd' delete */
	size_t at;
	size_t cur;
	int grp;
	struct sbuf text;
};

static struct termios origterm;

/* set on SIGWINCH; checked in input loop to force a redraw. */
static volatile sig_atomic_t winch;

/* suppress undo recording while applying an undo. */
static bool undomute;

/* editor state. most fields are manipulated directly for simplicity. */
static struct {
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

	struct undo *undo;
	int undolen;
	int undocap;
	int insgrp;
} E;

static int linecount(void);
static void normreset(void);
static void yankset(size_t a, size_t b, bool linewise);
static void bufdelrange(size_t a, size_t b);
static void setwinsz(void);
static void undoclear(void);
static void undopushins(size_t at, const void *p, size_t n, size_t cur, bool merge);
static void undopushdel(size_t at, const void *p, size_t n, size_t cur);
static void undodo(void);
static void enterinsert(void);

static size_t utfprev(const char *s, size_t len, size_t i);
static size_t utfnext(const char *s, size_t len, size_t i);

static int findnext(const char *s, size_t slen, const char *pat, size_t plen, size_t start, size_t *pos);
static int findprev(const char *s, size_t slen, const char *pat, size_t plen, size_t before, size_t *pos);
static void searchdo(int dir);
static void subcmd(const char *cmd, size_t rs, size_t re, int hasrange);
static void bufinsert(size_t at, const void *p, size_t n);
static void vison(void);
static void visoff(void);
static int visrange(size_t *a, size_t *b);
static int viswant(void);
static void viskey(int key);

static void onsigwinch(int sig)
{
	(void)sig;
	winch = 1;
}

static void enterinsert(void)
{
	E.mode = minsert;
	E.insgrp++;
}

static void die(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
	exit(1);
}

static void sbufgrow(struct sbuf *b, size_t need)
{
	size_t nc;
	char *ns;

	if (b->cap >= need)
		return;

	nc = b->cap ? b->cap : 64;
	while (nc < need)
		nc *= 2;

	ns = realloc(b->s, nc);
	if (!ns)
		die("out of memory");
	b->s = ns;
	b->cap = nc;
}

static void sbufsetlen(struct sbuf *b, size_t n)
{
	sbufgrow(b, n + 1);
	b->len = n;
	b->s[b->len] = 0;
}

static void sbuffree(struct sbuf *b)
{
	free(b->s);
	b->s = NULL;
	b->len = 0;
	b->cap = 0;
}

static void sbufins(struct sbuf *b, size_t at, const void *p, size_t n)
{
	if (at > b->len)
		at = b->len;
	sbufgrow(b, b->len + n + 1);
	memmove(b->s + at + n, b->s + at, b->len - at);
	memcpy(b->s + at, p, n);
	b->len += n;
	b->s[b->len] = 0;
}

static void sbufdel(struct sbuf *b, size_t at, size_t n)
{
	if (at >= b->len)
		return;
	if (at + n > b->len)
		n = b->len - at;
	memmove(b->s + at, b->s + at + n, b->len - (at + n));
	b->len -= n;
	b->s[b->len] = 0;
}

static int getwinsz(int *rows, int *cols)
{
	struct winsize ws;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
		return -1;
	*cols = ws.ws_col;
	*rows = ws.ws_row;
	return 0;
}

static void rawoff(void)
{
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &origterm);
	write(STDOUT_FILENO, "\x1b[2 q\x1b[?25h", 10);
}

static void rawon(void)
{
	struct termios t;

	if (tcgetattr(STDIN_FILENO, &origterm) == -1)
		die("tcgetattr: %s", strerror(errno));
	atexit(rawoff);

	t = origterm;
	t.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	t.c_oflag &= ~(OPOST);
	t.c_cflag |= (CS8);
	t.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	t.c_cc[VMIN] = 0;
	t.c_cc[VTIME] = 1;

	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &t) == -1)
		die("tcsetattr: %s", strerror(errno));
}

/*
 * read one keypress.
 * returns knull on timeout or resize so the main loop can redraw.
 */
static int readkey(void)
{
	char c;
	ssize_t n;

	for (;;) {
		if (winch)
			return knull;
		n = read(STDIN_FILENO, &c, 1);
		if (n == 1)
			break;
		if (n == -1) {
			if (errno == EAGAIN)
				continue;
			if (errno == EINTR)
				return knull;
			die("read: %s", strerror(errno));
		}
	}

	if (c == '\x1b') {
		char seq[3];

		if (read(STDIN_FILENO, &seq[0], 1) != 1)
			return kesc;
		if (read(STDIN_FILENO, &seq[1], 1) != 1)
			return kesc;

		if (seq[0] == '[') {
			if (seq[1] >= '0' && seq[1] <= '9') {
				if (read(STDIN_FILENO, &seq[2], 1) != 1)
					return kesc;
				if (seq[2] == '~') {
					switch (seq[1]) {
					case '1': return khome;
					case '3': return kdel;
					case '4': return kend;
					case '5': return kpgup;
					case '6': return kpgdn;
					case '7': return khome;
					case '8': return kend;
					}
				}
			} else {
				switch (seq[1]) {
				case 'A': return kup;
				case 'B': return kdown;
				case 'C': return kright;
				case 'D': return kleft;
				case 'H': return khome;
				case 'F': return kend;
				}
			}
		}
		return kesc;
	}

	return (unsigned char)c;
}

static void setstatus(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(E.status, sizeof(E.status), fmt, ap);
	va_end(ap);
	E.statustime = time(NULL);
}

static const char *modestr(void)
{
	switch (E.mode) {
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

static void vison(void)
{
	E.vmark = E.cur;
	E.mode = mvisual;
}

static void visoff(void)
{
	E.mode = mnormal;
}

static int viswant(void)
{
	return E.mode == mvisual || (E.mode == mcmd && E.prevmode == mvisual);
}

static int visrange(size_t *a, size_t *b)
{
	size_t lo, hi;

	if (!viswant())
		return 0;
	lo = E.vmark < E.cur ? E.vmark : E.cur;
	hi = E.vmark < E.cur ? E.cur : E.vmark;
	if (hi < E.buf.len)
		hi = utfnext(E.buf.s, E.buf.len, hi);
	if (lo > E.buf.len)
		lo = E.buf.len;
	if (hi > E.buf.len)
		hi = E.buf.len;
	*a = lo;
	*b = hi;
	return 1;
}

static int ndigits(int n)
{
	int d;

	if (n < 0)
		n = -n;
	d = 1;
	while (n >= 10) {
		n /= 10;
		d++;
	}
	return d;
}

static int numw(void)
{
	if (!E.shownum)
		return 0;
	return ndigits(linecount()) + 1; /* digits + space */
}

static bool isutfcont(unsigned char c)
{
	return (c & 0xc0) == 0x80;
}

static size_t utfprev(const char *s, size_t len, size_t i)
{
	(void)len;
	if (i == 0)
		return 0;
	i--;
	while (i > 0 && isutfcont((unsigned char)s[i]))
		i--;
	return i;
}

static size_t utfnext(const char *s, size_t len, size_t i)
{
	size_t j;

	if (i >= len)
		return len;
	j = i + 1;
	while (j < len && isutfcont((unsigned char)s[j]))
		j++;
	return j;
}

static size_t linestart(size_t at)
{
	while (at > 0 && E.buf.s[at - 1] != '\n')
		at--;
	return at;
}

static size_t lineend(size_t at)
{
	while (at < E.buf.len && E.buf.s[at] != '\n')
		at++;
	return at;
}

static int linecount(void)
{
	int n;
	size_t i;

	if (E.buf.len == 0)
		return 1;
	n = 1;
	for (i = 0; i < E.buf.len; i++)
		if (E.buf.s[i] == '\n')
			n++;
	return n;
}

static int off2row(size_t off)
{
	int r;
	size_t i;

	r = 0;
	for (i = 0; i < off && i < E.buf.len; i++)
		if (E.buf.s[i] == '\n')
			r++;
	return r;
}

static int off2col(size_t off)
{
	size_t ls;
	size_t i;
	int col;

	ls = linestart(off);
	col = 0;
	i = ls;
	while (i < off && i < E.buf.len && E.buf.s[i] != '\n') {
		unsigned char c;
		size_t j;

		c = (unsigned char)E.buf.s[i];
		if (c == '\t') {
			col += tabstop - (col % tabstop);
			i++;
			continue;
		}
		j = utfnext(E.buf.s, E.buf.len, i);
		if (j <= i)
			j = i + 1;
		col++;
		i = j;
	}
	return col;
}

static size_t offatcol(size_t ls, size_t le, int want)
{
	size_t i;
	int col;

	if (want <= 0)
		return ls;
	col = 0;
	i = ls;
	while (i < le && i < E.buf.len && E.buf.s[i] != '\n') {
		unsigned char c;
		size_t j;
		int n;

		if (col >= want)
			break;
		c = (unsigned char)E.buf.s[i];
		if (c == '\t') {
			n = tabstop - (col % tabstop);
			if (col + n > want)
				break;
			col += n;
			i++;
			continue;
		}
		j = utfnext(E.buf.s, E.buf.len, i);
		if (j <= i)
			j = i + 1;
		col++;
		i = j;
	}
	return i;
}

static size_t row2off(int row)
{
	int r;
	size_t i;

	if (row <= 0)
		return 0;
	r = 0;
	for (i = 0; i < E.buf.len; i++) {
		if (E.buf.s[i] == '\n') {
			r++;
			if (r == row)
				return i + 1;
		}
	}
	return E.buf.len;
}

static void clampcur(void)
{
	if (E.cur > E.buf.len)
		E.cur = E.buf.len;
	if (E.cur < E.buf.len && isutfcont((unsigned char)E.buf.s[E.cur]))
		E.cur = utfprev(E.buf.s, E.buf.len, E.cur);
}

/* undo stack storage; grows as needed. */
static void undogrow(int need)
{
	int nc;
	struct undo *nu;

	if (E.undocap >= need)
		return;
	nc = E.undocap ? E.undocap : 64;
	while (nc < need)
		nc *= 2;
	nu = realloc(E.undo, (size_t)nc * sizeof(E.undo[0]));
	if (!nu)
		die("out of memory");
	E.undo = nu;
	E.undocap = nc;
}

static void undoclear(void)
{
	int i;
	for (i = 0; i < E.undolen; i++)
		sbuffree(&E.undo[i].text);
	free(E.undo);
	E.undo = NULL;
	E.undolen = 0;
	E.undocap = 0;
}

static void undopushins(size_t at, const void *p, size_t n, size_t cur, bool merge)
{
	struct undo *u;

	if (undomute || n == 0)
		return;

	if (merge && E.undolen > 0) {
		u = &E.undo[E.undolen - 1];
		if (u->kind == 'i' && u->grp == E.insgrp && u->at + u->text.len == at) {
			sbufins(&u->text, u->text.len, p, n);
			return;
		}
	}

	undogrow(E.undolen + 1);
	u = &E.undo[E.undolen++];
	memset(u, 0, sizeof(*u));
	u->kind = 'i';
	u->at = at;
	u->cur = cur;
	u->grp = E.insgrp;
	sbufsetlen(&u->text, 0);
	sbufins(&u->text, 0, p, n);
}

static void undopushdel(size_t at, const void *p, size_t n, size_t cur)
{
	struct undo *u;

	if (undomute || n == 0)
		return;

	undogrow(E.undolen + 1);
	u = &E.undo[E.undolen++];
	memset(u, 0, sizeof(*u));
	u->kind = 'd';
	u->at = at;
	u->cur = cur;
	u->grp = 0;
	sbufsetlen(&u->text, 0);
	sbufins(&u->text, 0, p, n);
}

static void undodo(void)
{
	struct undo u;

	if (E.undolen == 0) {
		setstatus("nothing to undo");
		return;
	}

	u = E.undo[--E.undolen];
	/* apply without recording a new undo step. */
	undomute = true;
	if (u.kind == 'i') {
		if (u.at <= E.buf.len)
			sbufdel(&E.buf, u.at, u.text.len);
		E.cur = u.cur;
	} else if (u.kind == 'd') {
		if (u.at <= E.buf.len)
			sbufins(&E.buf, u.at, u.text.s, u.text.len);
		E.cur = u.cur;
	}
	undomute = false;

	E.dirty = true;
	clampcur();
	sbuffree(&u.text);
	setstatus("undone");
}

/*
 * rendering pipeline.
 * scroll() maintains rowoff/coloff so E.cur stays visible.
 * refresh() redraws the full screen each keypress.
 */

static void scroll(void)
{
	int cy, cx;
	int w, textcols;

	cy = off2row(E.cur);
	cx = off2col(E.cur);
	w = numw();
	textcols = E.screencols - w;
	if (textcols < 1)
		textcols = 1;

	if (cy < E.rowoff)
		E.rowoff = cy;
	if (cy >= E.rowoff + E.textrows)
		E.rowoff = cy - E.textrows + 1;

	if (cx < E.coloff)
		E.coloff = cx;
	if (cx >= E.coloff + textcols)
		E.coloff = cx - textcols + 1;

	if (E.rowoff < 0)
		E.rowoff = 0;
	if (E.coloff < 0)
		E.coloff = 0;
}

static void drawrows(struct sbuf *ab)
{
	int y;
	size_t off;
	int w, digits;
	int lineno;
	int lcount;
	int curline;

	off = row2off(E.rowoff);
	w = numw();
	digits = w ? (w - 1) : 0;
	lcount = linecount();
	curline = off2row(E.cur) + 1;
	for (y = 0; y < E.textrows; y++) {
		size_t ls, le;
		int cols;
		int col;
		size_t i;
		int inv;
		size_t sa, sb;
		int hasvis;

		ls = off;
		lineno = E.rowoff + y + 1;
		cols = E.screencols - w;
		if (cols < 1)
			cols = 1;
		if (lineno > lcount || ls >= E.buf.len) {
			if (w) {
				int n;
				sbufins(ab, ab->len, "~", 1);
				n = w - 1;
				while (n-- > 0)
					sbufins(ab, ab->len, " ", 1);
			} else {
				sbufins(ab, ab->len, "~", 1);
			}
		} else {
			le = lineend(ls);
			hasvis = visrange(&sa, &sb);
			if (w) {
				char nb[32];
				int n;
				int shown;

				shown = lineno;
				if (E.shownumrel && lineno != curline)
					shown = lineno > curline ? (lineno - curline) : (curline - lineno);
				n = snprintf(nb, sizeof(nb), "%*d ", digits, shown);
				if (n > 0)
					sbufins(ab, ab->len, nb, (size_t)n);
			}
			col = 0;
			inv = 0;
			i = ls;
			while (i < le && i < E.buf.len && E.buf.s[i] != '\n') {
				unsigned char c;
				size_t j;
				int k;
				int n;
				int wantinv;

				if (col >= E.coloff + cols)
					break;
				c = (unsigned char)E.buf.s[i];
				wantinv = hasvis && i >= sa && i < sb;
				if (wantinv != inv) {
					if (wantinv)
						sbufins(ab, ab->len, "\x1b[7m", 4);
					else
						sbufins(ab, ab->len, "\x1b[m", 3);
					inv = wantinv;
				}
				if (c == '\t') {
					n = tabstop - (col % tabstop);
					for (k = 0; k < n; k++) {
						if (col >= E.coloff && col < E.coloff + cols)
							sbufins(ab, ab->len, " ", 1);
						col++;
						if (col >= E.coloff + cols)
							break;
					}
					i++;
					continue;
				}
				j = utfnext(E.buf.s, E.buf.len, i);
				if (j <= i)
					j = i + 1;
				if (col >= E.coloff && col < E.coloff + cols)
					sbufins(ab, ab->len, E.buf.s + i, j - i);
				col++;
				i = j;
			}
			if (inv)
				sbufins(ab, ab->len, "\x1b[m", 3);
			off = (le < E.buf.len && E.buf.s[le] == '\n') ? le + 1 : le;
		}
		sbufins(ab, ab->len, "\x1b[K", 3);
		sbufins(ab, ab->len, "\r\n", 2);
	}
}

static void drawstatus(struct sbuf *ab)
{
	char left[128], right[128];
	int llen, rlen;
	int lcount, row, col;

	row = off2row(E.cur) + 1;
	col = off2col(E.cur) + 1;
	lcount = linecount();

	snprintf(left, sizeof(left), " %s%s - %d lines [%s] ",
		E.filename ? E.filename : "[No Name]",
		E.dirty ? "*" : "",
		lcount,
		modestr());
	snprintf(right, sizeof(right), " %d,%d ", row, col);

	llen = (int)strlen(left);
	rlen = (int)strlen(right);

	sbufins(ab, ab->len, "\x1b[7m", 4);
	if (llen > E.screencols)
		llen = E.screencols;
	sbufins(ab, ab->len, left, (size_t)llen);
	while (llen < E.screencols) {
		if (E.screencols - llen == rlen) {
			sbufins(ab, ab->len, right, (size_t)rlen);
			llen += rlen;
			break;
		}
		sbufins(ab, ab->len, " ", 1);
		llen++;
	}
	sbufins(ab, ab->len, "\x1b[m", 3);
	sbufins(ab, ab->len, "\r\n", 2);
}

static void drawmsg(struct sbuf *ab)
{
	if (E.mode == mcmd) {
		char p;
		p = E.cmdpre ? E.cmdpre : ':';
		sbufins(ab, ab->len, &p, 1);
		if (E.cmd.len)
			sbufins(ab, ab->len, E.cmd.s, E.cmd.len);
		sbufins(ab, ab->len, "\x1b[K", 3);
		return;
	}

	if (E.status[0] && time(NULL) - E.statustime < 5) {
		size_t n;
		n = strlen(E.status);
		if ((int)n > E.screencols)
			n = (size_t)E.screencols;
		sbufins(ab, ab->len, E.status, n);
	}
	
	sbufins(ab, ab->len, "\x1b[K", 3);
}

static void refresh(void)
{
	struct sbuf ab = {0};
	char buf[32];
	int cy, cx;
	int w;

	scroll();
	if (E.mode == minsert)
		sbufins(&ab, ab.len, "\x1b[6 q", 5);
	else
		sbufins(&ab, ab.len, "\x1b[2 q", 5);

	sbufins(&ab, ab.len, "\x1b[?25l", 6);
	sbufins(&ab, ab.len, "\x1b[H", 3);

	drawrows(&ab);
	drawstatus(&ab);
	drawmsg(&ab);

	cy = off2row(E.cur) - E.rowoff + 1;
	w = numw();
	cx = off2col(E.cur) - E.coloff + 1 + w;
	if (cy < 1)
		cy = 1;
	if (cy > E.textrows)
		cy = E.textrows;
	if (cx < 1)
		cx = 1;
	if (cx > E.screencols)
		cx = E.screencols;

	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", cy, cx);
	sbufins(&ab, ab.len, buf, strlen(buf));
	
	sbufins(&ab, ab.len, "\x1b[?25h", 6);
	write(STDOUT_FILENO, ab.s, ab.len);
	sbuffree(&ab);
}

static void setwinsz(void)
{
	if (getwinsz(&E.screenrows, &E.screencols) == -1)
		die("getwinsz");
	E.textrows = E.screenrows - 2;
	if (E.textrows < 1)
		E.textrows = 1;
}

static void winchtick(void)
{
	/* handle resize at a safe point (outside the signal handler). */
	if (!winch)
		return;
	winch = 0;
	setwinsz();
}

static void filenew(void)
{
	undoclear();
	sbufsetlen(&E.buf, 0);
	E.cur = 0;
	E.dirty = false;
	E.rowoff = 0;
	E.coloff = 0;
}

static void fileopen(const char *path)
{
	int fd;
	struct stat st;
	size_t n;

	fd = open(path, O_RDONLY);
	if (fd == -1) {
		filenew();
		setstatus("new file");
		return;
	}
	if (fstat(fd, &st) == -1)
		die("fstat: %s", strerror(errno));
	if (st.st_size < 0)
		die("bad file size");

	n = (size_t)st.st_size;
	sbufsetlen(&E.buf, n);
	if (n && read(fd, E.buf.s, n) != (ssize_t)n)
		die("read file: %s", strerror(errno));
	close(fd);

	E.cur = 0;
	E.dirty = false;
	E.rowoff = 0;
	E.coloff = 0;
	undoclear();
}

static void filesave(void)
{
	int fd;
	ssize_t n;
	char *tmp;

	if (!E.filename) {
		setstatus("no filename");
		return;
	}

	tmp = malloc(strlen(E.filename) + 5);
	if (!tmp)
		die("out of memory");
	sprintf(tmp, "%s.tmp", E.filename);

	fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd == -1) {
		free(tmp);
		setstatus("write failed: %s", strerror(errno));
		return;
	}

	n = write(fd, E.buf.s, E.buf.len);
	if (n == -1 || (size_t)n != E.buf.len) {
		close(fd);
		unlink(tmp);
		free(tmp);
		setstatus("write failed: %s", strerror(errno));
		return;
	}

	if (fsync(fd) == -1) {
		close(fd);
		unlink(tmp);
		free(tmp);
		setstatus("fsync failed: %s", strerror(errno));
		return;
	}
	close(fd);

	if (rename(tmp, E.filename) == -1) {
		unlink(tmp);
		free(tmp);
		setstatus("rename failed: %s", strerror(errno));
		return;
	}
	free(tmp);

	E.dirty = false;
	setstatus("%zu bytes written", E.buf.len);
}

/*
 * edit primitives.
 * mutators that change E.buf should record undo via undopush*.
 */

static bool isword(int c)
{
	return isalnum((unsigned char)c) || c == '_';
}

static int cclass(int c)
{
	if (c == 0)
		return 0;
	if (c == '\n')
		return 0;
	if (isspace((unsigned char)c))
		return 0;
	if (isword(c))
		return 1;
	return 2;
}

static size_t motionh(size_t p)
{
	return utfprev(E.buf.s, E.buf.len, p);
}

static size_t motionl(size_t p)
{
	return utfnext(E.buf.s, E.buf.len, p);
}

static size_t motionbol(size_t p)
{
	return linestart(p);
}

static size_t motioneol(size_t p)
{
	size_t le;
	le = lineend(p);
	if (le > 0 && le == E.buf.len)
		return le;
	return le;
}

static size_t motionj(size_t p)
{
	int row, col;
	size_t np;
	size_t ls, le;

	row = off2row(p);
	col = off2col(p);
	np = row2off(row + 1);
	ls = np;
	le = lineend(ls);
	return offatcol(ls, le, col);
}

static size_t motionk(size_t p)
{
	int row, col;
	size_t np;
	size_t ls, le;

	row = off2row(p);
	col = off2col(p);
	np = row2off(row - 1);
	ls = np;
	le = lineend(ls);
	return offatcol(ls, le, col);
}

static size_t motiongg(size_t p)
{
	(void)p;
	return 0;
}

static size_t motioncapg(size_t p)
{
	(void)p;
	return row2off(linecount() - 1);
}

static size_t motiont(size_t p, int ch, int n)
{
	size_t scan, ls, le;
	int k;

	if (p >= E.buf.len)
		return p;

	scan = p;
	ls = linestart(scan);
	le = lineend(scan);
	for (k = 0; k < n; k++) {
		size_t i;
		size_t start;
		size_t found;

		ls = linestart(scan);
		le = lineend(scan);

		start = utfnext(E.buf.s, E.buf.len, scan);
		if (start > le)
			return p;

		found = le;
		for (i = start; i < le; i++) {
			if ((unsigned char)E.buf.s[i] == (unsigned char)ch) {
				found = i;
				break;
			}
		}
		if (found == le)
			return p;

		scan = found;
	}

	if (scan <= ls)
		return ls;
	return utfprev(E.buf.s, E.buf.len, scan);
}

static size_t motionf(size_t p, int ch, int n)
{
	size_t scan, le;
	int k;

	if (p >= E.buf.len)
		return p;

	scan = p;
	le = lineend(scan);
	for (k = 0; k < n; k++) {
		size_t i;
		size_t start;
		size_t found;

		le = lineend(scan);
		start = utfnext(E.buf.s, E.buf.len, scan);
		if (start > le)
			return p;

		found = le;
		for (i = start; i < le; i++) {
			if ((unsigned char)E.buf.s[i] == (unsigned char)ch) {
				found = i;
				break;
			}
		}
		if (found == le)
			return p;
		scan = found;
	}

	return scan;
}

static size_t motionw(size_t p)
{
	int c, t;

	if (p >= E.buf.len)
		return p;

	c = (unsigned char)E.buf.s[p];
	t = cclass(c);

	if (t == 0) {
		while (p < E.buf.len) {
			c = (unsigned char)E.buf.s[p];
			if (cclass(c) != 0)
				break;
			p = utfnext(E.buf.s, E.buf.len, p);
		}
		return p;
	}

	while (p < E.buf.len) {
		c = (unsigned char)E.buf.s[p];
		if (c == '\n')
			break;
		if (cclass(c) != t)
			break;
		p = utfnext(E.buf.s, E.buf.len, p);
	}
	while (p < E.buf.len) {
		c = (unsigned char)E.buf.s[p];
		if (cclass(c) != 0)
			break;
		p = utfnext(E.buf.s, E.buf.len, p);
	}
	return p;
}

static int pairfor(int c, int *open, int *close)
{
	switch (c) {
	case '(':
	case ')':
		*open = '(';
		*close = ')';
		return 1;
	case '[':
	case ']':
		*open = '[';
		*close = ']';
		return 1;
	case '{':
	case '}':
		*open = '{';
		*close = '}';
		return 1;
	case '<':
	case '>':
		*open = '<';
		*close = '>';
		return 1;
	case '\'':
		*open = '\'';
		*close = '\'';
		return 1;
	case '\"':
		*open = '\"';
		*close = '\"';
		return 1;
	}
	return 0;
}

static int findinnerpair(int open, int close, size_t *a, size_t *b)
{
	size_t i, ls, le;
	ssize_t oi, ci;
	int depth;

	oi = -1;
	ci = -1;

	if (E.buf.len == 0)
		return 0;

	if (open == close) {
		ls = linestart(E.cur);
		le = lineend(E.cur);
		if (E.cur > le)
			return 0;

		for (i = E.cur; i > ls; ) {
			i = utfprev(E.buf.s, E.buf.len, i);
			if ((unsigned char)E.buf.s[i] == (unsigned char)open) {
				oi = (ssize_t)i;
				break;
			}
		}
		for (i = E.cur; i < le; ) {
			if ((unsigned char)E.buf.s[i] == (unsigned char)close) {
				ci = (ssize_t)i;
				break;
			}
			i = utfnext(E.buf.s, E.buf.len, i);
		}
		if (oi < 0 || ci < 0 || (size_t)oi >= (size_t)ci)
			return 0;
		*a = (size_t)oi + 1;
		*b = (size_t)ci;
		return 1;
	}

	depth = 0;
	for (i = E.cur; i > 0; ) {
		i = utfprev(E.buf.s, E.buf.len, i);
		if ((unsigned char)E.buf.s[i] == (unsigned char)close) {
			depth++;
			continue;
		}
		if ((unsigned char)E.buf.s[i] == (unsigned char)open) {
			if (depth == 0) {
				oi = (ssize_t)i;
				break;
			}
			depth--;
		}
	}
	if (oi < 0)
		return 0;

	depth = 0;
	for (i = (size_t)oi + 1; i < E.buf.len; ) {
		if ((unsigned char)E.buf.s[i] == (unsigned char)open) {
			depth++;
			i = utfnext(E.buf.s, E.buf.len, i);
			continue;
		}
		if ((unsigned char)E.buf.s[i] == (unsigned char)close) {
			if (depth == 0) {
				ci = (ssize_t)i;
				break;
			}
			depth--;
			i = utfnext(E.buf.s, E.buf.len, i);
			continue;
		}
		i = utfnext(E.buf.s, E.buf.len, i);
	}
	if (ci < 0)
		return 0;

	*a = (size_t)oi + 1;
	*b = (size_t)ci;
	return 1;
}

static void applytextobjinner(int ch)
{
	int open, close;
	size_t a, b;

	if (!pairfor(ch, &open, &close)) {
		setstatus("unknown textobj %c", (char)ch);
		normreset();
		return;
	}
	if (!findinnerpair(open, close, &a, &b)) {
		setstatus("no match for %c", (char)ch);
		normreset();
		return;
	}

	if (E.op == 'd' || E.op == 'c') {
		yankset(a, b, false);
		bufdelrange(a, b);
		if (E.op == 'c')
			enterinsert();
	} else if (E.op == 'y') {
		yankset(a, b, false);
		setstatus("yanked %zu bytes", E.yank.len);
	}
	normreset();
}

static size_t motionb(size_t p)
{
	if (p == 0)
		return 0;
	p = utfprev(E.buf.s, E.buf.len, p);
	while (p > 0 && E.buf.s[p] != '\n' && !isword((unsigned char)E.buf.s[p]))
		p = utfprev(E.buf.s, E.buf.len, p);
	while (p > 0 && E.buf.s[p] != '\n' && isword((unsigned char)E.buf.s[p])) {
		size_t pp;
		pp = utfprev(E.buf.s, E.buf.len, p);
		if (!isword((unsigned char)E.buf.s[pp]))
			break;
		p = pp;
	}
	return p;
}

static size_t motione(size_t p)
{
	if (p >= E.buf.len)
		return p;
	p = motionw(p);
	if (p >= E.buf.len)
		return p;
	while (p < E.buf.len && E.buf.s[p] != '\n' && isword((unsigned char)E.buf.s[p]))
		p = utfnext(E.buf.s, E.buf.len, p);
	return utfprev(E.buf.s, E.buf.len, p);
}

static void yankset(size_t a, size_t b, bool linewise)
{
	size_t n;

	if (b < a) {
		size_t t = a;
		a = b;
		b = t;
	}
	if (a > E.buf.len)
		a = E.buf.len;
	if (b > E.buf.len)
		b = E.buf.len;

	n = b - a;
	sbufsetlen(&E.yank, 0);
	if (n)
		sbufins(&E.yank, 0, E.buf.s + a, n);
	E.yankline = linewise;
}

static void bufdelrange(size_t a, size_t b)
{
	size_t cur;
	size_t n;

	if (b < a) {
		size_t t = a;
		a = b;
		b = t;
	}
	if (a > E.buf.len)
		a = E.buf.len;
	if (b > E.buf.len)
		b = E.buf.len;
	if (b == a)
		return;

	cur = E.cur;
	n = b - a;
	if (a < E.buf.len)
		/* record the deleted bytes for undo. */
		undopushdel(a, E.buf.s + a, n, cur);

	sbufdel(&E.buf, a, n);
	E.dirty = true;
	E.cur = a;
	clampcur();
}

static void bufinsert(size_t at, const void *p, size_t n)
{
	size_t cur;

	if (n == 0)
		return;
	if (at > E.buf.len)
		at = E.buf.len;
	cur = E.cur;
	undopushins(at, p, n, cur, false);
	sbufins(&E.buf, at, p, n);
	E.dirty = true;
}

static int findnext(const char *s, size_t slen, const char *pat, size_t plen, size_t start, size_t *pos)
{
	size_t i;

	if (plen == 0)
		return 0;
	if (start > slen)
		return 0;
	if (plen > slen)
		return 0;

	for (i = start; i + plen <= slen; i++) {
		if (memcmp(s + i, pat, plen) == 0) {
			*pos = i;
			return 1;
		}
	}
	return 0;
}

static int findprev(const char *s, size_t slen, const char *pat, size_t plen, size_t before, size_t *pos)
{
	size_t i;
	size_t last;
	int found;

	if (plen == 0)
		return 0;
	if (before > slen)
		before = slen;
	if (plen > slen)
		return 0;

	found = 0;
	last = 0;
	for (i = 0; i + plen <= before; i++) {
		if (memcmp(s + i, pat, plen) == 0) {
			last = i;
			found = 1;
		}
	}
	if (!found)
		return 0;
	*pos = last;
	return 1;
}

static void searchdo(int dir)
{
	size_t pos;
	size_t start;

	if (E.cmdpre == '/') {
		if (E.cmd.len) {
			sbufsetlen(&E.search, 0);
			sbufins(&E.search, 0, E.cmd.s, E.cmd.len);
		}
	}

	if (E.search.len == 0) {
		setstatus("no previous search");
		return;
	}

	if (dir >= 0) {
		start = (E.cur < E.buf.len) ? utfnext(E.buf.s, E.buf.len, E.cur) : E.cur;
		if (!findnext(E.buf.s, E.buf.len, E.search.s, E.search.len, start, &pos)) {
			setstatus("pattern not found");
			return;
		}
	} else {
		start = E.cur;
		if (start > 0)
			start = utfprev(E.buf.s, E.buf.len, start);
		if (!findprev(E.buf.s, E.buf.len, E.search.s, E.search.len, start, &pos)) {
			setstatus("pattern not found");
			return;
		}
	}

	E.cur = pos;
	clampcur();
	setstatus("match");
}

static void subcmd(const char *cmd, size_t rs, size_t re, int hasrange)
{
	int all;
	int global;
	char delim;
	size_t i;
	struct sbuf pat = {0};
	struct sbuf rep = {0};

	all = 0;
	global = 0;
	if (cmd[0] == '%' && cmd[1] == 's') {
		all = 1;
		cmd += 2;
	} else if (cmd[0] == 's') {
		cmd += 1;
	} else {
		setstatus("unknown command: %s", cmd);
		return;
	}

	delim = *cmd++;
	if (delim == 0) {
		setstatus("bad substitute");
		return;
	}

	for (i = 0; cmd[i]; i++) {
		char c;
		c = cmd[i];
		if (c == '\\' && cmd[i + 1]) {
			i++;
			sbufins(&pat, pat.len, &cmd[i], 1);
			continue;
		}
		if (c == delim)
			break;
		sbufins(&pat, pat.len, &c, 1);
	}
	if (cmd[i] != delim) {
		setstatus("bad substitute");
		goto out;
	}
	i++;

	for (; cmd[i]; i++) {
		char c;
		c = cmd[i];
		if (c == '\\' && cmd[i + 1]) {
			i++;
			sbufins(&rep, rep.len, &cmd[i], 1);
			continue;
		}
		if (c == delim)
			break;
		sbufins(&rep, rep.len, &c, 1);
	}
	if (cmd[i] == delim)
		i++;
	for (; cmd[i]; i++) {
		if (cmd[i] == 'g')
			global = 1;
	}

	if (pat.len == 0) {
		setstatus("empty pattern");
		goto out;
	}

	{
		size_t pos, next;
		int nsub;
		int first;

		if (!hasrange) {
			if (all) {
				rs = 0;
				re = E.buf.len;
			} else {
				rs = linestart(E.cur);
				re = lineend(E.cur);
			}
		} else {
			if (rs > E.buf.len)
				rs = E.buf.len;
			if (re > E.buf.len)
				re = E.buf.len;
			if (re < rs) {
				size_t t = rs;
				rs = re;
				re = t;
			}
		}

		nsub = 0;
		first = 1;
		pos = rs;
		while (pos <= re) {
			size_t m;
			if (!findnext(E.buf.s, re, pat.s, pat.len, pos, &m))
				break;
			if (m + pat.len > re)
				break;
			nsub++;
			if (first) {
				E.cur = m;
				first = 0;
			}
			bufdelrange(m, m + pat.len);
			bufinsert(m, rep.s, rep.len);
			next = m + rep.len;
			re = re + rep.len - pat.len;
			pos = next;
			if (!global)
				break;
		}
		if (nsub == 0)
			setstatus("no match");
		else
			setstatus("%d substitutions", nsub);
		clampcur();
	}

out:
	sbuffree(&pat);
	sbuffree(&rep);
}

static void pasteafter(void)
{
	size_t at;
	size_t cur;

	if (!E.yank.len)
		return;

	if (E.yankline) {
		size_t le;
		le = lineend(E.cur);
		at = (le < E.buf.len && E.buf.s[le] == '\n') ? le + 1 : le;
	} else {
		at = (E.cur < E.buf.len) ? utfnext(E.buf.s, E.buf.len, E.cur) : E.cur;
	}

	cur = E.cur;
	undopushins(at, E.yank.s, E.yank.len, cur, false);
	sbufins(&E.buf, at, E.yank.s, E.yank.len);
	E.dirty = true;
	E.cur = at;
	clampcur();
}

static void delchar(void)
{
	if (E.cur >= E.buf.len)
		return;
	bufdelrange(E.cur, utfnext(E.buf.s, E.buf.len, E.cur));
}

static void openbelow(void)
{
	size_t le;
	size_t at;
	char nl;
	size_t cur;

	le = lineend(E.cur);
	at = (le < E.buf.len && E.buf.s[le] == '\n') ? le + 1 : le;
	nl = '\n';
	cur = E.cur;
	undopushins(at, &nl, 1, cur, false);
	sbufins(&E.buf, at, &nl, 1);
	E.dirty = true;
	E.cur = at;
	enterinsert();
}

static void openabove(void)
{
	size_t ls;
	char nl;
	size_t cur;

	ls = linestart(E.cur);
	nl = '\n';
	cur = E.cur;
	undopushins(ls, &nl, 1, cur, false);
	sbufins(&E.buf, ls, &nl, 1);
	E.dirty = true;
	E.cur = ls;
	enterinsert();
}

static void backspace(void)
{
	size_t p;
	if (E.cur == 0)
		return;
	p = utfprev(E.buf.s, E.buf.len, E.cur);
	bufdelrange(p, E.cur);
}

static void insbyte(int c)
{
	char ch;
	size_t cur;

	ch = (char)c;
	cur = E.cur;
	undopushins(E.cur, &ch, 1, cur, true);
	sbufins(&E.buf, E.cur, &ch, 1);
	E.cur++;
	E.dirty = true;
}

static void insnl(void)
{
	char c;
	size_t cur;

	c = '\n';
	cur = E.cur;
	undopushins(E.cur, &c, 1, cur, true);
	sbufins(&E.buf, E.cur, &c, 1);
	E.cur++;
	E.dirty = true;
}

static void normreset(void)
{
	E.count = 0;
	E.op = 0;
}

static int usecount(void)
{
	return E.count ? E.count : 1;
}

/*
 * vi-ish command engine.
 * applymotion() resolves a motion (with count), then applies any pending op.
 */

static void applymotion(int key)
{
	int n;
	size_t start, end;
	bool linewise;

	start = E.cur;
	end = E.cur;
	linewise = false;

	n = usecount();

	if (key == 'g') {
		int k2 = readkey();
		if (k2 == 'g') {
			end = motiongg(E.cur);
		} else {
			setstatus("unknown g%c", (char)k2);
			normreset();
			return;
		}
	} else if (key == 'G') {
		if (E.count)
			end = row2off(E.count - 1);
		else
			end = motioncapg(E.cur);
	} else if (key == 't') {
		int ch;
		ch = readkey();
		end = motiont(end, ch, n);
	} else if (key == 'f') {
		int ch;
		ch = readkey();
		if (ch == kesc || ch == knull) {
			setstatus("find cancelled");
			normreset();
			return;
		}
		end = motionf(end, ch, n);
	} else {
		while (n--) {
			switch (key) {
			case 'h': end = motionh(end); break;
			case 'l': end = motionl(end); break;
			case 'j': end = motionj(end); break;
			case 'k': end = motionk(end); break;
			case '0': end = motionbol(end); break;
			case '$': end = motioneol(end); break;
			case 'w': end = motionw(end); break;
			case 'b': end = motionb(end); break;
			case 'e': end = motione(end); break;
			default:
				setstatus("unknown motion %c", (char)key);
				normreset();
				return;
			}
		}
	}

	if (!E.op) {
		E.cur = end;
		clampcur();
		normreset();
		return;
	}

	/* op + motion */
	if (E.op == 'd' || E.op == 'c') {
		if (key == 'e' && end < E.buf.len)
			end = utfnext(E.buf.s, E.buf.len, end);
		if (key == 'f' && end < E.buf.len)
			end = utfnext(E.buf.s, E.buf.len, end);
		linewise = false;
		yankset(start, end, linewise);
		bufdelrange(start, end);
		if (E.op == 'c')
			enterinsert();
	} else if (E.op == 'y') {
		if (key == 'e' && end < E.buf.len)
			end = utfnext(E.buf.s, E.buf.len, end);
		if (key == 'f' && end < E.buf.len)
			end = utfnext(E.buf.s, E.buf.len, end);
		yankset(start, end, linewise);
		setstatus("yanked %zu bytes", E.yank.len);
	}

	normreset();
}

/* parse one key in NORMAL mode (counts, operators, motions, and commands). */

static void normkey(int key)
{
	int n;

	if (key >= '0' && key <= '9') {
		if (E.count == 0 && key == '0') {
			applymotion('0');
			return;
		}
		E.count = E.count * 10 + (key - '0');
		return;
	}

	switch (key) {
	case 'i':
		if (E.op) {
			int ch;
			ch = readkey();
			applytextobjinner(ch);
			break;
		}
		enterinsert();
		setstatus("INSERT");
		normreset();
		break;
	case 'a':
		E.cur = motionl(E.cur);
		enterinsert();
		setstatus("INSERT");
		normreset();
		break;
	case 'A':
		E.cur = lineend(E.cur);
		enterinsert();
		setstatus("INSERT");
		normreset();
		break;
	case 'o':
		openbelow();
		setstatus("INSERT");
		normreset();
		break;
	case 'O':
		openabove();
		setstatus("INSERT");
		normreset();
		break;
	case 'C':
		E.op = 'c';
		E.count = 0;
		applymotion('$');
		setstatus("INSERT");
		break;
	case 'x':
		n = usecount();
		while (n--)
			delchar();
		normreset();
		break;
	case 'u':
		undodo();
		normreset();
		break;
	case 'p':
		pasteafter();
		normreset();
		break;
	case 'd':
		if (E.op == 'd') {
			/* dd */
			size_t a, b;
			a = linestart(E.cur);
			b = lineend(E.cur);
			if (b < E.buf.len && E.buf.s[b] == '\n')
				b++;
			yankset(a, b, true);
			bufdelrange(a, b);
			normreset();
			break;
		}
		E.op = 'd';
		break;
	case 'y':
		if (E.op == 'y') {
			/* yy */
			size_t a, b;
			a = linestart(E.cur);
			b = lineend(E.cur);
			if (b < E.buf.len && E.buf.s[b] == '\n')
				b++;
			yankset(a, b, true);
			setstatus("yanked line");
			normreset();
			break;
		}
		E.op = 'y';
		break;
	case 'c':
		E.op = 'c';
		break;
	case ':':
		E.prevmode = E.mode;
		E.mode = mcmd;
		E.cmdpre = ':';
		sbufsetlen(&E.cmd, 0);
		setstatus("CMD");
		normreset();
		break;
	case 'v':
		vison();
		setstatus("VISUAL");
		normreset();
		break;
	case '/':
		E.prevmode = E.mode;
		E.mode = mcmd;
		E.cmdpre = '/';
		sbufsetlen(&E.cmd, 0);
		setstatus("/");
		normreset();
		break;
	case 'n':
		searchdo(+1);
		normreset();
		break;
	case 'N':
		searchdo(-1);
		normreset();
		break;
	case kesc:
		normreset();
		break;
	case kleft: applymotion('h'); break;
	case kright: applymotion('l'); break;
	case kup: applymotion('k'); break;
	case kdown: applymotion('j'); break;
	case 'h': case 'j': case 'k': case 'l':
	case 'w': case 'b': case 'e':
	case '$':
	case 't':
	case 'f':
	case 'g':
	case 'G':
		applymotion(key);
		break;
	default:
		if (E.op) {
			setstatus("op %c cancelled", E.op);
			normreset();
		}
		break;
	}
}

static void viskey(int key)
{
	size_t a, b;

	if (key >= '0' && key <= '9') {
		if (E.count == 0 && key == '0') {
			applymotion('0');
			return;
		}
		E.count = E.count * 10 + (key - '0');
		return;
	}

	switch (key) {
	case kesc:
	case 'v':
		visoff();
		setstatus("NORMAL");
		normreset();
		break;
	case 'd':
		if (visrange(&a, &b)) {
			yankset(a, b, false);
			bufdelrange(a, b);
		}
		visoff();
		setstatus("NORMAL");
		normreset();
		break;
	case 'y':
		if (visrange(&a, &b)) {
			yankset(a, b, false);
			setstatus("yanked %zu bytes", E.yank.len);
		}
		visoff();
		normreset();
		break;
	case 'c':
		if (visrange(&a, &b)) {
			yankset(a, b, false);
			bufdelrange(a, b);
			enterinsert();
			setstatus("INSERT");
		}
		visoff();
		normreset();
		break;
	case ':':
		E.prevmode = E.mode;
		E.mode = mcmd;
		E.cmdpre = ':';
		sbufsetlen(&E.cmd, 0);
		setstatus("CMD");
		normreset();
		break;
	case '/':
		E.prevmode = E.mode;
		E.mode = mcmd;
		E.cmdpre = '/';
		sbufsetlen(&E.cmd, 0);
		setstatus("/");
		normreset();
		break;
	case 'n':
		searchdo(+1);
		normreset();
		break;
	case 'N':
		searchdo(-1);
		normreset();
		break;
	case kleft: applymotion('h'); break;
	case kright: applymotion('l'); break;
	case kup: applymotion('k'); break;
	case kdown: applymotion('j'); break;
	case 'h': case 'j': case 'k': case 'l':
	case 'w': case 'b': case 'e':
	case '$':
	case 't':
	case 'f':
	case 'g':
	case 'G':
		applymotion(key);
		break;
	default:
		/* ignore */
		break;
	}
}

static void inskey(int key)
{
	bool clamp;

	clamp = true;
	switch (key) {
	case kesc:
		E.mode = mnormal;
		if (E.cur > 0)
			E.cur = utfprev(E.buf.s, E.buf.len, E.cur);
		setstatus("NORMAL");
		break;
	case kenter:
		insnl();
		break;
	case kbs:
	case 8:
		backspace();
		break;
	case kdel:
		delchar();
		break;
	case kleft:
		E.cur = motionh(E.cur);
		break;
	case kright:
		E.cur = motionl(E.cur);
		break;
	case kup:
		E.cur = motionk(E.cur);
		break;
	case kdown:
		E.cur = motionj(E.cur);
		break;
	case '\t':
		insbyte('\t');
		clamp = false;
		break;
	default:
		if (key >= 32 && key <= 255) {
			insbyte(key);
			clamp = false;
		}
		break;
	}
	if (clamp)
		clampcur();
}

/*
 * ':' command line.
 * cmdkey() edits E.cmd; cmdexec() runs a tiny set of ex commands.
 */

static void cmdexec(void)
{
	if (E.cmdpre == '/') {
		searchdo(+1);
		E.mode = E.prevmode;
		if (E.mode == mvisual)
			setstatus("VISUAL");
		else
			setstatus("NORMAL");
		return;
	}

	if (E.cmd.len == 0) {
		E.mode = E.prevmode;
		setstatus(E.mode == mvisual ? "VISUAL" : "NORMAL");
		return;
	}
	if (!strcmp(E.cmd.s, "set nu")) {
		E.shownum = true;
		E.shownumrel = false;
		E.mode = mnormal;
		setstatus("NORMAL");
		return;
	}
	if (!strcmp(E.cmd.s, "set nonu")) {
		E.shownum = false;
		E.shownumrel = false;
		E.mode = mnormal;
		setstatus("NORMAL");
		return;
	}
	if (!strcmp(E.cmd.s, "set rnu")) {
		E.shownum = true;
		E.shownumrel = true;
		E.mode = mnormal;
		setstatus("NORMAL");
		return;
	}
	if (!strcmp(E.cmd.s, "set nornu")) {
		E.shownum = true;
		E.shownumrel = false;
		E.mode = mnormal;
		setstatus("NORMAL");
		return;
	}
	if (E.cmd.s[0] == 's' || (E.cmd.s[0] == '%' && E.cmd.s[1] == 's')) {
		if (E.prevmode == mvisual) {
			size_t a, b;
			if (visrange(&a, &b))
				subcmd(E.cmd.s, a, b, 1);
			visoff();
			E.mode = mnormal;
		} else {
			subcmd(E.cmd.s, 0, 0, 0);
			E.mode = mnormal;
		}
		return;
	}
	if (!strcmp(E.cmd.s, "q")) {
		if (E.dirty) {
			setstatus("no write since last change (:q! to quit)");
			E.mode = mnormal;
			return;
		}
		write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
		exit(0);
	}
	if (!strcmp(E.cmd.s, "q!")) {
		write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
		exit(0);
	}
	if (!strcmp(E.cmd.s, "w")) {
		filesave();
		E.mode = mnormal;
		setstatus("NORMAL");
		return;
	}
	if (!strcmp(E.cmd.s, "wq")) {
		filesave();
		if (!E.dirty) {
			write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
			exit(0);
		}
		E.mode = mnormal;
		setstatus("NORMAL");
		return;
	}

	setstatus("unknown command: %s", E.cmd.s);
	E.mode = E.prevmode;
}

static void cmdkey(int key)
{
	switch (key) {
	case kesc:
		E.mode = E.prevmode;
		setstatus(E.mode == mvisual ? "VISUAL" : "NORMAL");
		break;
	case kenter:
		cmdexec();
		break;
	case kbs:
	case 8:
		if (E.cmd.len)
			sbufsetlen(&E.cmd, E.cmd.len - 1);
		break;
	default:
		if (key >= 32 && key <= 126)
			sbufins(&E.cmd, E.cmd.len, (char[]){(char)key}, 1);
		break;
	}
}

static void processkey(void)
{
	int key;

	key = readkey();
	if (key == knull)
		return;

	if (key == 17) { /* ctrl-q */
		write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
		exit(0);
	}

	switch (E.mode) {
	case mnormal:
		normkey(key);
		break;
	case minsert:
		inskey(key);
		break;
	case mvisual:
		viskey(key);
		break;
	case mcmd:
		cmdkey(key);
		break;
	}
}

int main(int argc, char **argv)
{
	struct sigaction sa;

	rawon();
	setwinsz();

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = onsigwinch;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	if (sigaction(SIGWINCH, &sa, NULL) == -1)
		die("sigaction: %s", strerror(errno));

	E.mode = mnormal;
	E.prevmode = mnormal;
	E.filename = NULL;
	E.dirty = false;
	E.cur = 0;
	E.vmark = 0;
	E.rowoff = 0;
	E.coloff = 0;
	E.count = 0;
	E.op = 0;
	E.status[0] = 0;
	E.shownum = false;
	E.shownumrel = false;
	E.cmdpre = ':';
	E.undo = NULL;
	E.undolen = 0;
	E.undocap = 0;
	E.insgrp = 0;
	sbufsetlen(&E.buf, 0);
	sbufsetlen(&E.yank, 0);
	sbufsetlen(&E.cmd, 0);
	sbufsetlen(&E.search, 0);

	if (argc >= 2) {
		E.filename = strdup(argv[1]);
		if (!E.filename)
			die("out of memory");
		fileopen(E.filename);
	}

	setstatus("NORMAL");

	for (;;) {
		winchtick();
		refresh();
		processkey();
	}

	return 0;
}
