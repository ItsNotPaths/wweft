/* Wayland: layer surface, shm buffers, damage, output scale. */
#define _GNU_SOURCE
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <wayland-client.h>

#include "fractional-scale-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "wweft.h"

struct buffer {
	struct wl_buffer *wl;
	uint32_t *pixels;
	bool busy;
};

static struct {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct zwlr_layer_shell_v1 *shell;
	struct wl_surface *surface;
	struct zwlr_layer_surface_v1 *layer;
	struct wl_seat *seat;
	struct wp_viewporter *viewporter;
	struct wp_fractional_scale_manager_v1 *fsm;
	struct wp_viewport *viewport;
	struct wp_fractional_scale_v1 *fscale;

	struct buffer buffers[2];
	void *pool_data;
	size_t pool_size;

	int scale;        /* whole number fallback, when there is no viewporter */
	int scale120;     /* device pixels for 120 logical pixels. 0 = not told */
	struct wl_output *outputs[8];
	char output_names[8][64];
	int output_count;
	char want_output[64];
	uint32_t layer_id;
	uint32_t anchor;
	int margin[4];    /* logical px: top, right, bottom, left */
	int exclusive;    /* cells. -1 = ignore other surfaces */
	int keyboard;     /* -1 follows exclusive, else 0 or 1 */
	int width;        /* buffer, in device pixels */
	int height;
	int logical_w;    /* what the compositor last asked for */
	int logical_h;
	int want_cols;    /* what the script asked for. 0 means fill */
	int want_rows;
	int outline;      /* device pixels of outline, outside the cells */
	bool configured;
	bool drawn;
	bool got_scale;   /* the compositor told us the fractional scale */
} W;

/* ------------------------------------------------------- script setters */

void wl_set_layer(const char *name)
{
	if (strcmp(name, "background") == 0)
		W.layer_id = ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND;
	else if (strcmp(name, "bottom") == 0)
		W.layer_id = ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM;
	else if (strcmp(name, "top") == 0)
		W.layer_id = ZWLR_LAYER_SHELL_V1_LAYER_TOP;
	else
		W.layer_id = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
}

/* "center", "top", "bottom-left", "top-right", ... */
void wl_set_anchor(const char *spec)
{
	W.anchor = 0;

	if (strstr(spec, "top"))
		W.anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
	if (strstr(spec, "bottom"))
		W.anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
	if (strstr(spec, "left"))
		W.anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
	if (strstr(spec, "right"))
		W.anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
}

void wl_set_margin(int top, int right, int bottom, int left)
{
	W.margin[0] = top;
	W.margin[1] = right;
	W.margin[2] = bottom;
	W.margin[3] = left;
}

/* Device pixels added outside the cells, on every side. */
void wl_set_outline(int device_px)
{
	W.outline = device_px > 0 ? device_px : 0;
}

/* 0 keeps the output scale. Call it before Surface.font. */
void wl_set_scale(int n)
{
	if (n > 0)
		W.scale = n;
}

/* Empty means: let the compositor choose. */
void wl_set_output(const char *name)
{
	snprintf(W.want_output, sizeof W.want_output, "%s", name ? name : "");
}

static struct wl_output *chosen_output(void)
{
	int i;

	if (!W.want_output[0])
		return NULL;

	for (i = 0; i < W.output_count; i++)
		if (strcmp(W.output_names[i], W.want_output) == 0)
			return W.outputs[i];

	fprintf(stderr, "wweft: no output named %s\n", W.want_output);
	return NULL;
}

void wl_set_exclusive(int cells)
{
	W.exclusive = cells;
}

/* -1 keeps the old rule: a popup takes the keyboard, a surface that
 * reserves space does not. */
void wl_set_keyboard(int on)
{
	W.keyboard = on;
}

static int keyboard_on(void)
{
	return W.keyboard >= 0 ? W.keyboard : (W.exclusive < 0 ? 1 : 0);
}

/* The globals, never the viewport object: the viewport is made in wl_open
 * and the font is sized before that. */
static int fractional(void)
{
	return W.viewporter != NULL && W.fsm != NULL;
}

int wl_scale120(void)
{
	return W.scale120 > 0 ? W.scale120 : 120;
}

int wl_align(void)
{
	return fractional() ? 1 : W.scale;
}

/* Up, so a buffer is never a fraction short and stretched to fit. */
static int to_device(int logical)
{
	return (logical * wl_scale120() + 119) / 120;
}

int wl_to_logical(int device)
{
	int scale = wl_scale120();

	return (device * 120 + scale / 2) / scale;
}

