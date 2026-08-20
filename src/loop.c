/* poll() over every file descriptor: Wayland, key repeat, the script tick,
 * file watches, and the message channels. */
#include <errno.h>
#include <poll.h>
#include <stddef.h>
#include <signal.h>
#include <stdint.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

#include "wweft.h"

static volatile sig_atomic_t running = 1;
static int exit_code;
static int tick_fd = -1;

/* A deadline, not a timer: it costs no descriptor, and any new call to
 * loop_lifetime starts the countdown again. */
static struct timespec deadline;
static int has_deadline;

void loop_lifetime(int ms)
{
	if (ms <= 0) {
		has_deadline = 0;
		return;
	}

	clock_gettime(CLOCK_MONOTONIC, &deadline);
	deadline.tv_sec += ms / 1000;
	deadline.tv_nsec += (long)(ms % 1000) * 1000000L;
	if (deadline.tv_nsec >= 1000000000L) {
		deadline.tv_sec++;
		deadline.tv_nsec -= 1000000000L;
	}
	has_deadline = 1;
}

/* Milliseconds left, 0 when the time is up, -1 when there is no deadline. */
static int time_left(void)
{
	struct timespec now;
	long ms;

	if (!has_deadline)
		return -1;

	clock_gettime(CLOCK_MONOTONIC, &now);
	ms = (long)(deadline.tv_sec - now.tv_sec) * 1000 +
	     (deadline.tv_nsec - now.tv_nsec) / 1000000L;

	return ms > 0 ? (int)ms : 0;
}

/* A whole number of seconds starts on the next second boundary, so a clock
 * changes when the minute does and not up to a second late. */
void loop_every(int ms)
{
	struct itimerspec ts = {0};
	struct timespec now;

	if (tick_fd < 0)
		tick_fd = timerfd_create(CLOCK_REALTIME, TFD_CLOEXEC | TFD_NONBLOCK);
	if (tick_fd < 0 || ms <= 0) {
		if (tick_fd >= 0)
			timerfd_settime(tick_fd, 0, &ts, NULL);
		return;
	}

	ts.it_interval.tv_sec = ms / 1000;
	ts.it_interval.tv_nsec = (long)(ms % 1000) * 1000000L;

	if (ms % 1000 == 0 && clock_gettime(CLOCK_REALTIME, &now) == 0) {
		ts.it_value.tv_sec = now.tv_sec + 1;
		ts.it_value.tv_nsec = 0;
		timerfd_settime(tick_fd, TFD_TIMER_ABSTIME, &ts, NULL);
		return;
	}

	ts.it_value = ts.it_interval;
	timerfd_settime(tick_fd, 0, &ts, NULL);
}

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

/* wayland, key repeat, the tick, the watches, then one for each channel */
enum { FD_WAYLAND = 0, FD_REPEAT, FD_TICK, FD_WATCH, FD_FIXED };

int loop_run(void)
{
	struct pollfd fds[FD_FIXED + 4];
	int channels;
	int i;

	catch_signals();

	while (running) {
		int n = FD_FIXED;

		fds[FD_WAYLAND] = (struct pollfd){ .fd = wl_get_fd(), .events = POLLIN };
		fds[FD_REPEAT]  = (struct pollfd){ .fd = input_timer_fd(), .events = POLLIN };
		fds[FD_TICK]    = (struct pollfd){ .fd = tick_fd, .events = POLLIN };
		fds[FD_WATCH]   = (struct pollfd){ .fd = msg_watch_fd(), .events = POLLIN };

		channels = msg_count();
		for (i = 0; i < channels; i++)
			fds[n++] = (struct pollfd){ .fd = msg_fd(i), .events = POLLIN };

		if (wl_prepare() < 0)
			return 1;

		if (poll(fds, (nfds_t)n, time_left()) < 0) {
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

		if (fds[FD_REPEAT].revents & POLLIN)
			input_timer_fire();

		if (fds[FD_TICK].revents & POLLIN) {
			uint64_t ticks;
			if (read(tick_fd, &ticks, sizeof ticks) == sizeof ticks)
				app_on_tick();
		}

		if (fds[FD_WATCH].revents & POLLIN)
			msg_watch_read();

		for (i = 0; i < channels; i++)
			if (fds[FD_FIXED + i].revents & POLLIN)
				msg_read(i);

		if (fds[FD_WAYLAND].revents & (POLLERR | POLLHUP))
			return 1;

		if (has_deadline && time_left() == 0)
			loop_quit(0);
	}

	return exit_code;
}
