#include "edit.h"
#include "file.h"
#include "mode.h"
#include "render.h"
#include "sbuf.h"
#include "status.h"
#include "term.h"
#include "wee.h"
#include "wee_util.h"

/*
 * wee main.
 *
 * wires together the editor modules and runs the main refresh/input loop.
 */

struct editor e;
struct sigaction sa;

void
setssigaction(struct sigaction *sa)
{
	memset(sa, 0, sizeof(*sa));
	sa->sa_handler = onsigwinch;
	sigemptyset(&sa->sa_mask);
	sa->sa_flags = 0;
	if (sigaction(SIGWINCH, sa, NULL) == -1)
		die("sigaction: %s", strerror(errno));
}

void
initeditor(struct editor *e)
{
    e->mode = mnormal;
	e->prevmode = mnormal;
	e->filename = NULL;
	e->dirty = false;
	e->cur = 0;
	e->vmark = 0;
	e->rowoff = 0;
	e->coloff = 0;
	e->count = 0;
	e->op = 0;
	e->status[0] = 0;
	e->shownum = false;
	e->shownumrel = false;
	e->cmdpre = ':';
	e->linest = NULL;
	e->linelen = 0;
	e->linecap = 0;
	e->linedirty = true;
	e->undo = NULL;
	e->undolen = 0;
	e->undocap = 0;
	e->insgrp = 0;

	sbufsetlen(e, &e->buf, 0);
	sbufsetlen(e, &e->yank, 0);
	sbufsetlen(e, &e->cmd, 0);
	sbufsetlen(e, &e->search, 0);

	setstatus(e, "NORMAL");
}

int
main(int argc, char **argv)
{
	rawon();
	setwinsz(&e);
    setssigaction(&sa);
    initeditor(&e);

	if (argc >= 2) {
		e.filename = strdup(argv[1]);
		if (!e.filename)
			die("out of memory");
		fileopen(&e, e.filename);
	}

	for (;;) {
		winchtick(&e);
		refresh(&e);
		processkey(&e);
	}

	return 0;
}
