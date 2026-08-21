/* Small helpers with no owner of their own. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "wweft.h"

/* Put ~ back to the home directory. The result is in out. */
void expand_home(const char *in, char *out, size_t size)
{
	const char *home = getenv("HOME");

	if (in[0] == '~' && in[1] == '/' && home)
		snprintf(out, size, "%s%s", home, in + 1);
	else
		snprintf(out, size, "%s", in);
}

int wweft_debug(void)
{
	static int on = -1;

	if (on < 0)
		on = getenv("WWEFT_DEBUG") != NULL;
	return on;
}

/* A moment ms from now, on the monotonic clock. */
void deadline_set(struct timespec *at, int ms)
{
	clock_gettime(CLOCK_MONOTONIC, at);
	at->tv_sec += ms / 1000;
	at->tv_nsec += (long)(ms % 1000) * 1000000L;

	if (at->tv_nsec >= 1000000000L) {
		at->tv_sec++;
		at->tv_nsec -= 1000000000L;
	}
}

int deadline_left(const struct timespec *at)
{
	struct timespec now;
	long ms;

	clock_gettime(CLOCK_MONOTONIC, &now);
	ms = (long)(at->tv_sec - now.tv_sec) * 1000 +
	     (at->tv_nsec - now.tv_nsec) / 1000000L;

	return ms > 0 ? (int)ms : 0;
}
