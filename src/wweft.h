/* Internal header. Build step 2: grid, font, render. No Wren, no xkbcommon. */
#ifndef WWEFT_H
#define WWEFT_H

#include <stdint.h>

/* ------------------------------------------------------------------ font */

struct glyph {
	const unsigned char *alpha;   /* w * h, one byte for each pixel */
	int w, h;
	int left, top;                /* from the pen on the baseline */
};

int  font_open(const char *path, int px);   /* path NULL = search. 0 = ok */
void font_close(void);
const struct glyph *font_glyph(uint32_t cp);
int  font_cell_w(void);
int  font_cell_h(void);
int  font_baseline(void);
const char *font_source(void);              /* the path, or "spleen 8x16" */

/* ------------------------------------------------------------------ grid */

struct cell {
	uint32_t cp;      /* 0 = empty */
	uint8_t  style;
	uint8_t  cont;    /* second half of a wide glyph. Do not draw */
};

#define GRID_STYLES 16

int  grid_init(int cols, int rows);
void grid_free(void);
void grid_set_style(int id, uint32_t fg, uint32_t bg);   /* 0xAARRGGBB */
void grid_clear(int style);
void grid_text(int x, int y, const char *utf8, int style);
void grid_fill(int x, int y, int w, int h, int style);
int  grid_str_w(const char *utf8);          /* display columns */
int  grid_cols(void);
int  grid_rows(void);
const struct cell *grid_cell(int x, int y);
void grid_style_colors(int id, uint32_t *fg, uint32_t *bg);

/* ---------------------------------------------------------------- render */

void render_frame(uint32_t *pixels, int width, int height);

/* ------------------------------------------------------------------ loop */

int  loop_run(void);
void loop_quit(int code);

/* -------------------------------------------------------------- wayland */

int  wl_start(int width, int height);
void wl_stop(void);
void wl_redraw(void);
int  wl_get_fd(void);
int  wl_prepare(void);
int  wl_read(void);
void wl_cancel(void);
int  wl_dispatch(void);

/* ----------------------------------------------------------------- input */

struct wl_seat;
void input_bind_seat(struct wl_seat *seat);
void input_stop(void);

#endif
