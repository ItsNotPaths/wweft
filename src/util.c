/* Small helpers with no owner of their own. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>

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
