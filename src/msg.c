/* Lines from outside: a FIFO that `wweft --send` writes, or a unix socket
 * that a compositor publishes events on. It knows no Wayland and no VM. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "wweft.h"

#define MSG_MAX   4       /* channels one script may listen on */
#define WATCH_MAX 8       /* files one script may watch */
#define LINE_MAX  4096

static struct {
	int fd;
	char buf[LINE_MAX];
	size_t used;
} M[MSG_MAX];

static int count;

/* A watch is on the directory, never on the file: a careful writer writes a
 * temporary file and renames it over the target, which is a new inode, and
 * a watch on the file itself goes deaf after the first update. */
static struct {
	int wd;
	char base[256];          /* the file name inside the directory */
	char full[LINE_MAX];     /* the path the script asked for */
	int changed;
} W[WATCH_MAX];

static int inotify = -1;
static int watches;

/* A name becomes $XDG_RUNTIME_DIR/wweft-<name>. Anything with a slash is
 * taken as it is, and is opened as a unix socket. */
static void resolve(const char *spec, char *out, size_t size)
{
	const char *dir = getenv("XDG_RUNTIME_DIR");

	if (strchr(spec, '/'))
		snprintf(out, size, "%s", spec);
	else
		snprintf(out, size, "%s/wweft-%s", dir && *dir ? dir : "/tmp", spec);
}

static int open_socket(const char *path)
{
	struct sockaddr_un addr = { .sun_family = AF_UNIX };
	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);

	if (fd < 0)
		return -1;

	snprintf(addr.sun_path, sizeof addr.sun_path, "%s", path);
	if (connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static int open_fifo(const char *path)
{
	if (mkfifo(path, 0600) < 0 && errno != EEXIST)
		return -1;

	/* O_RDWR, not O_RDONLY: it keeps a writer inside this process, so
	 * poll() never reports a permanent end of file when a sender exits. */
	return open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
}

int msg_listen(const char *spec)
{
	char path[LINE_MAX];
	int fd;

	if (count >= MSG_MAX)
		return -1;

	resolve(spec, path, sizeof path);
	fd = strchr(spec, '/') ? open_socket(path) : open_fifo(path);
	if (fd < 0) {
		fprintf(stderr, "wweft: cannot listen on %s\n", path);
		return -1;
	}

	M[count].fd = fd;
	M[count].used = 0;
	count++;
	return 0;
}

int msg_count(void)
{
	return count;
}

int msg_fd(int i)
{
	return i >= 0 && i < count ? M[i].fd : -1;
}

/* Read what is there and hand over each whole line. */
void msg_read(int i)
{
	ssize_t n;

	if (i < 0 || i >= count)
		return;

	n = read(M[i].fd, M[i].buf + M[i].used, sizeof M[i].buf - M[i].used - 1);
	if (n <= 0)
		return;

	M[i].used += (size_t)n;
	M[i].buf[M[i].used] = 0;

	for (;;) {
		char *end = memchr(M[i].buf, '\n', M[i].used);
		size_t line_len;

		if (!end) {
			/* A line longer than the buffer is dropped, not kept
			 * forever. */
			if (M[i].used >= sizeof M[i].buf - 1)
				M[i].used = 0;
			return;
		}

		*end = 0;
		app_on_message(M[i].buf);

		line_len = (size_t)(end - M[i].buf) + 1;
		M[i].used -= line_len;
		memmove(M[i].buf, end + 1, M[i].used);
		M[i].buf[M[i].used] = 0;
	}
}

void msg_stop(void)
{
	int i;

	for (i = 0; i < count; i++)
		close(M[i].fd);
	count = 0;

	if (inotify >= 0)
		close(inotify);
	inotify = -1;
	watches = 0;
}

/* `wweft --send NAME TEXT`. It never blocks and never waits: a FIFO with no
 * reader gives ENXIO at once, instead of hanging the compositor bind. */
/* ------------------------------------------------------------- watches */

static void expand_home(const char *in, char *out, size_t size)
{
	const char *home = getenv("HOME");

	if (in[0] == '~' && in[1] == '/' && home)
		snprintf(out, size, "%s%s", home, in + 1);
	else
		snprintf(out, size, "%s", in);
}

int msg_watch(const char *path)
{
	char full[LINE_MAX];
	char dir[LINE_MAX];
	const char *slash;
	int wd;

	if (watches >= WATCH_MAX)
		return -1;

	expand_home(path, full, sizeof full);

	slash = strrchr(full, '/');
	if (slash) {
		size_t n = (size_t)(slash - full);
		if (n == 0)
			n = 1;                    /* a file in / */
		if (n >= sizeof dir)
			n = sizeof dir - 1;
		memcpy(dir, full, n);
		dir[n] = 0;
	} else {
		snprintf(dir, sizeof dir, ".");
	}

	if (inotify < 0) {
		inotify = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
		if (inotify < 0)
			return -1;
	}

	wd = inotify_add_watch(inotify, dir, IN_CLOSE_WRITE | IN_MOVED_TO);
	if (wd < 0) {
		fprintf(stderr, "wweft: cannot watch %s\n", dir);
		return -1;
	}

	W[watches].wd = wd;
	snprintf(W[watches].base, sizeof W[watches].base, "%.255s",
		 slash ? slash + 1 : full);
	snprintf(W[watches].full, sizeof W[watches].full, "%s", full);
	W[watches].changed = 0;
	watches++;
	return 0;
}

int msg_watch_fd(void)
{
	return inotify;
}

/* Read every event that is waiting, then report each path one time. Ten
 * writes between two wakeups are one redraw, not ten. */
void msg_watch_read(void)
{
	char buf[4096] __attribute__ ((aligned(__alignof__(struct inotify_event))));
	ssize_t n;
	int i;

	while ((n = read(inotify, buf, sizeof buf)) > 0) {
		char *p = buf;

		while (p < buf + n) {
			const struct inotify_event *e = (const struct inotify_event *)p;

			for (i = 0; i < watches; i++)
				if (W[i].wd == e->wd && e->len > 0 &&
				    strcmp(e->name, W[i].base) == 0)
					W[i].changed = 1;

			p += sizeof *e + e->len;
		}
	}

	for (i = 0; i < watches; i++) {
		if (!W[i].changed)
			continue;
		W[i].changed = 0;
		app_on_change(W[i].full);
	}
}

int msg_send(const char *name, const char *text)
{
	char path[LINE_MAX];
	int fd;
	int ok;

	resolve(name, path, sizeof path);
	fd = open(path, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0)
		return -1;

	ok = dprintf(fd, "%s\n", text) > 0;
	close(fd);
	return ok ? 0 : -1;
}
