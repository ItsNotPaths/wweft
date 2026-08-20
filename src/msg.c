/* Lines from outside: a FIFO that `wweft --send` writes, or a unix socket
 * that a compositor publishes events on. It knows no Wayland and no VM. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "wweft.h"

#define MSG_MAX  4        /* channels one script may listen on */
#define LINE_MAX 4096

static struct {
	int fd;
	char buf[LINE_MAX];
	size_t used;
} M[MSG_MAX];

static int count;

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
}

/* `wweft --send NAME TEXT`. It never blocks and never waits: a FIFO with no
 * reader gives ENXIO at once, instead of hanging the compositor bind. */
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
