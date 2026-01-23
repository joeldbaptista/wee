#include "term.h"

#include "wee_util.h"

/*
 * terminal i/o.
 *
 * handles raw mode, key decoding, and window size management.
 */

static struct termios origterm;

static void rawoff(void);

/* set on SIGWINCH; checked in input loop to force a redraw. */
static volatile sig_atomic_t winch;

static void
termonsig(int sig)
{
	rawoff();
	_exit(128 + sig);
}

/* onsigwinch sets the resize flag (SIGWINCH handler). */
void
onsigwinch(int sig)
{
	(void)sig;
	winch = 1;
}

static int
utf8len(unsigned char c)
{
	if ((c & 0x80) == 0x00)
		return 1;
	if ((c & 0xe0) == 0xc0)
		return 2;
	if ((c & 0xf0) == 0xe0)
		return 3;
	if ((c & 0xf8) == 0xf0)
		return 4;
	return 1;
}

static int
readbyte_block(unsigned char *out)
{
	ssize_t n;

	for (;;) {
		if (winch)
			return 0;
		n = read(STDIN_FILENO, out, 1);
		if (n == 1)
			return 1;
		if (n == 0)
			continue;
		if (n == -1) {
			if (errno == EAGAIN)
				continue;
			if (errno == EINTR)
				return 0;
			die("read: %s", strerror(errno));
		}
	}
}

static int
readbyte_timeout(unsigned char *out)
{
	ssize_t n;

	for (;;) {
		if (winch)
			return 0;
		n = read(STDIN_FILENO, out, 1);
		if (n == 1)
			return 1;
		if (n == 0)
			return 0;
		if (n == -1) {
			if (errno == EAGAIN)
				continue;
			if (errno == EINTR)
				return 0;
			die("read: %s", strerror(errno));
		}
	}
}

/* getwinsz reads the current terminal size via ioctl. */
static int
getwinsz(int *rows, int *cols)
{
	struct winsize ws;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
		return -1;
	*cols = ws.ws_col;
	*rows = ws.ws_row;
	return 0;
}

/* rawoff restores cooked mode and resets cursor shape/visibility. */
static void
rawoff(void)
{
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &origterm);
	write(STDOUT_FILENO,
	    "\x1b[2 q\x1b[?25h",
	    sizeof("\x1b[2 q\x1b[?25h") - 1);
}

/* rawon enables raw mode and registers atexit cleanup. */
void
rawon(void)
{
	struct termios t;
	struct sigaction sa;

	if (tcgetattr(STDIN_FILENO, &origterm) == -1)
		die("tcgetattr: %s", strerror(errno));
	atexit(rawoff);

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = termonsig;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	(void)sigaction(SIGTERM, &sa, NULL);
	(void)sigaction(SIGHUP, &sa, NULL);
	(void)sigaction(SIGQUIT, &sa, NULL);
	(void)sigaction(SIGINT, &sa, NULL);

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
 * readkeyex reads one keypress and returns raw bytes + decoded key.
 * returns key=knull on timeout/resize so the main loop can redraw.
 */
struct key
readkeyex(void)
{
	struct key k;
	unsigned char a, b, c;
	int need;

	memset(&k, 0, sizeof(k));
	k.key = knull;

	if (!readbyte_block(&c))
		return k;
	k.b[k.n++] = c;

	if (c == '\x1b') {
		/* distinguish a lone ESC from an escape sequence */
		if (!readbyte_timeout(&a)) {
			k.key = kesc;
			return k;
		}
		k.b[k.n++] = a;
		if (!readbyte_timeout(&b)) {
			k.key = kesc;
			return k;
		}
		k.b[k.n++] = b;

		if (a == '[') {
			if (b >= '0' && b <= '9') {
				unsigned char t;
				if (!readbyte_timeout(&t)) {
					k.key = kesc;
					return k;
				}
				if (k.n < TERM_KEYMAX)
					k.b[k.n++] = t;
				if (t == '~') {
					switch (b) {
					case '1': k.key = khome; return k;
					case '3': k.key = kdel; return k;
					case '4': k.key = kend; return k;
					case '5': k.key = kpgup; return k;
					case '6': k.key = kpgdn; return k;
					case '7': k.key = khome; return k;
					case '8': k.key = kend; return k;
					}
				}
			} else {
				switch (b) {
				case 'A': k.key = kup; return k;
				case 'B': k.key = kdown; return k;
				case 'C': k.key = kright; return k;
				case 'D': k.key = kleft; return k;
				case 'H': k.key = khome; return k;
				case 'F': k.key = kend; return k;
				}
			}
		}
		k.key = kesc;
		return k;
	}

	need = utf8len(c);
	while (k.n < need && k.n < TERM_KEYMAX) {
		unsigned char x;
		if (!readbyte_timeout(&x))
			break;
		k.b[k.n++] = x;
	}

	if (need == 1 && (c == 0x7f || c == 0x08))
		k.key = kbs;
	else
		k.key = c;

	return k;
}

/*
 * readkey reads one decoded key code.
 * kept for compatibility with older callers.
 */
int
readkey(void)
{
	struct key k;

	k = readkeyex();
	return k.key;
}

/* setwinsz queries terminal size and updates E.screenrows/screencols/textrows. */
void
setwinsz(struct editor *e)
{
	if (getwinsz(&e->screenrows, &e->screencols) == -1)
		die("getwinsz");
	e->textrows = e->screenrows - 2;
	if (e->textrows < 1)
		e->textrows = 1;
}

/* winchtick applies a pending resize at a safe point. */
void
winchtick(struct editor *e)
{
	if (!winch)
		return;
	winch = 0;
	setwinsz(e);
}