void wl_logical_size(int *w, int *h)
{
	*w = W.logical_w;
	*h = W.logical_h;
}

/* Device pixels. A script sized axis is a whole number of cells; going
 * through logical and back would round twice. A filled axis is whatever the
 * screen is, so render.c paints the strip past the last cell. */
static void buffer_size(int logical_w, int logical_h, int *px_w, int *px_h)
{
	*px_w = W.want_cols > 0 ? W.want_cols * font_cell_w() + 2 * W.outline
				: (fractional() ? to_device(logical_w)
						: logical_w * W.scale);
	*px_h = W.want_rows > 0 ? W.want_rows * font_cell_h() + 2 * W.outline
				: (fractional() ? to_device(logical_h)
						: logical_h * W.scale);
}

static void fail(const char *what)
{
	fprintf(stderr, "wweft: %s\n", what);
}

/* ------------------------------------------------------------- buffers */

/* Set when a redraw found both buffers still in the compositor's hands. The
 * next release runs it, so the frame is late rather than lost. */
static bool redraw_pending;

static void draw(void);

static void on_release(void *data, struct wl_buffer *wl)
{
	(void)wl;
	((struct buffer *)data)->busy = false;
	if (redraw_pending)
		draw();
}

static const struct wl_buffer_listener buffer_listener = { on_release };

static void drop_buffers(void)
{
	int i;

	for (i = 0; i < 2; i++) {
		if (W.buffers[i].wl)
			wl_buffer_destroy(W.buffers[i].wl);
		W.buffers[i].wl = NULL;
		W.buffers[i].pixels = NULL;
		W.buffers[i].busy = false;
	}
	if (W.pool_data) {
		munmap(W.pool_data, W.pool_size);
		W.pool_data = NULL;
		W.pool_size = 0;
	}
}

static int make_buffers(void)
{
	int stride = W.width * 4;
	size_t one = (size_t)stride * (size_t)W.height;
	struct wl_shm_pool *pool;
	int fd;
	int i;

	drop_buffers();
	W.pool_size = one * 2;

	fd = memfd_create("wweft", MFD_CLOEXEC);
	if (fd < 0) {
		fail("memfd_create failed");
		return -1;
	}
	if (ftruncate(fd, (off_t)W.pool_size) < 0) {
		fail("ftruncate failed");
		close(fd);
		return -1;
	}

	W.pool_data = mmap(NULL, W.pool_size, PROT_READ | PROT_WRITE,
			   MAP_SHARED, fd, 0);
	if (W.pool_data == MAP_FAILED) {
		fail("mmap failed");
		W.pool_data = NULL;
		close(fd);
		return -1;
	}

	pool = wl_shm_create_pool(W.shm, fd, (int32_t)W.pool_size);
	close(fd);

	for (i = 0; i < 2; i++) {
		struct buffer *b = &W.buffers[i];
		b->pixels = (uint32_t *)((char *)W.pool_data + one * (size_t)i);
		b->wl = wl_shm_pool_create_buffer(pool, (int32_t)(one * (size_t)i),
						  W.width, W.height, stride,
						  WL_SHM_FORMAT_ARGB8888);
		wl_buffer_add_listener(b->wl, &buffer_listener, b);
	}
	wl_shm_pool_destroy(pool);
	return 0;
}

static struct buffer *free_buffer(void)
{
	if (!W.buffers[0].busy)
		return &W.buffers[0];
	if (!W.buffers[1].busy)
		return &W.buffers[1];
	return NULL;
}

/* Debug only: write the buffer we are about to send, as a PAM file. */
static void dump(const uint32_t *px)
{
	const char *path = getenv("WWEFT_DUMP");
	FILE *f;
	int i, n = W.width * W.height;

	if (!path || !(f = fopen(path, "wb")))
		return;

	fprintf(f, "P7\nWIDTH %d\nHEIGHT %d\nDEPTH 4\nMAXVAL 255\n"
		   "TUPLTYPE RGB_ALPHA\nENDHDR\n", W.width, W.height);
	for (i = 0; i < n; i++) {
		unsigned char rgba[4] = {
			(unsigned char)(px[i] >> 16), (unsigned char)(px[i] >> 8),
			(unsigned char)px[i], (unsigned char)(px[i] >> 24)
		};
		fwrite(rgba, 1, 4, f);
	}
	fclose(f);
}

