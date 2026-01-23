#include "ex.h"

#include "edit.h"
#include "file.h"
#include "lines.h"
#include "sbuf.h"
#include "status.h"
#include "term.h"
#include "utf.h"

/*
 * ex/search.
 *
 * implements ":" commands, "/" search, and a small substitute engine.
 */

/*
 * parsepat parses a vi-ish pattern into a literal byte string plus anchors.
 * parameters:
 *   - s/slen: raw pattern bytes as typed
 *   - out: destination buffer (is overwritten)
 *   - a0: set to 1 if pattern begins with unescaped '^' (bol)
 *   - a1: set to 1 if pattern ends with unescaped '$' (eol)
 */
static void
parsepat(const char *s, size_t slen, struct sbuf *out, int *a0, int *a1)
{
	size_t i;
	int esc;
	bool lastesc;

	sbufsetlen(NULL, out, 0);
	if (a0)
		*a0 = 0;
	if (a1)
		*a1 = 0;

	esc = 0;
	lastesc = false;
	for (i = 0; i < slen; i++) {
		char c;

		c = s[i];
		if (!esc && c == '\\' && i + 1 < slen) {
			esc = 1;
			continue;
		}
		if (esc) {
			lastesc = true;
			esc = 0;
		} else {
			lastesc = false;
		}

		if (out->len == 0 && a0 && !lastesc && c == '^') {
			*a0 = 1;
			continue;
		}
		sbufins(NULL, out, out->len, &c, 1);
	}

	if (a1 && out->len && out->s[out->len - 1] == '$' && !lastesc) {
		*a1 = 1;
		sbufsetlen(NULL, out, out->len - 1);
	}
}

/*
 * runstdout runs cmd via a shell and captures its stdout.
 * returns: 0 on success, -1 on failure.
 */
static int
runstdout(const char *cmd, struct sbuf *out, int *ws)
{
	int pfd[2];
	pid_t pid;
	int st;
	char buf[4096];
	ssize_t n;
	int in, err;

	if (ws)
		*ws = 0;
	sbufsetlen(NULL, out, 0);

	if (pipe(pfd) == -1)
		return -1;

	pid = fork();
	if (pid == -1) {
		close(pfd[0]);
		close(pfd[1]);
		return -1;
	}

	if (pid == 0) {
		in = open("/dev/null", O_RDONLY);
		if (in >= 0) {
			dup2(in, STDIN_FILENO);
			close(in);
		}
		dup2(pfd[1], STDOUT_FILENO);
		close(pfd[0]);
		close(pfd[1]);

		err = open("/dev/null", O_WRONLY);
		if (err >= 0) {
			dup2(err, STDERR_FILENO);
			close(err);
		}

		execl("/bin/bash", "bash", "-c", cmd, (char *)NULL);
		execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
		_exit(127);
	}

	close(pfd[1]);
	for (;;) {
		n = read(pfd[0], buf, sizeof(buf));
		if (n > 0) {
			sbufins(NULL, out, out->len, buf, (size_t)n);
			continue;
		}
		if (n == 0)
			break;
		if (errno == EINTR)
			continue;
		close(pfd[0]);
		(void)waitpid(pid, NULL, 0);
		return -1;
	}
	close(pfd[0]);

	st = 0;
	if (waitpid(pid, &st, 0) == -1)
		return -1;
	if (ws)
		*ws = st;
	return 0;
}

/* findnext searches forward for a literal pat. */
static int
findnext(const char *s, size_t slen, const char *pat, size_t plen, size_t start, size_t *pos)
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

/* findprev searches backward for a literal pat. */
static int
findprev(const char *s, size_t slen, const char *pat, size_t plen, size_t before, size_t *pos)
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

/* prevlinestart returns the start offset of the previous line. */
static size_t
prevlinestart(struct editor *e, size_t ls)
{
	size_t i;

	if (ls == 0)
		return 0;
	i = ls - 1;
	while (i > 0 && e->buf.s[i - 1] != '\n')
		i--;
	return i;
}

