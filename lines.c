#include "lines.h"

#include "utf.h"
#include "wee_util.h"

/*
 * line indexing and cursor mapping.
 *
 * this module maintains a cached table of line-start offsets and provides
 * helpers to map byte offsets to (row,col) and back.
 */

/* isutfcont reports whether c is a utf-8 continuation byte. */
static bool
isutfcont(unsigned char c)
{
	return (c & 0xc0) == 0x80;
}

/* linesdirty marks the line-start cache as needing a rebuild. */
void
linesdirty(struct editor *e)
{
	e->linedirty = true;
}

/* linesgrow ensures the line-start table capacity is at least need entries. */
static void
linesgrow(struct editor *e, int need)
{
	int nc;
	size_t *ns;

	if (e->linecap >= need)
		return;
	nc = e->linecap ? e->linecap : 128;
	while (nc < need)
		nc *= 2;

	ns = realloc(e->linest, (size_t)nc * sizeof(e->linest[0]));
	if (!ns)
		die("out of memory");
	e->linest = ns;
	e->linecap = nc;
}

/* linesbuild rebuilds the line-start table for the whole buffer. */
static void
linesbuild(struct editor *e)
{
	size_t i;
	int n;

	linesgrow(e, 1);
	e->linest[0] = 0;
	n = 1;
	for (i = 0; i < e->buf.len; i++) {
		if (e->buf.s[i] == '\n') {
			linesgrow(e, n + 1);
			e->linest[n++] = i + 1;
		}
	}
	e->linelen = n;
	e->linedirty = false;
}

/* linesensure lazily rebuilds the line-start table if needed. */
static void
linesensure(struct editor *e)
{
	if (e->linelen == 0 || e->linedirty)
		linesbuild(e);
}

/* linestart returns the offset of the start of the line containing at. */
size_t
linestart(struct editor *e, size_t at)
{
	while (at > 0 && e->buf.s[at - 1] != '\n')
		at--;
	return at;
}

/* lineend returns the offset of the end of the line containing at. */
size_t
lineend(struct editor *e, size_t at)
{
	while (at < e->buf.len && e->buf.s[at] != '\n')
		at++;
	return at;
}

/* linecount returns the number of lines in the buffer (>= 1). */
int
linecount(struct editor *e)
{
	linesensure(e);
	return e->linelen;
}

/* off2row maps a byte offset to a 0-based row index. */
int
off2row(struct editor *e, size_t off)
{
	size_t lo, hi;

	linesensure(e);
	if (off > e->buf.len)
		off = e->buf.len;
	if (e->linelen <= 1)
		return 0;

	lo = 0;
	hi = (size_t)e->linelen;
	while (lo + 1 < hi) {
		size_t mid;

		mid = lo + (hi - lo) / 2;
		if (e->linest[mid] <= off)
			lo = mid;
		else
			hi = mid;
	}
	return (int)lo;
}

/* off2col maps a byte offset to a display column (tabs expanded). */
int
off2col(struct editor *e, size_t off)
{
	size_t ls;
	size_t i;
	int col;

	ls = linestart(e, off);
	col = 0;
	i = ls;
	while (i < off && i < e->buf.len && e->buf.s[i] != '\n') {
		unsigned char c;
		size_t j;

		c = (unsigned char)e->buf.s[i];
		if (c == '\t') {
			col += tabstop - (col % tabstop);
			i++;
			continue;
		}
		j = utfnext(e->buf.s, e->buf.len, i);
		if (j <= i)
			j = i + 1;
		col++;
		i = j;
	}
	return col;
}

/* offatcol maps a desired display column to a byte offset within [ls,le]. */
size_t
offatcol(struct editor *e, size_t ls, size_t le, int want)
{
	size_t i;
	int col;

	if (want <= 0)
		return ls;
	col = 0;
	i = ls;
	while (i < le && i < e->buf.len && e->buf.s[i] != '\n') {
		unsigned char c;
		size_t j;
		int n;

		if (col >= want)
			break;
		c = (unsigned char)e->buf.s[i];
		if (c == '\t') {
			n = tabstop - (col % tabstop);
			if (col + n > want)
				break;
			col += n;
			i++;
			continue;
		}
		j = utfnext(e->buf.s, e->buf.len, i);
		if (j <= i)
			j = i + 1;
		col++;
		i = j;
	}
	return i;
}

/* row2off maps a 0-based row index to its starting byte offset. */
size_t
row2off(struct editor *e, int row)
{
	linesensure(e);
	if (row <= 0)
		return 0;
	if (row >= e->linelen)
		return e->buf.len;
	return e->linest[row];
}

/* clampcur keeps E.cur in-range and on a utf-8 lead byte. */
void
clampcur(struct editor *e)
{
	if (e->cur > e->buf.len)
		e->cur = e->buf.len;
	if (e->cur < e->buf.len && isutfcont((unsigned char)e->buf.s[e->cur]))
		e->cur = utfprev(e->buf.s, e->buf.len, e->cur);
}

/* ndigits returns the number of decimal digits in n (>= 1). */
static int
ndigits(int n)
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

/* numw returns the width of the line-number gutter (0 if disabled). */
int
numw(struct editor *e)
{
	if (!e->shownum)
		return 0;
	return ndigits(linecount(e)) + 1; /* digits + space */
}
