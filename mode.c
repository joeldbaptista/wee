#include "mode.h"

#include "edit.h"
#include "ex.h"
#include "lines.h"
#include "sbuf.h"
#include "status.h"
#include "term.h"
#include "undo.h"
#include "utf.h"

/*
 * mode state machine.
 *
 * parses keys and dispatches to NORMAL/INSERT/VISUAL/CMD handlers.
 */

/* cmdkey edits E.cmd and executes it on enter. */
void
cmdkey(struct editor *e, struct key k)
{
	switch (k.key) {
	case kesc:
		e->mode = e->prevmode;
		setstatus(e, e->mode == mvisual ? "VISUAL" : "NORMAL");
		break;
	case kenter:
		cmdexec(e);
		break;
	case kbs:
	case 8:
		if (e->cmd.len)
			sbufsetlen(NULL, &e->cmd, e->cmd.len - 1);
		break;
	default:
		if (k.key >= 32 && k.key <= 255 && k.n > 0)
			sbufins(NULL, &e->cmd, e->cmd.len, (char *)k.b, (size_t)k.n);
		break;
	}
}

/* normkey parses one key in NORMAL mode (counts, ops, motions, commands). */
void
normkey(struct editor *e, int key)
{
	int n;

	if (key >= '0' && key <= '9') {
		if (e->count == 0 && key == '0') {
			applymotion(e, '0');
			return;
		}
		e->count = e->count * 10 + (key - '0');
		return;
	}

	switch (key) {
	case 'i':
		if (e->op) {
			int ch;

			ch = readkey();
			applytextobjinner(e, ch);
			break;
		}
		enterinsert(e);
		setstatus(e, "INSERT");
		normreset(e);
		break;
	case 'a':
		e->cur = motionl(e, e->cur);
		enterinsert(e);
		setstatus(e, "INSERT");
		normreset(e);
		break;
	case 'A':
		e->cur = lineend(e, e->cur);
		enterinsert(e);
		setstatus(e, "INSERT");
		normreset(e);
		break;
	case 'o':
		openbelow(e);
		setstatus(e, "INSERT");
		normreset(e);
		break;
	case 'O':
		openabove(e);
		setstatus(e, "INSERT");
		normreset(e);
		break;
	case 'C':
		e->op = 'c';
		e->count = 0;
		applymotion(e, '$');
		setstatus(e, "INSERT");
		break;
	case 'x':
		n = usecount(e);
		while (n--)
			delchar(e);
		normreset(e);
		break;
	case 'u':
		undodo(e);
		normreset(e);
		break;
	case 'p':
		pasteafter(e);
		normreset(e);
		break;
	case 'd':
		if (e->op == 'd') {
			size_t a, b;

			a = linestart(e, e->cur);
			b = lineend(e, e->cur);
			if (b < e->buf.len && e->buf.s[b] == '\n')
				b++;
			yankset(e, a, b, true);
			bufdelrange(e, a, b);
			normreset(e);
			break;
		}
		e->op = 'd';
		break;
	case 'y':
		if (e->op == 'y') {
			size_t a, b;

			a = linestart(e, e->cur);
			b = lineend(e, e->cur);
			if (b < e->buf.len && e->buf.s[b] == '\n')
				b++;
			yankset(e, a, b, true);
			setstatus(e, "yanked line");
			normreset(e);
			break;
		}
		e->op = 'y';
		break;
	case 'c':
		e->op = 'c';
		break;
	case ':':
		e->prevmode = e->mode;
		e->mode = mcmd;
		e->cmdpre = ':';
		sbufsetlen(NULL, &e->cmd, 0);
		setstatus(e, "CMD");
		normreset(e);
		break;
	case 'v':
		vison(e);
		setstatus(e, "VISUAL");
		normreset(e);
		break;
	case '/':
		e->prevmode = e->mode;
		e->mode = mcmd;
		e->cmdpre = '/';
		sbufsetlen(NULL, &e->cmd, 0);
		setstatus(e, "/");
		normreset(e);
		break;
	case 'n':
		searchdo(e, +1);
		normreset(e);
		break;
	case 'N':
		searchdo(e, -1);
		normreset(e);
		break;
	case kesc:
		normreset(e);
		break;
	case kleft: applymotion(e, 'h'); break;
	case kright: applymotion(e, 'l'); break;
	case kup: applymotion(e, 'k'); break;
	case kdown: applymotion(e, 'j'); break;
	case 'h': case 'j': case 'k': case 'l':
	case '(': case ')':
	case 'w': case 'b': case 'e':
	case '$':
	case 't':
	case 'f':
	case 'g':
	case 'G':
		applymotion(e, key);
		break;
	default:
		if (e->op) {
			setstatus(e, "op %c cancelled", e->op);
			normreset(e);
		}
		break;
	}
}

