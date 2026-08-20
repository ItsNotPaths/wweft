/* Internal header. Build step 4: grid, font, render, keys, Wren. */
#ifndef WWEFT_H
#define WWEFT_H

#include <stdint.h>
#include <stddef.h>

/* ------------------------------------------------------------------ font */

struct glyph {
	const unsigned char *alpha;   /* w * h, one byte for each pixel */
	int w, h;
	int left, top;                /* from the pen on the baseline */
};

int  font_open(const char *path, int px, int scale);   /* NULL = search */
void font_close(void);
const struct glyph *font_glyph(uint32_t cp);
int  font_cell_w(void);
int  font_cell_h(void);
int  font_baseline(void);
int  font_scale(void);
const char *font_source(void);              /* the path, or "spleen 8x16" */

/* ------------------------------------------------------------------ grid */

struct cell {
	uint32_t cp;      /* 0 = empty */
	uint8_t  style;
	uint8_t  cont;    /* second half of a wide glyph. Do not draw */
};

#define GRID_STYLES 32

int  grid_init(int cols, int rows);
void grid_free(void);
void grid_reset_styles(void);
void grid_set_style(int id, uint32_t fg, uint32_t bg);   /* 0xAARRGGBB */
int  grid_add_style(uint32_t fg, uint32_t bg);           /* new id, or -1 */
void grid_clear(int style);
void grid_text(int x, int y, const char *utf8, int style);
void grid_fill(int x, int y, int w, int h, int style);
int  grid_str_w(const char *utf8);          /* display columns */
int  grid_cols(void);          /* what the script sees: inside the border */
int  grid_rows(void);
int  grid_full_cols(void);     /* what the renderer draws: border included */
int  grid_full_rows(void);

/* A border of one cell on every side. The script keeps counting from 0, so
 * nothing in its layout moves. */
void grid_set_inset(int on);
int  grid_inset(void);
void grid_border(int style, const char *chars);   /* TL TR BR BL H V */
const struct cell *grid_cell(int x, int y);
void grid_style_colors(int id, uint32_t *fg, uint32_t *bg);

/* ---------------------------------------------------------------- render */

void render_frame(uint32_t *pixels, int width, int height);

/* ------------------------------------------------------------------ loop */

int  loop_run(void);
void loop_quit(int code);
void loop_every(int ms);        /* 0 stops the tick */
void loop_lifetime(int ms);     /* quit after this long. Any call resets it */

/* ------------------------------------------------------------- messages */

int  msg_listen(const char *spec);   /* a name, or a path to a unix socket */
int  msg_count(void);
int  msg_fd(int i);
void msg_read(int i);
void msg_stop(void);
int  msg_send(const char *name, const char *text);
int  msg_watch(const char *path);    /* a file another program writes */
int  msg_watch_fd(void);
void msg_watch_read(void);

/* -------------------------------------------------------------- wayland */

void wl_set_layer(const char *name);        /* before wl_open */
void wl_set_anchor(const char *spec);
void wl_set_margin(int top, int right, int bottom, int left);   /* cells */
void wl_set_exclusive(int cells);           /* -1 = ignore */
void wl_set_scale(int n);                   /* 0 = follow the output */
void wl_set_output(const char *name);       /* "" = the compositor chooses */
int  wl_connect(void);                      /* bind the globals */
int  wl_scale(void);                        /* whole number output scale */
int  wl_open(int cols, int rows);           /* 0 on an axis = fill */
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
const char *input_key_text(void);   /* UTF-8 of the last key, or "" */
int  input_timer_fd(void);       /* key repeat. -1 while no key repeats */
void input_timer_fire(void);

/* ------------------------------------------------------------------- app */
/* main.c joins the grid, the surface, and the script. */

void app_resize(int cols, int rows);   /* the surface size changed */
void app_paint(void);                  /* fill the grid before a frame */
int  app_on_key(const char *name);     /* 0 = not handled, no redraw */
void app_on_blur(void);                /* the surface lost the keyboard */
void app_on_tick(void);                /* the Surface.every timer fired */
void app_on_message(const char *line); /* a line arrived on a channel */
void app_on_change(const char *path);  /* a watched file was written */

/* --------------------------------------------------------------- script */
/* script_wren.c is the only file that includes wren.h. */

int  script_init(const char *path);
void script_set_args(int count, char **args);
void script_window_size(int *cols, int *rows);
int  script_font_done(void);
int  script_dismiss(void);              /* close when the focus goes away */
int  script_border(int *style, const char **chars);
int  script_on_key(const char *name);   /* 0 = not handled, no redraw */
void script_on_draw(void);
void script_on_tick(void);
void script_on_message(const char *line);
void script_on_change(const char *path);
void script_close(void);
size_t script_bytes(void);              /* live bytes in the Wren heap */

#endif