/* findanchnext searches forward for an anchored match. */
static int
findanchnext(struct editor *e, const char *pat, size_t plen, int a0, int a1, size_t start, size_t *pos)
{
	size_t ls, le;

	if (start > e->buf.len)
		return 0;
	ls = linestart(e, start);
	if (start != ls) {
		le = lineend(e, start);
		if (le < e->buf.len && e->buf.s[le] == '\n')
			ls = le + 1;
		else
			return 0;
	}

	for (;;) {
		size_t cand;

		if (ls > e->buf.len)
			break;
		le = lineend(e, ls);
		cand = ls;
		if (!a0 && a1) {
			if (le < plen)
				goto next;
			cand = le - plen;
		}
		if (cand < start)
			goto next;
		if (plen == 0) {
			if (a0 && a1) {
				if (ls == le) {
					*pos = ls;
					return 1;
				}
			} else if (a0) {
				*pos = ls;
				return 1;
			} else if (a1) {
				*pos = le;
				return 1;
			}
			goto next;
		}
		if (cand + plen > le)
			goto next;
		if (a0 && cand != ls)
			goto next;
		if (a1 && cand + plen != le)
			goto next;
		if (memcmp(e->buf.s + cand, pat, plen) == 0) {
			*pos = cand;
			return 1;
		}

next:
		if (le < e->buf.len && e->buf.s[le] == '\n') {
			ls = le + 1;
			continue;
		}
		break;
	}
	return 0;
}

/* findanchnextrange searches forward for an anchored match within [rs,re]. */
static int
findanchnextrange(struct editor *e, const char *pat, size_t plen, int a0, int a1, size_t start, size_t rs, size_t re, size_t *pos)
{
	size_t ls, le;

	if (rs > e->buf.len)
		rs = e->buf.len;
	if (re > e->buf.len)
		re = e->buf.len;
	if (re < rs) {
		size_t t = rs;

		rs = re;
		re = t;
	}
	if (start < rs)
		start = rs;
	if (start > re)
		return 0;

	ls = linestart(e, start);
	if (start != ls) {
		le = lineend(e, start);
		if (le < e->buf.len && e->buf.s[le] == '\n')
			ls = le + 1;
		else
			return 0;
	}

	for (;;) {
		size_t cand;

		if (ls > re)
			break;
		le = lineend(e, ls);
		cand = ls;
		if (!a0 && a1) {
			if (le < plen)
				goto next;
			cand = le - plen;
		}
		if (cand < start)
			goto next;
		if (cand < rs || cand + plen > re)
			goto next;
		if (plen == 0) {
			if (a0 && a1) {
				if (ls == le && ls >= rs && ls <= re) {
					*pos = ls;
					return 1;
				}
			} else if (a0) {
				if (ls >= rs && ls <= re) {
					*pos = ls;
					return 1;
				}
			} else if (a1) {
				if (le >= rs && le <= re) {
					*pos = le;
					return 1;
				}
			}
			goto next;
		}
		if (cand + plen > le)
			goto next;
		if (a0 && cand != ls)
			goto next;
		if (a1 && cand + plen != le)
			goto next;
		if (memcmp(e->buf.s + cand, pat, plen) == 0) {
			*pos = cand;
			return 1;
		}

next:
		if (le < e->buf.len && e->buf.s[le] == '\n') {
			ls = le + 1;
			continue;
		}
		break;
	}
	return 0;
}

/* findanchprev searches backward for an anchored match. */
static int
findanchprev(struct editor *e, const char *pat, size_t plen, int a0, int a1, size_t before, size_t *pos)
{
	size_t ls, le;
	size_t cand;

	if (before > e->buf.len)
		before = e->buf.len;
	ls = linestart(e, before);

	for (;;) {
		le = lineend(e, ls);
		cand = ls;
		if (!a0 && a1) {
			if (le < plen)
				goto prev;
			cand = le - plen;
		}
		if (plen == 0) {
			if (a0 && a1) {
				if (ls == le && ls <= before) {
					*pos = ls;
					return 1;
				}
			} else if (a0) {
				if (ls <= before) {
					*pos = ls;
					return 1;
				}
			} else if (a1) {
				if (le <= before) {
					*pos = le;
					return 1;
				}
			}
			goto prev;
		}
		if (cand + plen > le)
			goto prev;
		if (a0 && cand != ls)
			goto prev;
		if (a1 && cand + plen != le)
			goto prev;
		if (cand + plen > before)
			goto prev;
		if (memcmp(e->buf.s + cand, pat, plen) == 0) {
			*pos = cand;
			return 1;
		}

prev:
		if (ls == 0)
			break;
		ls = prevlinestart(e, ls);
	}
	return 0;
}