static void draw(void)
{
	struct buffer *b = free_buffer();

	if (!W.configured)
		return;

	/* Both buffers are on screen or queued. Remember the frame instead of
	 * dropping it: input arrives in bursts, and the last line of a burst
	 * is the one carrying the state worth showing. */
	if (!b) {
		redraw_pending = true;
		return;
	}
	redraw_pending = false;

	app_paint();
	render_frame(b->pixels, W.width, W.height);
	dump(b->pixels);

	b->busy = true;
	wl_surface_attach(W.surface, b->wl, 0, 0);
	wl_surface_damage_buffer(W.surface, 0, 0, W.width, W.height);
	wl_surface_commit(W.surface);
}

void wl_redraw(void)
{
	draw();
}

/* ------------------------------------------------------- layer surface */

static void on_configure(void *data, struct zwlr_layer_surface_v1 *ls,
			 uint32_t serial, uint32_t width, uint32_t height)
{
	int px_w, px_h, cols, rows;

	(void)data;

	if (getenv("WWEFT_DEBUG"))
		fprintf(stderr, "configure: %ux%u logical, scale120=%d\n",
			width, height, W.scale120);

	zwlr_layer_surface_v1_ack_configure(ls, serial);

	/* The event carries logical pixels. The buffer holds device pixels. */
	W.logical_w = (int)width;
	W.logical_h = (int)height;

	buffer_size((int)width, (int)height, &px_w, &px_h);

	if (px_w < 1 || px_h < 1) {
		fail("the compositor asked for an empty surface");
		loop_quit(1);
		return;
	}

	if (px_w != W.width || px_h != W.height || !W.configured) {
		W.width = px_w;
		W.height = px_h;
		if (make_buffers() < 0) {
			loop_quit(1);
			return;
		}
		W.configured = true;
		cols = (W.width - 2 * W.outline) / font_cell_w();
		rows = (W.height - 2 * W.outline) / font_cell_h();
		app_resize(cols > 0 ? cols : 1, rows > 0 ? rows : 1);
	}

	/* Every configure, not only on a size change: the logical size can
	 * move while the buffer does not. */
	if (W.viewport)
		wp_viewport_set_destination(W.viewport, (int32_t)width,
					    (int32_t)height);

	/* The scale event follows the first configure, so hold frame one. */
	if (fractional() && !W.got_scale)
		return;

	draw();
	W.drawn = true;
}

static void on_closed(void *data, struct zwlr_layer_surface_v1 *ls)
{
	(void)data;
	(void)ls;
	loop_quit(0);
}

static const struct zwlr_layer_surface_v1_listener layer_listener = {
	on_configure, on_closed
};

/* ----------------------------------------------------- fractional scale */

static void on_preferred_scale(void *data, struct wp_fractional_scale_v1 *fs,
			       uint32_t scale)
{
	(void)data;
	(void)fs;

	if (getenv("WWEFT_DEBUG"))
		fprintf(stderr, "preferred scale: %u/120 = %.3f\n",
			scale, (double)scale / 120.0);

	if (scale == 0)
		return;

	W.got_scale = true;

	if ((int)scale == W.scale120)
		return;

	W.scale120 = (int)scale;

	/* Corrects frame one, and fires again on a move between monitors. */
	if (W.configured)
		app_rescale();
}

static const struct wp_fractional_scale_v1_listener fscale_listener = {
	on_preferred_scale
};

/* --------------------------------------------------------------- output */

static void out_geometry(void *d, struct wl_output *o, int32_t x, int32_t y,
			 int32_t pw, int32_t ph, int32_t sub, const char *make,
			 const char *model, int32_t transform)
{
	(void)d; (void)o; (void)x; (void)y; (void)pw; (void)ph;
	(void)sub; (void)make; (void)model; (void)transform;
}

static void out_mode(void *d, struct wl_output *o, uint32_t flags,
		     int32_t w, int32_t h, int32_t refresh)
{
	(void)d; (void)o; (void)flags; (void)w; (void)h; (void)refresh;
}

static void out_done(void *d, struct wl_output *o)
{
	(void)d; (void)o;
}

static void out_scale(void *d, struct wl_output *o, int32_t factor)
{
	(void)d; (void)o;

	if (factor > W.scale)
		W.scale = factor;
}

static void out_name(void *d, struct wl_output *o, const char *name)
{
	int i;

	(void)d;

	for (i = 0; i < W.output_count; i++)
		if (W.outputs[i] == o)
			snprintf(W.output_names[i], sizeof W.output_names[i],
				 "%s", name);
}

static void out_description(void *d, struct wl_output *o, const char *desc)
{
	(void)d; (void)o; (void)desc;
}

static const struct wl_output_listener output_listener = {
	out_geometry, out_mode, out_done, out_scale, out_name, out_description
};

/* ------------------------------------------------------------ registry */

static uint32_t pick(uint32_t have, uint32_t want)
{
	return have < want ? have : want;
}

