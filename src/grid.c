/* The cell array, the styles, and the text measure. No VM and no Wayland. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "wweft.h"

static struct {
	struct cell *cells;
	int cols, rows;
	uint32_t fg[GRID_STYLES];
	uint32_t bg[GRID_STYLES];
} G;

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

int grid_init(int cols, int rows)
{
	int i;

	if (cols < 1 || rows < 1)
		return -1;

	G.cells = calloc((size_t)cols * (size_t)rows, sizeof *G.cells);
	if (!G.cells)
		return -1;

	G.cols = cols;
	G.rows = rows;

	for (i = 0; i < GRID_STYLES; i++) {
		G.fg[i] = 0xffcccccc;
		G.bg[i] = 0x00000000;
	}
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
	G.fg[id] = fg;
	G.bg[id] = bg;
}

void grid_style_colors(int id, uint32_t *fg, uint32_t *bg)
{
	if (id < 0 || id >= GRID_STYLES)
		id = 0;
	*fg = G.fg[id];
	*bg = G.bg[id];
}

int grid_cols(void) { return G.cols; }
int grid_rows(void) { return G.rows; }

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

void grid_fill(int x, int y, int w, int h, int style)
{
	int cx, cy;

	for (cy = y; cy < y + h; cy++) {
		if (cy < 0 || cy >= G.rows)
			continue;
		for (cx = x; cx < x + w; cx++) {
			struct cell *c;
			if (cx < 0 || cx >= G.cols)
				continue;
			c = &G.cells[cy * G.cols + cx];
			c->cp = 0;
			c->cont = 0;
			c->style = (uint8_t)style;
		}
	}
}

void grid_text(int x, int y, const char *utf8, int style)
{
	uint32_t cp;

	if (y < 0 || y >= G.rows)
		return;

	while (*utf8) {
		int w;

		utf8 += utf8_next(utf8, &cp);
		w = cp_width(cp);

		if (w == 0)
			continue;              /* step 2 drops combining marks */
		if (x >= G.cols)
			return;

		if (x >= 0) {
			struct cell *c = &G.cells[y * G.cols + x];
			c->cp = cp;
			c->style = (uint8_t)style;
			c->cont = 0;

			if (w == 2 && x + 1 < G.cols) {
				c[1].cp = 0;
				c[1].style = (uint8_t)style;
				c[1].cont = 1;
			}
		}
		x += w;
	}
}