/* searchdo performs a forward/backward search using the last pattern. */
void
searchdo(struct editor *e, int dir)
{
	size_t pos;
	size_t start;
	struct sbuf pat = {0};
	int a0, a1;

	if (e->cmdpre == '/') {
		if (e->cmd.len) {
			sbufsetlen(NULL, &e->search, 0);
			sbufins(NULL, &e->search, 0, e->cmd.s, e->cmd.len);
		}
	}

	if (e->search.len == 0) {
		setstatus(e, "no previous search");
		return;
	}

	parsepat(e->search.s, e->search.len, &pat, &a0, &a1);

	if (dir >= 0) {
		start = (e->cur < e->buf.len) ? utfnext(e->buf.s, e->buf.len, e->cur) : e->cur;
		if ((a0 || a1) ? !findanchnext(e, pat.s, pat.len, a0, a1, start, &pos)
		              : !findnext(e->buf.s, e->buf.len, pat.s, pat.len, start, &pos)) {
			setstatus(e, "pattern not found");
			sbuffree(NULL, &pat);
			return;
		}
	} else {
		start = e->cur;
		if (start > 0)
			start = utfprev(e->buf.s, e->buf.len, start);
		if ((a0 || a1) ? !findanchprev(e, pat.s, pat.len, a0, a1, start, &pos)
		              : !findprev(e->buf.s, e->buf.len, pat.s, pat.len, start, &pos)) {
			setstatus(e, "pattern not found");
			sbuffree(NULL, &pat);
			return;
		}
	}
	sbuffree(NULL, &pat);

	e->cur = pos;
	clampcur(e);
	setstatus(e, "match");
}

/* skips returns p advanced past ASCII spaces/tabs. */
static const char *
skips(const char *p)
{
	while (*p == ' ' || *p == '\t')
		p++;
	return p;
}

/* addrfindline implements /literal/ address lookup from row startrow. */
static int
addrfindline(struct editor *e, const char *s, size_t n, int startrow)
{
	size_t start;
	size_t pos;
	int row;
	int lcount;

	if (n == 0)
		return -1;
	if (startrow < 1)
		startrow = 1;
	lcount = linecount(e);
	if (startrow > lcount)
		startrow = lcount;

	start = row2off(e, startrow - 1);
	if (findnext(e->buf.s, e->buf.len, s, n, start, &pos)) {
		row = off2row(e, pos) + 1;
		return row;
	}
	if (start > 0 && findnext(e->buf.s, e->buf.len, s, n, 0, &pos)) {
		row = off2row(e, pos) + 1;
		return row;
	}
	return -1;
}

/* parseaddr parses a single ex address and returns 1 on success. */
static int
parseaddr(struct editor *e, const char **pp, int *outrow)
{
	const char *p;
	int base;
	int lcount;

	p = skips(*pp);
	base = -1;
	lcount = linecount(e);

	if (*p == '.') {
		base = off2row(e, e->cur) + 1;
		p++;
	} else if (*p == '$') {
		base = lcount;
		p++;
	} else if (isdigit((unsigned char)*p)) {
		long v;

		v = 0;
		while (isdigit((unsigned char)*p)) {
			v = v * 10 + (*p - '0');
			p++;
			if (v > 1000000)
				break;
		}
		base = (int)v;
	} else if (*p == '/') {
		size_t i;
		struct sbuf lit = {0};
		int found;

		p++;
		for (i = 0; p[i]; i++) {
			char c;

			c = p[i];
			if (c == '\\' && p[i + 1]) {
				i++;
				sbufins(NULL, &lit, lit.len, &p[i], 1);
				continue;
			}
			if (c == '/')
				break;
			sbufins(NULL, &lit, lit.len, &c, 1);
		}
		if (p[i] != '/') {
			sbuffree(NULL, &lit);
			return 0;
		}
		found = addrfindline(e, lit.s, lit.len, off2row(e, e->cur) + 1);
		sbuffree(NULL, &lit);
		if (found < 0)
			return 0;
		base = found;
		p += i + 1;
	}

	if (base < 0)
		return 0;

	for (;;) {
		int sign;
		int n;

		p = skips(p);
		sign = 0;
		if (*p == '+') {
			sign = +1;
			p++;
		} else if (*p == '-') {
			sign = -1;
			p++;
		} else {
			break;
		}
		p = skips(p);
		n = 0;
		if (!isdigit((unsigned char)*p))
			n = 1;
		while (isdigit((unsigned char)*p)) {
			n = n * 10 + (*p - '0');
			p++;
			if (n > 1000000)
				break;
		}
		base += sign * n;
	}

	if (base < 1)
		base = 1;
	if (base > lcount)
		base = lcount;

	*outrow = base;
	*pp = p;
	return 1;
}

