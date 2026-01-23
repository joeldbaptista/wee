#include "render.h"

#include "edit.h"
#include "lines.h"
#include "sbuf.h"
#include "status.h"
#include "utf.h"

/*
 * rendering pipeline.
 *
 * scroll() maintains rowoff/coloff so E.cur stays visible.
 * refresh() redraws the full screen each keypress.
 */

/* scroll updates viewport offsets to keep the cursor visible. */
static void
scroll(struct editor *e)
{
	int cy, cx;
	int w, textcols;

	cy = off2row(e, e->cur);
	cx = off2col(e, e->cur);
	w = numw(e);
	textcols = e->screencols - w;
	if (textcols < 1)
		textcols = 1;

	if (cy < e->rowoff)
		e->rowoff = cy;
	if (cy >= e->rowoff + e->textrows)
		e->rowoff = cy - e->textrows + 1;

	if (cx < e->coloff)
		e->coloff = cx;
	if (cx >= e->coloff + textcols)
		e->coloff = cx - textcols + 1;

	if (e->rowoff < 0)
		e->rowoff = 0;
	if (e->coloff < 0)
		e->coloff = 0;
}

/* drawrows draws the visible buffer rows into the append buffer. */
static void
drawrows(struct editor *e, struct sbuf *ab)
{
	int y;
	size_t off;
	int w, digits;
	int lineno;
	int lcount;
	int curline;

	off = row2off(e, e->rowoff);
	w = numw(e);
	digits = w ? (w - 1) : 0;
	lcount = linecount(e);
	curline = off2row(e, e->cur) + 1;
	for (y = 0; y < e->textrows; y++) {
		size_t ls, le;
		int cols;
		int col;
		size_t i;
		int inv;
		size_t sa, sb;
		int hasvis;

		ls = off;
		lineno = e->rowoff + y + 1;
		cols = e->screencols - w;
		if (cols < 1)
			cols = 1;
		if (lineno > lcount) {
			if (w) {
				int n;

				sbufins(NULL, ab, ab->len, "~", 1);
				n = w - 1;
				while (n-- > 0)
					sbufins(NULL, ab, ab->len, " ", 1);
			} else {
				sbufins(NULL, ab, ab->len, "~", 1);
			}
		} else {
			le = lineend(e, ls);
			hasvis = visrange(e, &sa, &sb);
			if (w) {
				char nb[32];
				int n;
				int shown;

				shown = lineno;
				if (e->shownumrel && lineno != curline)
					shown = lineno > curline ? (lineno - curline) : (curline - lineno);
				n = snprintf(nb, sizeof(nb), "%*d ", digits, shown);
				if (n > 0)
					sbufins(NULL, ab, ab->len, nb, (size_t)n);
			}
			col = 0;
			inv = 0;
			i = ls;
			while (i < le && i < e->buf.len && e->buf.s[i] != '\n') {
				unsigned char c;
				size_t j;
				int k;
				int n;
				int wantinv;

				if (col >= e->coloff + cols)
					break;
				c = (unsigned char)e->buf.s[i];
				wantinv = hasvis && i >= sa && i < sb;
				if (wantinv != inv) {
					if (wantinv)
						sbufins(NULL, ab, ab->len, "\x1b[7m", 4);
					else
						sbufins(NULL, ab, ab->len, "\x1b[m", 3);
					inv = wantinv;
				}
				if (c == '\t') {
					n = tabstop - (col % tabstop);
					for (k = 0; k < n; k++) {
						if (col >= e->coloff && col < e->coloff + cols)
							sbufins(NULL, ab, ab->len, " ", 1);
						col++;
						if (col >= e->coloff + cols)
							break;
					}
					i++;
					continue;
				}
				j = utfnext(e->buf.s, e->buf.len, i);
				if (j <= i)
					j = i + 1;
				if (col >= e->coloff && col < e->coloff + cols)
					sbufins(NULL, ab, ab->len, e->buf.s + i, j - i);
				col++;
				i = j;
			}
			if (inv)
				sbufins(NULL, ab, ab->len, "\x1b[m", 3);
			off = (le < e->buf.len && e->buf.s[le] == '\n') ? le + 1 : le;
		}
		sbufins(NULL, ab, ab->len, "\x1b[K", 3);
		sbufins(NULL, ab, ab->len, "\r\n", 2);
	}
}

