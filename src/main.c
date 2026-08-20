/* wweft: a scriptable cell grid on a Wayland layer surface.
 * Build step 3: keys have names. The content and the key logic are a
 * placeholder until the Wren script arrives in step 4. */
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wweft.h"

#define DEFAULT_COLS 46
#define DEFAULT_ROWS 5
#define DEFAULT_SIZE 16

enum { ST_BASE = 0, ST_TITLE, ST_ITEM, ST_SEL };

static const char *items[] = { "Suspend", "Reboot", "Power off" };
static int selection;
static char last_key[96] = "-";

static void set_styles(void)
{
	grid_set_style(ST_BASE,  0xffcccccc, 0xee151515);
	grid_set_style(ST_TITLE, 0xff7fbfff, 0xee151515);
	grid_set_style(ST_ITEM,  0xffcccccc, 0xee151515);
	grid_set_style(ST_SEL,   0xff151515, 0xff7fbfff);
}

static void paint(void)
{
	int x = 2;
	int i;

	grid_clear(ST_BASE);
	grid_text(2, 1, "Session", ST_TITLE);

	for (i = 0; i < 3; i++) {
		char label[32];
		snprintf(label, sizeof label, " %s ", items[i]);
		grid_text(x, 3, label, i == selection ? ST_SEL : ST_ITEM);
		x += grid_str_w(items[i]) + 3;
	}

	if (grid_rows() > 4)
		grid_text(2, 4, last_key, ST_ITEM);
}

void app_resize(int cols, int rows)
{
	if (cols != grid_cols() || rows != grid_rows()) {
		grid_free();
		if (grid_init(cols, rows) < 0) {
			loop_quit(1);
			return;
		}
		set_styles();
	}
	paint();
}

int app_on_key(const char *name)
{
	if (getenv("WWEFT_DEBUG"))
		fprintf(stderr, "key: %s\n", name);

	snprintf(last_key, sizeof last_key, "%s", name);

	if (strcmp(name, "Escape") == 0) {
		loop_quit(1);
		return 1;
	}
	if (strcmp(name, "Return") == 0) {
		printf("%s\n", items[selection]);
		loop_quit(0);
		return 1;
	}
	if (strcmp(name, "Left") == 0 && selection > 0)
		selection--;
	else if (strcmp(name, "Right") == 0 && selection < 2)
		selection++;

	paint();
	return 1;
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

	if (wl_connect() < 0) {
		wl_stop();
		return 1;
	}

	font_open(NULL, size, wl_scale());

	/* grid_init resets the style table, so the styles come after it. */
	if (grid_init(cols > 0 ? cols : 1, rows > 0 ? rows : 1) < 0) {
		fprintf(stderr, "wweft: out of memory\n");
		wl_stop();
		return 1;
	}
	set_styles();

	if (wl_open(cols, rows) < 0) {
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