static void on_global(void *data, struct wl_registry *reg, uint32_t name,
		      const char *iface, uint32_t version)
{
	(void)data;

	if (strcmp(iface, wl_compositor_interface.name) == 0) {
		W.compositor = wl_registry_bind(reg, name,
						&wl_compositor_interface,
						pick(version, 4));
	} else if (strcmp(iface, wl_shm_interface.name) == 0) {
		W.shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
	} else if (strcmp(iface, wl_seat_interface.name) == 0) {
		W.seat = wl_registry_bind(reg, name, &wl_seat_interface,
					  pick(version, 5));
		/* Listen now: the capabilities event is already on its way,
		 * and an event with no listener is dropped. */
		input_bind_seat(W.seat);
	} else if (strcmp(iface, wl_output_interface.name) == 0) {
		/* Version 4 carries the name event. */
		struct wl_output *out = wl_registry_bind(reg, name,
							 &wl_output_interface,
							 pick(version, 4));
		wl_output_add_listener(out, &output_listener, NULL);
		if (W.output_count < 8)
			W.outputs[W.output_count++] = out;
	} else if (strcmp(iface, wp_viewporter_interface.name) == 0) {
		if (!getenv("WWEFT_NO_FRACTIONAL"))
			W.viewporter = wl_registry_bind(reg, name,
							&wp_viewporter_interface, 1);
	} else if (strcmp(iface, wp_fractional_scale_manager_v1_interface.name) == 0) {
		/* Escape hatch for broken fractional scale support. */
		if (!getenv("WWEFT_NO_FRACTIONAL"))
			W.fsm = wl_registry_bind(reg, name,
						 &wp_fractional_scale_manager_v1_interface, 1);
	} else if (strcmp(iface, zwlr_layer_shell_v1_interface.name) == 0) {
		W.shell = wl_registry_bind(reg, name,
					   &zwlr_layer_shell_v1_interface,
					   pick(version, 4));
	}
}

static void on_global_remove(void *data, struct wl_registry *reg, uint32_t name)
{
	(void)data; (void)reg; (void)name;
}

static const struct wl_registry_listener registry_listener = {
	on_global, on_global_remove
};

/* --------------------------------------------------------------- start */

int wl_connect(void)
{
	W.scale = 1;
	W.exclusive = -1;
	W.keyboard = -1;
	W.layer_id = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;

	W.display = wl_display_connect(NULL);
	if (!W.display) {
		fail("no Wayland display. Is WAYLAND_DISPLAY set?");
		return -1;
	}

	W.registry = wl_display_get_registry(W.display);
	wl_registry_add_listener(W.registry, &registry_listener, NULL);
	wl_display_roundtrip(W.display);   /* the globals arrive */
	wl_display_roundtrip(W.display);   /* the output scale arrives */

	/* Fix the guess now, so the cell and the surface use one number. */
	W.scale120 = W.scale * 120;

	if (!W.compositor || !W.shm || !W.shell) {
		fail("the compositor has no wlr-layer-shell support");
		return -1;
	}

	return 0;
}

