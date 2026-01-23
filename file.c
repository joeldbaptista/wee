#include "file.h"

#include "sbuf.h"
#include "status.h"
#include "undo.h"
#include "wee_util.h"

/*
 * file i/o.
 *
 * load and save the main buffer.
 */

/* filenew resets state for a new, empty buffer. */
void
filenew(struct editor *e)
{
	undoclear(e);
	sbufsetlen(e, &e->buf, 0);
	e->cur = 0;
	e->dirty = false;
	e->rowoff = 0;
	e->coloff = 0;
}

/* fileopen loads path into the buffer (or creates an empty new file). */
void
fileopen(struct editor *e, const char *path)
{
	int fd;
	struct stat st;
	size_t n;

	fd = open(path, O_RDONLY);
	if (fd == -1) {
		filenew(e);
		setstatus(e, "new file");
		return;
	}
	if (fstat(fd, &st) == -1)
		die("fstat: %s", strerror(errno));
	if (st.st_size < 0)
		die("bad file size");

	n = (size_t)st.st_size;
	sbufsetlen(e, &e->buf, n);
	if (n && read(fd, e->buf.s, n) != (ssize_t)n)
		die("read file: %s", strerror(errno));
	close(fd);

	e->cur = 0;
	e->dirty = false;
	e->rowoff = 0;
	e->coloff = 0;
	undoclear(e);
}

/* filesave writes the current buffer to E.filename (atomic via .tmp). */
void
filesave(struct editor *e)
{
	int fd;
	ssize_t n;
	char *tmp;

	if (!e->filename) {
		setstatus(e, "no filename");
		return;
	}

	tmp = malloc(strlen(e->filename) + 5);
	if (!tmp)
		die("out of memory");
	sprintf(tmp, "%s.tmp", e->filename);

	fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd == -1) {
		free(tmp);
		setstatus(e, "write failed: %s", strerror(errno));
		return;
	}

	n = write(fd, e->buf.s, e->buf.len);
	if (n == -1 || (size_t)n != e->buf.len) {
		close(fd);
		unlink(tmp);
		free(tmp);
		setstatus(e, "write failed: %s", strerror(errno));
		return;
	}

	if (fsync(fd) == -1) {
		close(fd);
		unlink(tmp);
		free(tmp);
		setstatus(e, "fsync failed: %s", strerror(errno));
		return;
	}
	close(fd);

	if (rename(tmp, e->filename) == -1) {
		unlink(tmp);
		free(tmp);
		setstatus(e, "rename failed: %s", strerror(errno));
		return;
	}
	free(tmp);

	e->dirty = false;
	setstatus(e, "%zu bytes written", e->buf.len);
}
