#include "edit.h"

#include "lines.h"
#include "sbuf.h"
#include "status.h"
#include "term.h"
#include "undo.h"
#include "utf.h"

/*
 * editing + motions.
 *
 * implements primitive buffer mutations, yank/paste, visual selection helpers,
 * and a tiny vi-ish motion/operator engine.
 */

/* enterinsert switches to INSERT mode and starts a new insert group. */
void
enterinsert(struct editor *e)
{
	e->mode = minsert;
	e->insgrp++;
}

/* vison starts visual selection at the current cursor. */
void
vison(struct editor *e)
{
	e->vmark = e->cur;
	e->mode = mvisual;
}

/* visoff exits visual mode back to normal. */
void
visoff(struct editor *e)
{
	e->mode = mnormal;
}

/* viswant reports whether visual selection is active (including cmd sub-mode). */
int
viswant(struct editor *e)
{
	return e->mode == mvisual || (e->mode == mcmd && e->prevmode == mvisual);
}

/* visrange computes the selected byte range as [a,b). */
int
visrange(struct editor *e, size_t *a, size_t *b)
{
	size_t lo, hi;

	if (!viswant(e))
		return 0;
	lo = e->vmark < e->cur ? e->vmark : e->cur;
	hi = e->vmark < e->cur ? e->cur : e->vmark;
	if (hi < e->buf.len)
		hi = utfnext(e->buf.s, e->buf.len, hi);
	if (lo > e->buf.len)
		lo = e->buf.len;
	if (hi > e->buf.len)
		hi = e->buf.len;
	*a = lo;
	*b = hi;
	return 1;
}

/* isword reports whether c is a word character for motions. */
static bool
isword(int c)
{
	return isalnum((unsigned char)c) || c == '_';
}

/* cclass classifies a byte for word motion grouping. */
static int
cclass(int c)
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

/* motionh moves left by one codepoint. */
size_t
motionh(struct editor *e, size_t p)
{
	return utfprev(e->buf.s, e->buf.len, p);
}

/* motionl moves right by one codepoint. */
size_t
motionl(struct editor *e, size_t p)
{
	return utfnext(e->buf.s, e->buf.len, p);
}

/* motionbol moves to beginning of line. */
static size_t
motionbol(struct editor *e, size_t p)
{
	return linestart(e, p);
}

/* motioneol moves to end of line. */
static size_t
motioneol(struct editor *e, size_t p)
{
	size_t le;

	le = lineend(e, p);
	if (le > 0 && le == e->buf.len)
		return le;
	return le;
}

/* motionj moves down one screen line, preserving column. */
size_t
motionj(struct editor *e, size_t p)
{
	int row, col;
	size_t np;
	size_t ls, le;

	row = off2row(e, p);
	col = off2col(e, p);
	np = row2off(e, row + 1);
	ls = np;
	le = lineend(e, ls);
	return offatcol(e, ls, le, col);
}

/* motionk moves up one screen line, preserving column. */
size_t
motionk(struct editor *e, size_t p)
{
	int row, col;
	size_t np;
	size_t ls, le;

	row = off2row(e, p);
	col = off2col(e, p);
	np = row2off(e, row - 1);
	ls = np;
	le = lineend(e, ls);
	return offatcol(e, ls, le, col);
}

/* motiongg moves to the start of the buffer. */
static size_t
motiongg(struct editor *e, size_t p)
{
	(void)e;
	(void)p;
	return 0;
}

/* motioncapg moves to the start of the last line. */
static size_t
motioncapg(struct editor *e, size_t p)
{
	(void)p;
	return row2off(e, linecount(e) - 1);
}