/* parsesubex parses an optional range prefix for "s" commands. */
static int
parsesubex(struct editor *e, const char *cmd, const char **sub, int *r0, int *r1)
{
	const char *p;
	int a0, a1;
	int has0, has1;

	p = skips(cmd);
	has0 = 0;
	has1 = 0;
	a0 = 0;
	a1 = 0;

	if (*p == '%') {
		a0 = 1;
		a1 = linecount(e);
		has0 = 1;
		has1 = 1;
		p++;
	} else {
		has0 = parseaddr(e, &p, &a0);
		p = skips(p);
		if (has0 && *p == ',') {
			p++;
			has1 = parseaddr(e, &p, &a1);
			if (!has1)
				return 0;
		} else if (has0) {
			a1 = a0;
			has1 = 1;
		}
	}

	p = skips(p);
	if (*p != 's')
		return 0;

	*sub = p;
	if (has0 && has1) {
		*r0 = a0;
		*r1 = a1;
		return 2;
	}
	return 1;
}

/* subcmd implements :s and :%s on a byte range. */
static void
subcmd(struct editor *e, const char *cmd, size_t rs, size_t re, int hasrange)
{
	int global;
	int a0, a1;
	char delim;
	size_t i;
	struct sbuf raw = {0};
	struct sbuf pat = {0};
	struct sbuf rep = {0};
	size_t rangestart;
	size_t rangeend;
	size_t firsthit;
	int firstset;
	int nsub;

	global = 0;
	a0 = 0;
	a1 = 0;
	firsthit = 0;
	firstset = 0;
	nsub = 0;

	if (cmd[0] != 's') {
		setstatus(e, "unknown command: %s", cmd);
		return;
	}
	cmd++;
	delim = *cmd++;
	if (delim == 0) {
		setstatus(e, "bad substitute");
		return;
	}

	{
		int esc;

		esc = 0;
		for (i = 0; cmd[i]; i++) {
			char c;

			c = cmd[i];
			if (!esc && c == delim)
				break;
			if (!esc && c == '\\' && cmd[i + 1]) {
				esc = 1;
				sbufins(NULL, &raw, raw.len, &c, 1);
				continue;
			}
			esc = 0;
			sbufins(NULL, &raw, raw.len, &c, 1);
		}
		if (cmd[i] != delim) {
			setstatus(e, "bad substitute");
			goto out;
		}
	}
	parsepat(raw.s, raw.len, &pat, &a0, &a1);
	if (pat.len == 0 && !(a0 || a1)) {
		setstatus(e, "empty pattern");
		goto out;
	}
	i++;

	for (; cmd[i]; i++) {
		char c;

		c = cmd[i];
		if (c == '\\' && cmd[i + 1]) {
			i++;
			sbufins(NULL, &rep, rep.len, &cmd[i], 1);
			continue;
		}
		if (c == delim)
			break;
		sbufins(NULL, &rep, rep.len, &c, 1);
	}
	if (cmd[i] == delim)
		i++;
	for (; cmd[i]; i++) {
		if (cmd[i] == 'g')
			global = 1;
	}

	if (!hasrange) {
		rangestart = linestart(e, e->cur);
		rangeend = lineend(e, e->cur);
	} else {
		rangestart = rs;
		rangeend = re;
		if (rangestart > e->buf.len)
			rangestart = e->buf.len;
		if (rangeend > e->buf.len)
			rangeend = e->buf.len;
		if (rangeend < rangestart) {
			size_t t;

			t = rangestart;
			rangestart = rangeend;
			rangeend = t;
		}
	}

	{
		size_t ls;

		ls = linestart(e, rangestart);
		for (;;) {
			size_t le;
			size_t pos;

			if (ls > rangeend)
				break;
			le = lineend(e, ls);
			if (le > rangeend)
				le = rangeend;

			pos = ls;
			for (;;) {
				size_t m;
				size_t next;
				int ok;

				if ((a0 || a1))
					ok = findanchnextrange(e, pat.s, pat.len, a0, a1, pos, ls, le, &m);
				else
					ok = findnext(e->buf.s, le, pat.s, pat.len, pos, &m);
				if (!ok)
					break;
				if (m + pat.len > le)
					break;

				if (!firstset) {
					firsthit = m;
					firstset = 1;
				}
				nsub++;

				bufdelrange(e, m, m + pat.len);
				bufinsert(e, m, rep.s, rep.len);
				next = m + rep.len;
				le = le + rep.len - pat.len;
				rangeend = rangeend + rep.len - pat.len;

				if (!global)
					break;
				if (pat.len == 0 && (a0 || a1))
					break;
				pos = next;
				if (pos > le)
					break;
			}

			le = lineend(e, ls);
			if (le < e->buf.len && e->buf.s[le] == '\n') {
				ls = le + 1;
				continue;
			}
			break;
		}
	}

	if (!firstset) {
		setstatus(e, "no match");
	} else {
		e->cur = firsthit;
		clampcur(e);
		setstatus(e, "%d substitutions", nsub);
	}

out:
	sbuffree(NULL, &raw);
	sbuffree(NULL, &pat);
	sbuffree(NULL, &rep);
}

