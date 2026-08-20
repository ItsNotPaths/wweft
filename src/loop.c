/* poll() over every file descriptor. Step 1 has one: the Wayland socket. */
#include <errno.h>
#include <poll.h>
#include <stddef.h>
#include <signal.h>

#include "wweft.h"

static volatile sig_atomic_t running = 1;
static int exit_code;

void loop_quit(int code)
{
	running = 0;
	exit_code = code;
}

static void on_signal(int sig)
{
	(void)sig;
	running = 0;
}

static void catch_signals(void)
{
	struct sigaction sa = {0};
	sa.sa_handler = on_signal;   /* no SA_RESTART, so poll() gives EINTR */
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
}

enum { FD_WAYLAND = 0, FD_TIMER, FD_COUNT };

int loop_run(void)
{
	struct pollfd fds[FD_COUNT] = {
		{ .fd = wl_get_fd(),      .events = POLLIN },
		{ .fd = input_timer_fd(), .events = POLLIN },   /* -1 is ignored */
	};

	catch_signals();

	while (running) {
		if (wl_prepare() < 0)
			return 1;

		if (poll(fds, FD_COUNT, -1) < 0) {
			wl_cancel();
			if (errno == EINTR)
				continue;
			return 1;
		}

		if (fds[FD_WAYLAND].revents & POLLIN) {
			if (wl_read() < 0)
				return 1;
		} else {
			wl_cancel();
		}

		if (wl_dispatch() < 0)
			return 1;

		if (fds[FD_TIMER].revents & POLLIN)
			input_timer_fire();

		if (fds[FD_WAYLAND].revents & (POLLERR | POLLHUP))
			return 1;
	}

	return exit_code;
}
