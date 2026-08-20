/* wweft: a scriptable cell grid on a Wayland layer surface.
 * main.c joins the parts. It owns no policy. */
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "notices.h"
#include "wweft.h"

#ifndef WWEFT_VERSION
#define WWEFT_VERSION "dev"
#endif

#define DEFAULT_SIZE 16

static int env_int(const char *name, int fallback)
{
	const char *v = getenv(name);
	int n;

	if (!v || !*v)
		return fallback;
	n = atoi(v);
	return n > 0 ? n : fallback;
}

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
	int style;
	const char *chars;

	grid_clear(0);
	script_on_draw();

	/* Last, so the script cannot draw over it. */
	if (script_border(&style, &chars))
		grid_border(style, chars);
}

/* Code 1, the same as Escape: nothing was chosen. */
void app_on_blur(void)
{
	if (script_dismiss())
		loop_quit(1);
}

void app_on_tick(void)
{
	script_on_tick();
	wl_redraw();
}

void app_on_message(const char *line)
{
	if (getenv("WWEFT_DEBUG"))
		fprintf(stderr, "message: %s\n", line);

	script_on_message(line);
	wl_redraw();
}

/* One place opens the font, because the scale can change while running. */
static void open_font(void)
{
	const char *path = NULL;
	int px = env_int("WWEFT_SIZE", DEFAULT_SIZE);

	if (script_font_done())
		script_font(&path, &px);

	font_open(path, px, wl_scale120(), wl_align());
}

void app_rescale(void)
{
	if (getenv("WWEFT_DEBUG"))
		fprintf(stderr, "rescale: scale120=%d\n", wl_scale120());

	font_close();
	open_font();
	wl_rescale();
}

void app_on_change(const char *path)
{
	if (getenv("WWEFT_DEBUG"))
		fprintf(stderr, "change: %s\n", path);

	script_on_change(path);
	wl_redraw();
}

int app_on_key(const char *name)
{
	if (getenv("WWEFT_DEBUG"))
		fprintf(stderr, "key: %s\n", name);

	return script_on_key(name);
}

int main(int argc, char **argv)
{
	int cols = 0, rows = 0;
	int code;

	setlocale(LC_CTYPE, "");   /* wcwidth needs a UTF-8 locale */

	/* Writes one line and exits. No surface. */
	if (argc == 4 && strcmp(argv[1], "--send") == 0)
		return msg_send(argv[2], argv[3]) == 0 ? 0 : 1;

	if (argc < 2) {
		fprintf(stderr, "usage: %s script.wren [args...]\n"
				"       %s --send <channel> <text>\n",
			argv[0], argv[0]);
		return 2;
	}

	if (strcmp(argv[1], "--version") == 0) {
		printf("wweft %s\n"
		       "MIT. It carries Wren, stb_truetype, and the Spleen font.\n"
		       "Run 'wweft --license' for every notice.\n", WWEFT_VERSION);
		return 0;
	}
	if (strcmp(argv[1], "--license") == 0) {
		fputs(wweft_notices, stdout);
		return 0;
	}
	if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
		printf("usage: %s script.wren [args...]\n"
		       "  WWEFT_FONT   path of a TTF file\n"
		       "  WWEFT_SIZE   cell height in pixels, default 16\n"
		       "  WWEFT_DEBUG  write key names to stderr\n"
		       "  WWEFT_DUMP   write each frame to a file, as a PAM image\n"
		       "\n"
		       "\n"
		       "  --send <channel> <text>   one line to a running wweft\n"
		       "  --version, --license, --help\n",
		       argv[0]);
		return 0;
	}

	if (wl_connect() < 0) {
		wl_stop();
		return 1;
	}

	grid_reset_styles();
	script_set_args(argc - 2, argv + 2);   /* Sys.args */

	/* It cannot draw yet: the surface has no size until the configure. */
	if (script_init(argv[1]) < 0) {
		script_close();
		wl_stop();
		return 1;
	}

	open_font();

	script_window_size(&cols, &rows);

	/* A border is outside the script's size: 10 by 10 becomes 12 by 12.
	 * A filled axis keeps its 0 and loses two cells at the configure. */
	if (script_border(NULL, NULL)) {
		grid_set_inset(1);
		if (cols > 0)
			cols += 2;
		if (rows > 0)
			rows += 2;
	}

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
	msg_stop();

	if (getenv("WWEFT_DEBUG"))
		fprintf(stderr, "wren heap at exit: %zu bytes\n", script_bytes());

	script_close();
	wl_stop();
	grid_free();
	font_close();
	return code;
}
