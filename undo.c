#include "undo.h"

#include "lines.h"
#include "sbuf.h"
#include "status.h"
#include "wee_util.h"

/*
 * undo stack.
 *
 * stores inserts/deletes so edits can be reverted with u.
 */

/* suppress undo recording while applying an undo. */
static bool undomute;

/* undogrow ensures the undo stack can hold need entries. */
static void
undogrow(struct editor *e, int need)
{
	int nc;
	struct undo *nu;

	if (e->undocap >= need)
		return;
	nc = e->undocap ? e->undocap : 64;
	while (nc < need)
		nc *= 2;
	nu = realloc(e->undo, (size_t)nc * sizeof(e->undo[0]));
	if (!nu)
		die("out of memory");
	e->undo = nu;
	e->undocap = nc;
}

/* undoclear frees undo history and resets the stack. */
void
undoclear(struct editor *e)
{
	int i;

	for (i = 0; i < e->undolen; i++)
		sbuffree(NULL, &e->undo[i].text);
	free(e->undo);
	e->undo = NULL;
	e->undolen = 0;
	e->undocap = 0;
}

/* undopushins records an insertion for undo (optionally merged). */
void
undopushins(struct editor *e, size_t at, const void *p, size_t n, size_t cur, bool merge)
{
	struct undo *u;

	if (undomute || n == 0)
		return;

	if (merge && e->undolen > 0) {
		u = &e->undo[e->undolen - 1];
		if (u->kind == 'i' && u->grp == e->insgrp && u->at + u->text.len == at) {
			sbufins(NULL, &u->text, u->text.len, p, n);
			return;
		}
	}

	undogrow(e, e->undolen + 1);
	u = &e->undo[e->undolen++];
	memset(u, 0, sizeof(*u));
	u->kind = 'i';
	u->at = at;
	u->cur = cur;
	u->grp = e->insgrp;
	sbufsetlen(NULL, &u->text, 0);
	sbufins(NULL, &u->text, 0, p, n);
}

/* undopushdel records a deletion for undo. */
void
undopushdel(struct editor *e, size_t at, const void *p, size_t n, size_t cur)
{
	struct undo *u;

	if (undomute || n == 0)
		return;

	undogrow(e, e->undolen + 1);
	u = &e->undo[e->undolen++];
	memset(u, 0, sizeof(*u));
	u->kind = 'd';
	u->at = at;
	u->cur = cur;
	u->grp = 0;
	sbufsetlen(NULL, &u->text, 0);
	sbufins(NULL, &u->text, 0, p, n);
}

/* undodo applies the last undo entry. */
void
undodo(struct editor *e)
{
	struct undo u;

	if (e->undolen == 0) {
		setstatus(e, "nothing to undo");
		return;
	}

	u = e->undo[--e->undolen];
	undomute = true;
	if (u.kind == 'i') {
		if (u.at <= e->buf.len)
			sbufdel(e, &e->buf, u.at, u.text.len);
		e->cur = u.cur;
	} else if (u.kind == 'd') {
		if (u.at <= e->buf.len)
			sbufins(e, &e->buf, u.at, u.text.s, u.text.len);
		e->cur = u.cur;
	}
	undomute = false;

	e->dirty = true;
	clampcur(e);
	sbuffree(NULL, &u.text);
	setstatus(e, "undone");
}
