#include "sbuf.h"

#include "lines.h"
#include "wee_util.h"

/*
 * growable byte buffers.
 *
 * sbuf is used for the main text buffer, yank buffer, command line, and undo.
 */

/* sbufgrow ensures b->cap is at least need bytes. */
static void
sbufgrow(struct sbuf *b, size_t need)
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

/* sbufsetlen resizes b and sets its length (always NUL-terminates). */
void
sbufsetlen(struct editor *e, struct sbuf *b, size_t n)
{
	sbufgrow(b, n + 1);
	b->len = n;
	b->s[b->len] = 0;
	if (e && b == &e->buf)
		linesdirty(e);
}

/* sbuffree frees the buffer storage and resets fields. */
void
sbuffree(struct editor *e, struct sbuf *b)
{
	free(b->s);
	b->s = NULL;
	b->len = 0;
	b->cap = 0;
	if (e && b == &e->buf)
		linesdirty(e);
}

/* sbufins inserts n bytes from p into b at offset at. */
void
sbufins(struct editor *e, struct sbuf *b, size_t at, const void *p, size_t n)
{
	if (at > b->len)
		at = b->len;
	sbufgrow(b, b->len + n + 1);
	memmove(b->s + at + n, b->s + at, b->len - at);
	memcpy(b->s + at, p, n);
	b->len += n;
	b->s[b->len] = 0;
	if (e && b == &e->buf)
		linesdirty(e);
}

/* sbufdel deletes n bytes from b starting at offset at. */
void
sbufdel(struct editor *e, struct sbuf *b, size_t at, size_t n)
{
	if (at >= b->len)
		return;
	if (at + n > b->len)
		n = b->len - at;
	memmove(b->s + at, b->s + at + n, b->len - (at + n));
	b->len -= n;
	b->s[b->len] = 0;
	if (e && b == &e->buf)
		linesdirty(e);
}
