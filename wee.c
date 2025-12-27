#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
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
	mcmd,
};

struct sbuf {
	char *s;
	size_t len;
	size_t cap;
};

static struct termios origterm;

static struct {
	int screenrows;
	int screencols;
	int textrows;

	enum mode mode;
	char *filename;
	bool dirty;

	struct sbuf buf;
	size_t cur; /* byte offset in buf */

	int rowoff; /* top line number (0-based) */
	int coloff; /* left column (0-based) */

	struct sbuf yank;
	bool yankline;

	int count;
	int op; /* 'd', 'y', 'c' or 0 */

	char status[128];
	time_t statustime;

	struct sbuf cmd;
} E;

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

static int readkey(void)
{
	char c;
	ssize_t n;

	for (;;) {
		n = read(STDIN_FILENO, &c, 1);
		if (n == 1)
			break;
		if (n == -1 && errno != EAGAIN)
			die("read: %s", strerror(errno));
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

	ls = linestart(off);
	return (int)(off - ls);
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

static void scroll(void)
{
	int cy, cx;

	cy = off2row(E.cur);
	cx = off2col(E.cur);

	if (cy < E.rowoff)
		E.rowoff = cy;
	if (cy >= E.rowoff + E.textrows)
		E.rowoff = cy - E.textrows + 1;

	if (cx < E.coloff)
		E.coloff = cx;
	if (cx >= E.coloff + E.screencols)
		E.coloff = cx - E.screencols + 1;

	if (E.rowoff < 0)
		E.rowoff = 0;
	if (E.coloff < 0)
		E.coloff = 0;
}

static void drawrows(struct sbuf *ab)
{
	int y;
	size_t off;

	off = row2off(E.rowoff);
	for (y = 0; y < E.textrows; y++) {
		size_t ls, le;
		int len;

		ls = off;
		if (ls > E.buf.len) {
			const char *t = "~";
			sbufins(ab, ab->len, t, 1);
		} else {
			le = lineend(ls);
			len = (int)(le - ls);
			if (E.coloff < len) {
				int n;
				const char *p;

				p = E.buf.s + ls + E.coloff;
				n = len - E.coloff;
				if (n > E.screencols)
					n = E.screencols;
				sbufins(ab, ab->len, p, (size_t)n);
			}
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

	snprintf(left, sizeof(left), " %s%s - %d lines %s ",
		E.filename ? E.filename : "[No Name]",
		E.dirty ? "*" : "",
		lcount,
		E.mode == minsert ? "[INSERT]" : (E.mode == mcmd ? "[CMD]" : ""));
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
		sbufins(ab, ab->len, ":", 1);
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

	scroll();

	sbufins(&ab, ab.len, "\x1b[?25l", 6);
	sbufins(&ab, ab.len, "\x1b[H", 3);

	drawrows(&ab);
	drawstatus(&ab);
	drawmsg(&ab);

	cy = off2row(E.cur) - E.rowoff + 1;
	cx = off2col(E.cur) - E.coloff + 1;
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

static void filenew(void)
{
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

static bool isword(int c)
{
	return isalnum((unsigned char)c) || c == '_';
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
	if ((size_t)col > le - ls)
		col = (int)(le - ls);
	return ls + (size_t)col;
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
	if ((size_t)col > le - ls)
		col = (int)(le - ls);
	return ls + (size_t)col;
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

static size_t motionw(size_t p)
{
	bool inw;

	if (p >= E.buf.len)
		return p;
	inw = isword((unsigned char)E.buf.s[p]);
	while (p < E.buf.len && E.buf.s[p] != '\n') {
		if (inw && !isword((unsigned char)E.buf.s[p]))
			break;
		if (!inw && isword((unsigned char)E.buf.s[p]))
			break;
		p = utfnext(E.buf.s, E.buf.len, p);
	}
	while (p < E.buf.len && E.buf.s[p] != '\n' && !isword((unsigned char)E.buf.s[p]))
		p = utfnext(E.buf.s, E.buf.len, p);
	return p;
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

	sbufdel(&E.buf, a, b - a);
	E.dirty = true;
	E.cur = a;
	clampcur();
}

static void pasteafter(void)
{
	size_t at;

	if (!E.yank.len)
		return;

	if (E.yankline) {
		size_t le;
		le = lineend(E.cur);
		at = (le < E.buf.len && E.buf.s[le] == '\n') ? le + 1 : le;
	} else {
		at = (E.cur < E.buf.len) ? utfnext(E.buf.s, E.buf.len, E.cur) : E.cur;
	}

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

	ch = (char)c;
	sbufins(&E.buf, E.cur, &ch, 1);
	E.cur++;
	E.dirty = true;
}

static void insnl(void)
{
	char c;

	c = '\n';
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
		if (key == '$')
			end = (end < E.buf.len && E.buf.s[end] == '\n') ? end : end;
		else if (end < E.buf.len)
			end = utfnext(E.buf.s, E.buf.len, end);
		linewise = false;
		yankset(start, end, linewise);
		bufdelrange(start, end);
		if (E.op == 'c')
			E.mode = minsert;
	} else if (E.op == 'y') {
		if (end < E.buf.len)
			end = utfnext(E.buf.s, E.buf.len, end);
		yankset(start, end, linewise);
		setstatus("yanked %zu bytes", E.yank.len);
	}

	normreset();
}

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
		E.mode = minsert;
		normreset();
		break;
	case 'a':
		E.cur = motionl(E.cur);
		E.mode = minsert;
		normreset();
		break;
	case 'x':
		n = usecount();
		while (n--)
			delchar();
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
		E.mode = mcmd;
		sbufsetlen(&E.cmd, 0);
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

static void inskey(int key)
{
	bool clamp;

	clamp = true;
	switch (key) {
	case kesc:
		E.mode = mnormal;
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

static void cmdexec(void)
{
	if (E.cmd.len == 0) {
		E.mode = mnormal;
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
		return;
	}
	if (!strcmp(E.cmd.s, "wq")) {
		filesave();
		if (!E.dirty) {
			write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
			exit(0);
		}
		E.mode = mnormal;
		return;
	}

	setstatus("unknown command: %s", E.cmd.s);
	E.mode = mnormal;
}

static void cmdkey(int key)
{
	switch (key) {
	case kesc:
		E.mode = mnormal;
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
	case mcmd:
		cmdkey(key);
		break;
	}
}

int main(int argc, char **argv)
{
	rawon();
	setwinsz();

	E.mode = mnormal;
	E.filename = NULL;
	E.dirty = false;
	E.cur = 0;
	E.rowoff = 0;
	E.coloff = 0;
	E.count = 0;
	E.op = 0;
	E.status[0] = 0;
	sbufsetlen(&E.buf, 0);
	sbufsetlen(&E.yank, 0);
	sbufsetlen(&E.cmd, 0);

	if (argc >= 2) {
		E.filename = strdup(argv[1]);
		if (!E.filename)
			die("out of memory");
		fileopen(E.filename);
	}

	setstatus("NORMAL  :w  :q  i  a  h j k l  w b e  dd yy p  (ctrl-q quits)");

	for (;;) {
		refresh();
		processkey();
	}

	return 0;
}