/* viskey parses one key in VISUAL mode. */
void
viskey(struct editor *e, int key)
{
	size_t a, b;

	if (key >= '0' && key <= '9') {
		if (e->count == 0 && key == '0') {
			applymotion(e, '0');
			return;
		}
		e->count = e->count * 10 + (key - '0');
		return;
	}

	switch (key) {
	case kesc:
	case 'v':
		visoff(e);
		setstatus(e, "NORMAL");
		normreset(e);
		break;
	case 'd':
		if (visrange(e, &a, &b)) {
			yankset(e, a, b, false);
			bufdelrange(e, a, b);
		}
		visoff(e);
		setstatus(e, "NORMAL");
		normreset(e);
		break;
	case 'y':
		if (visrange(e, &a, &b)) {
			yankset(e, a, b, false);
			setstatus(e, "yanked %zu bytes", e->yank.len);
		}
		visoff(e);
		normreset(e);
		break;
	case 'c':
		if (visrange(e, &a, &b)) {
			yankset(e, a, b, false);
			bufdelrange(e, a, b);
			enterinsert(e);
			setstatus(e, "INSERT");
		}
		visoff(e);
		normreset(e);
		break;
	case ':':
		e->prevmode = e->mode;
		e->mode = mcmd;
		e->cmdpre = ':';
		sbufsetlen(NULL, &e->cmd, 0);
		setstatus(e, "CMD");
		normreset(e);
		break;
	case '/':
		e->prevmode = e->mode;
		e->mode = mcmd;
		e->cmdpre = '/';
		sbufsetlen(NULL, &e->cmd, 0);
		setstatus(e, "/");
		normreset(e);
		break;
	case 'n':
		searchdo(e, +1);
		normreset(e);
		break;
	case 'N':
		searchdo(e, -1);
		normreset(e);
		break;
	case kleft: applymotion(e, 'h'); break;
	case kright: applymotion(e, 'l'); break;
	case kup: applymotion(e, 'k'); break;
	case kdown: applymotion(e, 'j'); break;
	case 'h': case 'j': case 'k': case 'l':
	case 'w': case 'b': case 'e':
	case '$':
	case 't':
	case 'f':
	case 'g':
	case 'G':
		applymotion(e, key);
		break;
	default:
		break;
	}
}

/* inskey parses one key in INSERT mode. */
void
inskey(struct editor *e, struct key k)
{
	bool clamp;

	clamp = true;
	switch (k.key) {
	case kesc:
		e->mode = mnormal;
		if (e->cur > 0 && e->buf.s[e->cur - 1] != '\n')
			e->cur = utfprev(e->buf.s, e->buf.len, e->cur);
		setstatus(e, "NORMAL");
		break;
	case kenter:
		insnl(e);
		break;
	case kbs:
	case 8:
		backspace(e);
		break;
	case kdel:
		delchar(e);
		break;
	case kleft:
		e->cur = motionh(e, e->cur);
		break;
	case kright:
		e->cur = motionl(e, e->cur);
		break;
	case kup:
		e->cur = motionk(e, e->cur);
		break;
	case kdown:
		e->cur = motionj(e, e->cur);
		break;
	case '\t':
		insbyte(e, '\t');
		clamp = false;
		break;
	default:
		if (k.key >= 32 && k.key <= 255 && k.n > 0) {
			size_t cur;

			cur = e->cur;
			undopushins(e, e->cur, k.b, (size_t)k.n, cur, true);
			sbufins(e, &e->buf, e->cur, (char *)k.b, (size_t)k.n);
			e->cur += (size_t)k.n;
			e->dirty = true;
			clamp = false;
		}
		break;
	}
	if (clamp)
		clampcur(e);
}

/* processkey reads a key and dispatches based on the current mode. */
void
processkey(struct editor *e)
{
	struct key k;
	int key;

	k = readkeyex();
	key = k.key;
	if (key == knull)
		return;

	if (key == 17) {
		write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
		exit(0);
	}

	switch (e->mode) {
	case mnormal:
		normkey(e, key);
		break;
	case minsert:
		inskey(e, k);
		break;
	case mvisual:
		viskey(e, key);
		break;
	case mcmd:
		cmdkey(e, k);
		break;
	}
}
