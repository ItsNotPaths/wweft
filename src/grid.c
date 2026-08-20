/* The cell array, the styles, and the text measure. No VM and no Wayland. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "wweft.h"

static struct {
	struct cell *cells;
	int cols, rows;      /* the whole grid, border included */
	int inset;           /* 1 when a border is on */
} G;

/* The styles outlive the cell array, because a resize frees the cells and
 * the script sets the styles one time. */
static struct {
	uint32_t fg[GRID_STYLES];
	uint32_t bg[GRID_STYLES];
	int used;
} S;

/* --------------------------------------------------------------- utf-8 */

/* Read one code point. It returns the number of bytes used. */
static int utf8_next(const char *s, uint32_t *cp)
{
	const unsigned char *u = (const unsigned char *)s;
	int n, i;

	if (u[0] < 0x80) { *cp = u[0]; return 1; }
	else if ((u[0] & 0xe0) == 0xc0) { *cp = u[0] & 0x1fu; n = 1; }
	else if ((u[0] & 0xf0) == 0xe0) { *cp = u[0] & 0x0fu; n = 2; }
	else if ((u[0] & 0xf8) == 0xf0) { *cp = u[0] & 0x07u; n = 3; }
	else { *cp = 0xfffd; return 1; }

	for (i = 1; i <= n; i++) {
		if ((u[i] & 0xc0) != 0x80) { *cp = 0xfffd; return 1; }
		*cp = (*cp << 6) | (u[i] & 0x3fu);
	}
	return n + 1;
}

/* Display columns of one code point. A combining mark takes none. */
static int cp_width(uint32_t cp)
{
	int w = wcwidth((wchar_t)cp);

	if (w < 0)
		return cp < 0x20u ? 0 : 1;
	return w;
}

int grid_str_w(const char *utf8)
{
	int total = 0;
	uint32_t cp;

	while (*utf8) {
		utf8 += utf8_next(utf8, &cp);
		total += cp_width(cp);
	}
	return total;
}

/* ---------------------------------------------------------------- grid */

void grid_reset_styles(void)
{
	int i;

	for (i = 0; i < GRID_STYLES; i++) {
		S.fg[i] = 0xffcccccc;
		S.bg[i] = 0x00000000;
	}
	S.bg[0] = 0xee151515;   /* style 0 is the base: dark and a little clear */
	S.used = 1;
}

int grid_add_style(uint32_t fg, uint32_t bg)
{
	if (S.used >= GRID_STYLES)
		return -1;

	S.fg[S.used] = fg;
	S.bg[S.used] = bg;
	return S.used++;
}

int grid_init(int cols, int rows)
{
	if (cols < 1 || rows < 1)
		return -1;

	free(G.cells);
	G.cells = calloc((size_t)cols * (size_t)rows, sizeof *G.cells);
	if (!G.cells) {
		G.cols = G.rows = 0;
		return -1;
	}

	G.cols = cols;
	G.rows = rows;
	return 0;
}

void grid_free(void)
{
	free(G.cells);
	memset(&G, 0, sizeof G);
}

void grid_set_style(int id, uint32_t fg, uint32_t bg)
{
	if (id < 0 || id >= GRID_STYLES)
		return;
	S.fg[id] = fg;
	S.bg[id] = bg;
}

void grid_style_colors(int id, uint32_t *fg, uint32_t *bg)
{
	if (id < 0 || id >= GRID_STYLES)
		id = 0;
	*fg = S.fg[id];
	*bg = S.bg[id];
}

int grid_cols(void) { return G.cols - 2 * G.inset; }
int grid_rows(void) { return G.rows - 2 * G.inset; }
int grid_full_cols(void) { return G.cols; }
int grid_full_rows(void) { return G.rows; }

void grid_set_inset(int on)
{
	G.inset = on ? 1 : 0;
}

int grid_inset(void)
{
	return G.inset;
}

const struct cell *grid_cell(int x, int y)
{
	if (x < 0 || y < 0 || x >= G.cols || y >= G.rows)
		return NULL;
	return &G.cells[y * G.cols + x];
}

void grid_clear(int style)
{
	int i, n = G.cols * G.rows;

	for (i = 0; i < n; i++) {
		G.cells[i].cp = 0;
		G.cells[i].style = (uint8_t)style;
		G.cells[i].cont = 0;
	}
}

/* Script coordinates. It writes nothing outside the inner area, so a
 * border can never be painted over. */
static struct cell *at(int x, int y)
{
	if (x < 0 || y < 0 || x >= grid_cols() || y >= grid_rows())
		return NULL;
	return &G.cells[(y + G.inset) * G.cols + (x + G.inset)];
}

void grid_fill(int x, int y, int w, int h, int style)
{
	int cx, cy;

	for (cy = y; cy < y + h; cy++) {
		for (cx = x; cx < x + w; cx++) {
			struct cell *c = at(cx, cy);
			if (!c)
				continue;
			c->cp = 0;
			c->cont = 0;
			c->style = (uint8_t)style;
		}
	}
}

/* The ring around the inner area. It is drawn after the script, so the
 * script cannot damage it. chars holds six code points: the four corners
 * clockwise from the top left, then horizontal, then vertical. */
void grid_border(int style, const char *chars)
{
	uint32_t cp[6];
	int i, x, y;

	if (!G.inset || G.cols < 2 || G.rows < 2)
		return;

	for (i = 0; i < 6; i++) {
		if (!*chars) {
			cp[i] = i < 4 ? '+' : (i == 4 ? '-' : '|');
			continue;
		}
		chars += utf8_next(chars, &cp[i]);
	}

	for (x = 1; x < G.cols - 1; x++) {
		G.cells[x] = (struct cell){ cp[4], (uint8_t)style, 0 };
		G.cells[(G.rows - 1) * G.cols + x] =
			(struct cell){ cp[4], (uint8_t)style, 0 };
	}
	for (y = 1; y < G.rows - 1; y++) {
		G.cells[y * G.cols] = (struct cell){ cp[5], (uint8_t)style, 0 };
		G.cells[y * G.cols + G.cols - 1] =
			(struct cell){ cp[5], (uint8_t)style, 0 };
	}

	G.cells[0] = (struct cell){ cp[0], (uint8_t)style, 0 };
	G.cells[G.cols - 1] = (struct cell){ cp[1], (uint8_t)style, 0 };
	G.cells[G.rows * G.cols - 1] = (struct cell){ cp[2], (uint8_t)style, 0 };
	G.cells[(G.rows - 1) * G.cols] = (struct cell){ cp[3], (uint8_t)style, 0 };
}

void grid_text(int x, int y, const char *utf8, int style)
{
	uint32_t cp;

	while (*utf8) {
		struct cell *c;
		int w;

		utf8 += utf8_next(utf8, &cp);
		w = cp_width(cp);

		if (w == 0)
			continue;              /* step 2 drops combining marks */
		if (x >= grid_cols())
			return;

		c = at(x, y);
		if (c) {
			c->cp = cp;
			c->style = (uint8_t)style;
			c->cont = 0;

			if (w == 2 && (c = at(x + 1, y))) {
				c->cp = 0;
				c->style = (uint8_t)style;
				c->cont = 1;
			}
		}
		x += w;
	}
}
