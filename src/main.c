/* wweft: a scriptable cell grid on a Wayland layer surface.
 * Build step 2: the grid, the font, and text. The content is a placeholder
 * until the Wren script arrives in step 4. */
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>

#include "wweft.h"

#define DEFAULT_COLS 46
#define DEFAULT_ROWS 5
#define DEFAULT_SIZE 16

enum { ST_BASE = 0, ST_TITLE, ST_ITEM, ST_SEL };

static void set_styles(void)
{
	grid_set_style(ST_BASE,  0xffcccccc, 0xee151515);
	grid_set_style(ST_TITLE, 0xff7fbfff, 0xee151515);
	grid_set_style(ST_ITEM,  0xffcccccc, 0xee151515);
	grid_set_style(ST_SEL,   0xff151515, 0xff7fbfff);
}

static void demo(void)
{
	static const char *items[] = { "Suspend", "Reboot", "Power off" };
	int x = 2;
	int i;

	grid_clear(ST_BASE);
	grid_text(2, 1, "Session", ST_TITLE);

	for (i = 0; i < 3; i++) {
		char label[32];
		snprintf(label, sizeof label, " %s ", items[i]);
		grid_text(x, 3, label, i == 0 ? ST_SEL : ST_ITEM);
		x += grid_str_w(items[i]) + 3;
	}
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
	int cols = DEFAULT_COLS;
	int rows = DEFAULT_ROWS;
	int size = env_int("WWEFT_SIZE", DEFAULT_SIZE);
	int code;

	setlocale(LC_CTYPE, "");   /* wcwidth needs a UTF-8 locale */

	if (argc == 3) {
		cols = atoi(argv[1]);
		rows = atoi(argv[2]);
	} else if (argc != 1) {
		fprintf(stderr, "usage: %s [cols rows]\n", argv[0]);
		return 2;
	}

	if (cols < 1 || rows < 1) {
		fprintf(stderr, "wweft: bad size\n");
		return 2;
	}

	font_open(NULL, size);
	if (grid_init(cols, rows) < 0) {
		fprintf(stderr, "wweft: out of memory\n");
		return 1;
	}

	set_styles();
	demo();

	if (wl_start(cols * font_cell_w(), rows * font_cell_h()) < 0) {
		wl_stop();
		grid_free();
		font_close();
		return 1;
	}

	code = loop_run();

	wl_stop();
	grid_free();
	font_close();
	return code;
}