/* cmdexec runs the current cmdline (':' or '/' prompt). */
void
cmdexec(struct editor *e)
{
	if (e->cmdpre == '/') {
		searchdo(e, +1);
		e->mode = e->prevmode;
		if (e->mode == mvisual)
			setstatus(e, "VISUAL");
		else
			setstatus(e, "NORMAL");
		return;
	}

	if (e->cmd.len == 0) {
		e->mode = e->prevmode;
		setstatus(e, e->mode == mvisual ? "VISUAL" : "NORMAL");
		return;
	}
	if (!strcmp(e->cmd.s, "set nu")) {
		e->shownum = true;
		e->shownumrel = false;
		e->mode = mnormal;
		setstatus(e, "NORMAL");
		return;
	}
	if (!strcmp(e->cmd.s, "set nonu")) {
		e->shownum = false;
		e->shownumrel = false;
		e->mode = mnormal;
		setstatus(e, "NORMAL");
		return;
	}
	if (!strcmp(e->cmd.s, "set rnu")) {
		e->shownum = true;
		e->shownumrel = true;
		e->mode = mnormal;
		setstatus(e, "NORMAL");
		return;
	}
	if (!strcmp(e->cmd.s, "set nornu")) {
		e->shownum = true;
		e->shownumrel = false;
		e->mode = mnormal;
		setstatus(e, "NORMAL");
		return;
	}
	{
		const char *sub;
		int r0, r1;
		int kind;

		sub = NULL;
		r0 = 0;
		r1 = 0;
		kind = parsesubex(e, e->cmd.s, &sub, &r0, &r1);
		if (kind) {
			if (kind == 2) {
				size_t a, b;

				a = row2off(e, r0 - 1);
				b = lineend(e, row2off(e, r1 - 1));
				subcmd(e, sub, a, b, 1);
				if (e->prevmode == mvisual)
					visoff(e);
				e->mode = mnormal;
				return;
			}
			if (e->prevmode == mvisual) {
				size_t a, b;
				int sa, sb;

				if (visrange(e, &a, &b)) {
					sa = off2row(e, linestart(e, a)) + 1;
					sb = off2row(e, linestart(e, b)) + 1;
					a = row2off(e, sa - 1);
					b = lineend(e, row2off(e, sb - 1));
					subcmd(e, sub, a, b, 1);
				}
				visoff(e);
				e->mode = mnormal;
				return;
			}
			subcmd(e, sub, 0, 0, 0);
			e->mode = mnormal;
			return;
		}
	}
	if (!strncmp(e->cmd.s, "run", 3) && (e->cmd.s[3] == 0 || isspace((unsigned char)e->cmd.s[3]))) {
		const char *p;
		struct sbuf out = {0};
		size_t at;
		size_t nbytes;
		int st;

		p = e->cmd.s + 3;
		while (*p == ' ' || *p == '\t')
			p++;
		if (*p == 0) {
			setstatus(e, "usage: :run <script>");
			e->mode = e->prevmode;
			return;
		}
		if (runstdout(p, &out, &st) == -1) {
			setstatus(e, "run failed");
			e->mode = e->prevmode;
			sbuffree(NULL, &out);
			return;
		}
		if (out.len == 0) {
			setstatus(e, "run: no output");
			e->mode = e->prevmode;
			sbuffree(NULL, &out);
			return;
		}

		at = (e->cur < e->buf.len) ? utfnext(e->buf.s, e->buf.len, e->cur) : e->cur;
		bufinsert(e, at, out.s, out.len);
		nbytes = out.len;
		sbuffree(NULL, &out);
		if (e->prevmode == mvisual)
			visoff(e);
		e->mode = mnormal;
		setstatus(e, "run: %zu bytes", nbytes);
		return;
	}
	if (!strcmp(e->cmd.s, "q")) {
		if (e->dirty) {
			setstatus(e, "no write since last change (:q! to quit)");
			e->mode = mnormal;
			return;
		}
		write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
		exit(0);
	}
	if (!strcmp(e->cmd.s, "q!")) {
		write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
		exit(0);
	}
	if (!strcmp(e->cmd.s, "w")) {
		filesave(e);
		e->mode = mnormal;
		setstatus(e, "NORMAL");
		return;
	}
	if (!strcmp(e->cmd.s, "wq")) {
		filesave(e);
		if (!e->dirty) {
			write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
			exit(0);
		}
		e->mode = mnormal;
		setstatus(e, "NORMAL");
		return;
	}

	setstatus(e, "unknown command: %s", e->cmd.s);
	e->mode = e->prevmode;
}
