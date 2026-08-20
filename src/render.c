/* The grid to ARGB8888 pixels. Alpha is premultiplied. */
#include <stdint.h>

#include "wweft.h"

static uint32_t premultiply(uint32_t argb)
{
	uint32_t a = argb >> 24;
	uint32_t r = (argb >> 16) & 0xff;
	uint32_t g = (argb >> 8) & 0xff;
	uint32_t b = argb & 0xff;

	r = r * a / 255;
	g = g * a / 255;
	b = b * a / 255;
	return (a << 24) | (r << 16) | (g << 8) | b;
}

/* src is premultiplied already. Both are ARGB8888. */
static uint32_t over(uint32_t src, uint32_t dst)
{
	uint32_t ia = 255 - (src >> 24);
	uint32_t rb = (((dst & 0x00ff00ffu) * ia) >> 8) & 0x00ff00ffu;
	uint32_t ag = (((dst >> 8) & 0x00ff00ffu) * ia) & 0xff00ff00u;

	return src + rb + ag;
}

static uint32_t fade(uint32_t premul, uint32_t cover)
{
	uint32_t a = ((premul >> 24) * cover) / 255;
	uint32_t r = (((premul >> 16) & 0xff) * cover) / 255;
	uint32_t g = (((premul >> 8) & 0xff) * cover) / 255;
	uint32_t b = ((premul & 0xff) * cover) / 255;

	return (a << 24) | (r << 16) | (g << 8) | b;
}

void render_frame(uint32_t *pixels, int width, int height)
{
	int cw = font_cell_w();
	int ch = font_cell_h();
	int base = font_baseline();
	int cx, cy, x, y;

	/* Pass 1: the cell backgrounds. */
	for (cy = 0; cy < grid_rows(); cy++) {
		for (cx = 0; cx < grid_cols(); cx++) {
			const struct cell *c = grid_cell(cx, cy);
			uint32_t fg, bg;
			int x0 = cx * cw, y0 = cy * ch;

			grid_style_colors(c->style, &fg, &bg);
			bg = premultiply(bg);

			for (y = y0; y < y0 + ch && y < height; y++)
				for (x = x0; x < x0 + cw && x < width; x++)
					pixels[y * width + x] = bg;
		}
	}

	/* Pass 2: the glyphs. A wide glyph may cross into the next cell. */
	for (cy = 0; cy < grid_rows(); cy++) {
		for (cx = 0; cx < grid_cols(); cx++) {
			const struct cell *c = grid_cell(cx, cy);
			const struct glyph *g;
			uint32_t fg, bg, src;
			int pen_x, pen_y;

			if (c->cp == 0 || c->cont)
				continue;

			g = font_glyph(c->cp);
			if (!g)
				continue;

			grid_style_colors(c->style, &fg, &bg);
			src = premultiply(fg);

			pen_x = cx * cw + g->left;
			pen_y = cy * ch + base + g->top;

			for (y = 0; y < g->h; y++) {
				int py = pen_y + y;
				if (py < 0 || py >= height)
					continue;
				for (x = 0; x < g->w; x++) {
					int px = pen_x + x;
					uint32_t cover = g->alpha[y * g->w + x];
					if (px < 0 || px >= width || cover == 0)
						continue;
					pixels[py * width + px] =
						over(fade(src, cover),
						     pixels[py * width + px]);
				}
			}
		}
	}
}