/* drawstatus draws the inverted status bar. */
static void
drawstatus(struct editor *e, struct sbuf *ab)
{
	char left[128], right[128];
	int llen, rlen;
	int lcount, row, col;

	row = off2row(e, e->cur) + 1;
	col = off2col(e, e->cur) + 1;
	lcount = linecount(e);

	snprintf(left, sizeof(left), " %s%s - %d lines [%s] ",
		e->filename ? e->filename : "[No Name]",
		e->dirty ? "*" : "",
		lcount,
		modestr(e));
	snprintf(right, sizeof(right), " %d,%d ", row, col);

	llen = (int)strlen(left);
	rlen = (int)strlen(right);

	sbufins(NULL, ab, ab->len, "\x1b[7m", 4);
	if (llen > e->screencols)
		llen = e->screencols;
	sbufins(NULL, ab, ab->len, left, (size_t)llen);
	while (llen < e->screencols) {
		if (e->screencols - llen == rlen) {
			sbufins(NULL, ab, ab->len, right, (size_t)rlen);
			llen += rlen;
			break;
		}
		sbufins(NULL, ab, ab->len, " ", 1);
		llen++;
	}
	sbufins(NULL, ab, ab->len, "\x1b[m", 3);
	sbufins(NULL, ab, ab->len, "\r\n", 2);
}

/* drawmsg draws the command line (in CMD) or transient status message. */
static void
drawmsg(struct editor *e, struct sbuf *ab)
{
	if (e->mode == mcmd) {
		char p;

		p = e->cmdpre ? e->cmdpre : ':';
		sbufins(NULL, ab, ab->len, &p, 1);
		if (e->cmd.len)
			sbufins(NULL, ab, ab->len, e->cmd.s, e->cmd.len);
		sbufins(NULL, ab, ab->len, "\x1b[K", 3);
		return;
	}

	if (e->status[0] && time(NULL) - e->statustime < 5) {
		size_t n;

		n = strlen(e->status);
		if ((int)n > e->screencols)
			n = (size_t)e->screencols;
		sbufins(NULL, ab, ab->len, e->status, n);
	}

	sbufins(NULL, ab, ab->len, "\x1b[K", 3);
}

/* refresh redraws the full screen and positions the cursor. */
void
refresh(struct editor *e)
{
	struct sbuf ab = {0};
	char buf[32];
	int cy, cx;
	int w;

	scroll(e);
	if (e->mode == minsert)
		sbufins(NULL, &ab, ab.len, "\x1b[6 q", 5);
	else
		sbufins(NULL, &ab, ab.len, "\x1b[2 q", 5);

	sbufins(NULL, &ab, ab.len, "\x1b[?25l", 6);
	sbufins(NULL, &ab, ab.len, "\x1b[H", 3);

	drawrows(e, &ab);
	drawstatus(e, &ab);
	drawmsg(e, &ab);

	cy = off2row(e, e->cur) - e->rowoff + 1;
	w = numw(e);
	cx = off2col(e, e->cur) - e->coloff + 1 + w;
	if (cy < 1)
		cy = 1;
	if (cy > e->textrows)
		cy = e->textrows;
	if (cx < 1)
		cx = 1;
	if (cx > e->screencols)
		cx = e->screencols;

	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", cy, cx);
	sbufins(NULL, &ab, ab.len, buf, strlen(buf));

	sbufins(NULL, &ab, ab.len, "\x1b[?25h", 6);
	write(STDOUT_FILENO, ab.s, ab.len);
	sbuffree(NULL, &ab);
}