int wl_open(int cols, int rows)
{
	uint32_t anchor = W.anchor;
	int cw, ch, zone;

	W.want_cols = cols;
	W.want_rows = rows;

	/* The cell is device sized. The layer surface speaks logical. */
	cw = fractional() ? font_cell_w() : font_cell_w() / W.scale;
	ch = fractional() ? font_cell_h() : font_cell_h() / W.scale;

	/* A 0 axis stretches between two edges. */
	if (cols < 1)
		anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
			  ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
	if (rows < 1)
		anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
			  ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;

	W.surface = wl_compositor_create_surface(W.compositor);

	if (W.viewporter && W.fsm) {
		W.viewport = wp_viewporter_get_viewport(W.viewporter, W.surface);
		W.fscale = wp_fractional_scale_manager_v1_get_fractional_scale(
			W.fsm, W.surface);
		wp_fractional_scale_v1_add_listener(W.fscale, &fscale_listener,
						    NULL);
	}

	W.layer = zwlr_layer_shell_v1_get_layer_surface(
		W.shell, W.surface, chosen_output(), W.layer_id, "wweft");
	zwlr_layer_surface_v1_add_listener(W.layer, &layer_listener, NULL);

	{
		int want_w = cols * cw + 2 * W.outline;
		int want_h = rows * ch + 2 * W.outline;

		zwlr_layer_surface_v1_set_size(W.layer,
			cols > 0 ? (uint32_t)(fractional() ? wl_to_logical(want_w)
							   : want_w) : 0,
			rows > 0 ? (uint32_t)(fractional() ? wl_to_logical(want_h)
							   : want_h) : 0);
	}
	zwlr_layer_surface_v1_set_anchor(W.layer, anchor);
	zwlr_layer_surface_v1_set_margin(W.layer, W.margin[0], W.margin[1],
					 W.margin[2], W.margin[3]);

	zone = W.exclusive < 0 ? -1
			       : (fractional() ? wl_to_logical(W.exclusive * ch)
					       : W.exclusive * ch);
	zwlr_layer_surface_v1_set_exclusive_zone(W.layer, zone);

	zwlr_layer_surface_v1_set_keyboard_interactivity(W.layer,
							 (uint32_t)keyboard_on());
	/* With a viewport the buffer scale stays 1. */
	if (!fractional())
		wl_surface_set_buffer_scale(W.surface, W.scale);

	if (getenv("WWEFT_DEBUG"))
		fprintf(stderr, "open: scale120=%d cell=%dx%d outline=%d\n",
			wl_scale120(), font_cell_w(), font_cell_h(), W.outline);

	/* Commit an empty surface. The buffer waits for the configure event. */
	wl_surface_commit(W.surface);
	wl_display_roundtrip(W.display);

	if (!W.configured) {
		fail("no configure event");
		return -1;
	}

	/* Collect the scale event, which follows the first configure. */
	if (fractional() && !W.drawn)
		wl_display_roundtrip(W.display);

	if (!W.drawn) {
		draw();          /* no scale event came: draw at the guess */
		W.drawn = true;
	}
	return 0;
}

/* After the font was reopened. It rebuilds at once, because a logical size
 * that did not change brings no configure. */
void wl_rescale(void)
{
	int cw = font_cell_w();
	int ch = font_cell_h();

	if (!W.layer || !W.configured)
		return;

	if (getenv("WWEFT_DEBUG"))
		fprintf(stderr, "rescale: cell=%dx%d outline=%d\n",
			cw, ch, W.outline);

	zwlr_layer_surface_v1_set_size(W.layer,
		W.want_cols > 0 ? (uint32_t)wl_to_logical(W.want_cols * cw +
						       2 * W.outline) : 0,
		W.want_rows > 0 ? (uint32_t)wl_to_logical(W.want_rows * ch +
						       2 * W.outline) : 0);
	wl_surface_commit(W.surface);

	buffer_size(W.logical_w, W.logical_h, &W.width, &W.height);
	if (make_buffers() < 0) {
		loop_quit(1);
		return;
	}
	if (W.viewport)
		wp_viewport_set_destination(W.viewport, W.logical_w, W.logical_h);

	{
		int c = (W.width - 2 * W.outline) / cw;
		int r = (W.height - 2 * W.outline) / ch;

		app_resize(c > 0 ? c : 1, r > 0 ? r : 1);
	}
	draw();
	W.drawn = true;
}

void wl_stop(void)
{
	input_stop();
	drop_buffers();

	if (W.fscale)
		wp_fractional_scale_v1_destroy(W.fscale);
	if (W.viewport)
		wp_viewport_destroy(W.viewport);
	if (W.fsm)
		wp_fractional_scale_manager_v1_destroy(W.fsm);
	if (W.viewporter)
		wp_viewporter_destroy(W.viewporter);
	if (W.layer)
		zwlr_layer_surface_v1_destroy(W.layer);
	if (W.surface)
		wl_surface_destroy(W.surface);
	if (W.shell)
		zwlr_layer_shell_v1_destroy(W.shell);
	if (W.seat)
		wl_seat_destroy(W.seat);
	if (W.shm)
		wl_shm_destroy(W.shm);
	if (W.compositor)
		wl_compositor_destroy(W.compositor);
	if (W.registry)
		wl_registry_destroy(W.registry);
	if (W.display)
		wl_display_disconnect(W.display);
	memset(&W, 0, sizeof W);
}

/* ------------------------------------------------- calls from the loop */

int wl_get_fd(void)
{
	return wl_display_get_fd(W.display);
}

int wl_prepare(void)
{
	while (wl_display_prepare_read(W.display) != 0)
		if (wl_display_dispatch_pending(W.display) < 0)
			return -1;

	if (wl_display_flush(W.display) < 0 && errno != EAGAIN) {
		wl_display_cancel_read(W.display);
		return -1;
	}
	return 0;
}

int wl_read(void)
{
	return wl_display_read_events(W.display);
}

void wl_cancel(void)
{
	wl_display_cancel_read(W.display);
}

int wl_dispatch(void)
{
	return wl_display_dispatch_pending(W.display);
}
