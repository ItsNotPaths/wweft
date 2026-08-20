/* wweft: a scriptable cell grid on a Wayland layer surface.
 * main.c joins the parts. It owns no policy. */
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>

#include "wweft.h"

#define DEFAULT_SIZE 16

void app_resize(int cols, int rows)
{
	if (cols == grid_cols() && rows == grid_rows())
		return;

	if (grid_init(cols, rows) < 0) {
		fprintf(stderr, "wweft: out of memory\n");
		loop_quit(1);
	}
}

void app_paint(void)
{
	grid_clear(0);
	script_on_draw();
}

/* Focus went to another surface. A popup closes. Exit code 1, the same as
 * Escape, because nothing was chosen. */
void app_on_blur(void)
{
	if (script_dismiss())
		loop_quit(1);
}

int app_on_key(const char *name)
{
	if (getenv("WWEFT_DEBUG"))
		fprintf(stderr, "key: %s\n", name);

	return script_on_key(name);
}

static int env_int(const char *name, int fallback)
{
	const char *v = getenv(name);
	int n;

	if (!v || !*v)
		return fallback;
	n = atoi(v);
	return n > 0 ? n : fallback;
}

int main(int argc, char **argv)
{
	int cols = 0, rows = 0;
	int code;

	setlocale(LC_CTYPE, "");   /* wcwidth needs a UTF-8 locale */

	if (argc != 2) {
		fprintf(stderr, "usage: %s script.wren\n", argv[0]);
		return 2;
	}

	if (wl_connect() < 0) {
		wl_stop();
		return 1;
	}

	grid_reset_styles();

	/* The script sets the font, the geometry, and the styles. It cannot
	 * draw yet, because the surface has no size until the compositor
	 * answers. */
	if (script_init(argv[1]) < 0) {
		script_close();
		wl_stop();
		return 1;
	}

	if (!script_font_done())
		font_open(NULL, env_int("WWEFT_SIZE", DEFAULT_SIZE), wl_scale());

	script_window_size(&cols, &rows);
	if (grid_init(cols > 0 ? cols : 1, rows > 0 ? rows : 1) < 0) {
		fprintf(stderr, "wweft: out of memory\n");
		script_close();
		wl_stop();
		return 1;
	}

	if (wl_open(cols, rows) < 0) {
		script_close();
		wl_stop();
		grid_free();
		font_close();
		return 1;
	}

	code = loop_run();

	if (getenv("WWEFT_DEBUG"))
		fprintf(stderr, "wren heap at exit: %zu bytes\n", script_bytes());

	script_close();
	wl_stop();
	grid_free();
	font_close();
	return code;
}