/* motiont searches for ch on the line and stops before it. */
static size_t
motiont(struct editor *e, size_t p, int ch, int n)
{
	size_t scan, ls, le;
	int k;

	if (p >= e->buf.len)
		return p;

	scan = p;
	ls = linestart(e, scan);
	le = lineend(e, scan);
	for (k = 0; k < n; k++) {
		size_t i;
		size_t start;
		size_t found;

		ls = linestart(e, scan);
		le = lineend(e, scan);

		start = utfnext(e->buf.s, e->buf.len, scan);
		if (start > le)
			return p;

		found = le;
		for (i = start; i < le; i++) {
			if ((unsigned char)e->buf.s[i] == (unsigned char)ch) {
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
	return utfprev(e->buf.s, e->buf.len, scan);
}

/* motionf searches for ch on the line and lands on it. */
static size_t
motionf(struct editor *e, size_t p, int ch, int n)
{
	size_t scan, le;
	int k;

	if (p >= e->buf.len)
		return p;

	scan = p;
	le = lineend(e, scan);
	for (k = 0; k < n; k++) {
		size_t i;
		size_t start;
		size_t found;

		le = lineend(e, scan);
		start = utfnext(e->buf.s, e->buf.len, scan);
		if (start > le)
			return p;

		found = le;
		for (i = start; i < le; i++) {
			if ((unsigned char)e->buf.s[i] == (unsigned char)ch) {
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

/* motionw implements vi-like word motion (w). */
static size_t
motionw(struct editor *e, size_t p)
{
	int c, t;

	if (p >= e->buf.len)
		return p;

	c = (unsigned char)e->buf.s[p];
	t = cclass(c);

	if (t == 0) {
		while (p < e->buf.len) {
			c = (unsigned char)e->buf.s[p];
			if (cclass(c) != 0)
				break;
			p = utfnext(e->buf.s, e->buf.len, p);
		}
		return p;
	}

	while (p < e->buf.len) {
		c = (unsigned char)e->buf.s[p];
		if (c == '\n')
			break;
		if (cclass(c) != t)
			break;
		p = utfnext(e->buf.s, e->buf.len, p);
	}
	while (p < e->buf.len) {
		c = (unsigned char)e->buf.s[p];
		if (cclass(c) != 0)
			break;
		p = utfnext(e->buf.s, e->buf.len, p);
	}
	return p;
}

/* motionb implements vi-like backward word motion (b). */
static size_t
motionb(struct editor *e, size_t p)
{
	if (p == 0)
		return 0;
	p = utfprev(e->buf.s, e->buf.len, p);
	while (p > 0 && e->buf.s[p] != '\n' && !isword((unsigned char)e->buf.s[p]))
		p = utfprev(e->buf.s, e->buf.len, p);
	while (p > 0 && e->buf.s[p] != '\n' && isword((unsigned char)e->buf.s[p])) {
		size_t pp;

		pp = utfprev(e->buf.s, e->buf.len, p);
		if (!isword((unsigned char)e->buf.s[pp]))
			break;
		p = pp;
	}
	return p;
}

/* motione implements vi-like end-of-word motion (e). */
static size_t
motione(struct editor *e, size_t p)
{
	if (p >= e->buf.len)
		return p;
	p = motionw(e, p);
	if (p >= e->buf.len)
		return p;
	while (p < e->buf.len && e->buf.s[p] != '\n' && isword((unsigned char)e->buf.s[p]))
		p = utfnext(e->buf.s, e->buf.len, p);
	return utfprev(e->buf.s, e->buf.len, p);
}

/* pairfor maps a delimiter to its opening/closing pair. */
static int
pairfor(int c, int *open, int *close)
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

/* findinnerpair locates the inner [a,b) range for a surrounding pair. */
static int
findinnerpair(struct editor *e, int open, int close, size_t *a, size_t *b)
{
	size_t i, ls, le;
	ssize_t oi, ci;
	int depth;

	oi = -1;
	ci = -1;

	if (e->buf.len == 0)
		return 0;

	if (open == close) {
		ls = linestart(e, e->cur);
		le = lineend(e, e->cur);
		if (e->cur > le)
			return 0;

		for (i = e->cur; i > ls; ) {
			i = utfprev(e->buf.s, e->buf.len, i);
			if ((unsigned char)e->buf.s[i] == (unsigned char)open) {
				oi = (ssize_t)i;
				break;
			}
		}
		for (i = e->cur; i < le; ) {
			if ((unsigned char)e->buf.s[i] == (unsigned char)close) {
				ci = (ssize_t)i;
				break;
			}
			i = utfnext(e->buf.s, e->buf.len, i);
		}
		if (oi < 0 || ci < 0 || (size_t)oi >= (size_t)ci)
			return 0;
		*a = (size_t)oi + 1;
		*b = (size_t)ci;
		return 1;
	}

	depth = 0;
	for (i = e->cur; i > 0; ) {
		i = utfprev(e->buf.s, e->buf.len, i);
		if ((unsigned char)e->buf.s[i] == (unsigned char)close) {
			depth++;
			continue;
		}
		if ((unsigned char)e->buf.s[i] == (unsigned char)open) {
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
	for (i = (size_t)oi + 1; i < e->buf.len; ) {
		if ((unsigned char)e->buf.s[i] == (unsigned char)open) {
			depth++;
			i = utfnext(e->buf.s, e->buf.len, i);
			continue;
		}
		if ((unsigned char)e->buf.s[i] == (unsigned char)close) {
			if (depth == 0) {
				ci = (ssize_t)i;
				break;
			}
			depth--;
			i = utfnext(e->buf.s, e->buf.len, i);
			continue;
		}
		i = utfnext(e->buf.s, e->buf.len, i);
	}
	if (ci < 0)
		return 0;

	*a = (size_t)oi + 1;
	*b = (size_t)ci;
	return 1;
}

/* applytextobjinner applies a pending op to the inner text object i?. */
void
applytextobjinner(struct editor *e, int ch)
{
	int open, close;
	size_t a, b;

	if (!pairfor(ch, &open, &close)) {
		setstatus(e, "unknown textobj %c", (char)ch);
		normreset(e);
		return;
	}
	if (!findinnerpair(e, open, close, &a, &b)) {
		setstatus(e, "no match for %c", (char)ch);
		normreset(e);
		return;
	}

	if (e->op == 'd' || e->op == 'c') {
		yankset(e, a, b, false);
		bufdelrange(e, a, b);
		if (e->op == 'c')
			enterinsert(e);
	} else if (e->op == 'y') {
		yankset(e, a, b, false);
		setstatus(e, "yanked %zu bytes", e->yank.len);
	}
	normreset(e);
}

/* yankset copies [a,b) into the yank buffer (optionally linewise). */
void
yankset(struct editor *e, size_t a, size_t b, bool linewise)
{
	size_t n;

	if (b < a) {
		size_t t = a;

		a = b;
		b = t;
	}
	if (a > e->buf.len)
		a = e->buf.len;
	if (b > e->buf.len)
		b = e->buf.len;

	n = b - a;
	sbufsetlen(NULL, &e->yank, 0);
	if (n)
		sbufins(NULL, &e->yank, 0, e->buf.s + a, n);
	e->yankline = linewise;
}

/* bufdelrange deletes bytes in [a,b) from the main buffer and records undo. */
void
bufdelrange(struct editor *e, size_t a, size_t b)
{
	size_t cur;
	size_t n;

	if (b < a) {
		size_t t = a;

		a = b;
		b = t;
	}
	if (a > e->buf.len)
		a = e->buf.len;
	if (b > e->buf.len)
		b = e->buf.len;
	if (b == a)
		return;

	cur = e->cur;
	n = b - a;
	if (a < e->buf.len)
		undopushdel(e, a, e->buf.s + a, n, cur);

	sbufdel(e, &e->buf, a, n);
	e->dirty = true;
	e->cur = a;
	clampcur(e);
}

/* bufinsert inserts n bytes at into the main buffer and records undo. */
void
bufinsert(struct editor *e, size_t at, const void *p, size_t n)
{
	size_t cur;

	if (n == 0)
		return;
	if (at > e->buf.len)
		at = e->buf.len;
	cur = e->cur;
	undopushins(e, at, p, n, cur, false);
	sbufins(e, &e->buf, at, p, n);
	e->dirty = true;
}

/* pasteafter inserts the yank buffer after the cursor (p). */
void
pasteafter(struct editor *e)
{
	size_t at;
	size_t cur;

	if (e->yank.len == 0)
		return;

	if (e->yankline) {
		size_t le;

		le = lineend(e, e->cur);
		at = (le < e->buf.len && e->buf.s[le] == '\n') ? le + 1 : le;
	} else {
		at = (e->cur < e->buf.len) ? utfnext(e->buf.s, e->buf.len, e->cur) : e->cur;
	}

	cur = e->cur;
	undopushins(e, at, e->yank.s, e->yank.len, cur, false);
	sbufins(e, &e->buf, at, e->yank.s, e->yank.len);
	e->dirty = true;
	e->cur = at;
	clampcur(e);
}

/* delchar deletes the byte/codepoint at the cursor (x / DEL). */
void
delchar(struct editor *e)
{
	if (e->cur >= e->buf.len)
		return;
	bufdelrange(e, e->cur, utfnext(e->buf.s, e->buf.len, e->cur));
}

/* openbelow inserts a newline after the current line and enters insert mode. */
void
openbelow(struct editor *e)
{
	size_t le;
	size_t at;
	char nl;
	size_t cur;

	le = lineend(e, e->cur);
	at = (le < e->buf.len && e->buf.s[le] == '\n') ? le + 1 : le;
	nl = '\n';
	cur = e->cur;
	undopushins(e, at, &nl, 1, cur, false);
	sbufins(e, &e->buf, at, &nl, 1);
	e->dirty = true;
	e->cur = at;
	enterinsert(e);
}

/* openabove inserts a newline before the current line and enters insert mode. */
void
openabove(struct editor *e)
{
	size_t ls;
	char nl;
	size_t cur;

	ls = linestart(e, e->cur);
	nl = '\n';
	cur = e->cur;
	undopushins(e, ls, &nl, 1, cur, false);
	sbufins(e, &e->buf, ls, &nl, 1);
	e->dirty = true;
	e->cur = ls;
	enterinsert(e);
}

/* backspace deletes the previous codepoint in insert mode. */
void
backspace(struct editor *e)
{
	size_t p;

	if (e->cur == 0)
		return;
	p = utfprev(e->buf.s, e->buf.len, e->cur);
	bufdelrange(e, p, e->cur);
}

/* insbyte inserts a single byte at the cursor (insert mode). */
void
insbyte(struct editor *e, int c)
{
	char ch;
	size_t cur;

	ch = (char)c;
	cur = e->cur;
	undopushins(e, e->cur, &ch, 1, cur, true);
	sbufins(e, &e->buf, e->cur, &ch, 1);
	e->cur++;
	e->dirty = true;
}

/* insnl inserts a newline at the cursor (insert mode). */
void
insnl(struct editor *e)
{
	char c;
	size_t cur;

	c = '\n';
	cur = e->cur;
	undopushins(e, e->cur, &c, 1, cur, true);
	sbufins(e, &e->buf, e->cur, &c, 1);
	e->cur++;
	e->dirty = true;
}

/* normreset clears pending count/operator state. */
void
normreset(struct editor *e)
{
	e->count = 0;
	e->op = 0;
}

/* usecount returns the active count (defaults to 1). */
int
usecount(struct editor *e)
{
	return e->count ? e->count : 1;
}

/*
 * applymotion resolves a motion (with count), then applies any pending op.
 */
void
applymotion(struct editor *e, int key)
{
	int n;
	size_t start, end;
	bool linewise;

	start = e->cur;
	end = e->cur;
	linewise = false;

	n = usecount(e);

	if (key == 'g') {
		int k2 = readkey();

		if (k2 == 'g') {
			end = motiongg(e, e->cur);
		} else {
			setstatus(e, "unknown g%c", (char)k2);
			normreset(e);
			return;
		}
	} else if (key == 'G') {
		if (e->count)
			end = row2off(e, e->count - 1);
		else
			end = motioncapg(e, e->cur);
	} else if (key == 't') {
		int ch;

		ch = readkey();
		end = motiont(e, end, ch, n);
	} else if (key == 'f') {
		int ch;

		ch = readkey();
		if (ch == kesc || ch == knull) {
			setstatus(e, "find cancelled");
			normreset(e);
			return;
		}
		end = motionf(e, end, ch, n);
	} else {
		while (n--) {
			switch (key) {
			case 'h': end = motionh(e, end); break;
			case 'l': end = motionl(e, end); break;
			case 'j': end = motionj(e, end); break;
			case 'k': end = motionk(e, end); break;
			case ')': {
				int k;

				for (k = 0; k < e->textrows; k++)
					end = motionj(e, end);
				break;
			}
			case '(':{
				int k;

				for (k = 0; k < e->textrows; k++)
					end = motionk(e, end);
				break;
			}
			case '0': end = motionbol(e, end); break;
			case '$': end = motioneol(e, end); break;
			case 'w': end = motionw(e, end); break;
			case 'b': end = motionb(e, end); break;
			case 'e': end = motione(e, end); break;
			default:
				setstatus(e, "unknown motion %c", (char)key);
				normreset(e);
				return;
			}
		}
	}

	if (!e->op) {
		e->cur = end;
		clampcur(e);
		normreset(e);
		return;
	}

	if (e->op == 'd' || e->op == 'c') {
		if (key == 'e' && end < e->buf.len)
			end = utfnext(e->buf.s, e->buf.len, end);
		if (key == 'f' && end < e->buf.len)
			end = utfnext(e->buf.s, e->buf.len, end);
		linewise = false;
		yankset(e, start, end, linewise);
		bufdelrange(e, start, end);
		if (e->op == 'c')
			enterinsert(e);
	} else if (e->op == 'y') {
		if (key == 'e' && end < e->buf.len)
			end = utfnext(e->buf.s, e->buf.len, end);
		if (key == 'f' && end < e->buf.len)
			end = utfnext(e->buf.s, e->buf.len, end);
		yankset(e, start, end, linewise);
		setstatus(e, "yanked %zu bytes", e->yank.len);
	}

	normreset(e);
}
