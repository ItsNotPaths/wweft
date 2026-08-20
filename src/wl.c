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

	struct buffer buffers[2];
	void *pool_data;
	size_t pool_size;

	int scale;        /* device pixels for one logical pixel */
	uint32_t layer_id;
	uint32_t anchor;
	int margin[4];    /* cells: top, right, bottom, left */
	int exclusive;    /* cells. -1 = ignore other surfaces */
	int width;        /* buffer, in device pixels */
	int height;
	bool configured;
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

/* 0 keeps the output scale. Call it before Surface.font. */
void wl_set_scale(int n)
{
	if (n > 0)
		W.scale = n;
}

void wl_set_exclusive(int cells)
{
	W.exclusive = cells;
}

static void fail(const char *what)
{
	fprintf(stderr, "wweft: %s\n", what);
}

/* ------------------------------------------------------------- buffers */

static void on_release(void *data, struct wl_buffer *wl)
{
	(void)wl;
	((struct buffer *)data)->busy = false;
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

	if (!b || !W.configured)
		return;

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
	int px_w, px_h;

	(void)data;

	zwlr_layer_surface_v1_ack_configure(ls, serial);

	/* The event carries logical pixels. The buffer holds device pixels. */
	px_w = (int)width * W.scale;
	px_h = (int)height * W.scale;

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
		app_resize(W.width / font_cell_w(), W.height / font_cell_h());
	}
	draw();
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
	(void)d; (void)o; (void)name;
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
		/* The capabilities event follows at once. The listener must
		 * be there before the next roundtrip, or the event is lost. */
		input_bind_seat(W.seat);
	} else if (strcmp(iface, wl_output_interface.name) == 0) {
		struct wl_output *out = wl_registry_bind(reg, name,
							 &wl_output_interface,
							 pick(version, 2));
		wl_output_add_listener(out, &output_listener, NULL);
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

	if (!W.compositor || !W.shm || !W.shell) {
		fail("the compositor has no wlr-layer-shell support");
		return -1;
	}

	return 0;
}

int wl_scale(void)
{
	return W.scale;
}

int wl_open(int cols, int rows)
{
	uint32_t anchor = W.anchor;
	int cw = font_cell_w() / W.scale;   /* logical cell */
	int ch = font_cell_h() / W.scale;
	int zone;

	/* A 0 axis stretches between two edges and takes the size back from
	 * the configure event. */
	if (cols < 1)
		anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
			  ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
	if (rows < 1)
		anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
			  ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;

	W.surface = wl_compositor_create_surface(W.compositor);
	W.layer = zwlr_layer_shell_v1_get_layer_surface(
		W.shell, W.surface, NULL, W.layer_id, "wweft");
	zwlr_layer_surface_v1_add_listener(W.layer, &layer_listener, NULL);

	zwlr_layer_surface_v1_set_size(W.layer,
				       cols > 0 ? (uint32_t)(cols * cw) : 0,
				       rows > 0 ? (uint32_t)(rows * ch) : 0);
	zwlr_layer_surface_v1_set_anchor(W.layer, anchor);
	zwlr_layer_surface_v1_set_margin(W.layer, W.margin[0] * ch,
					 W.margin[1] * cw, W.margin[2] * ch,
					 W.margin[3] * cw);

	zone = W.exclusive < 0 ? -1 : W.exclusive * ch;
	zwlr_layer_surface_v1_set_exclusive_zone(W.layer, zone);

	/* A popup takes the keyboard. A bar that reserves space does not. */
	zwlr_layer_surface_v1_set_keyboard_interactivity(W.layer,
							 W.exclusive < 0 ? 1 : 0);
	wl_surface_set_buffer_scale(W.surface, W.scale);

	/* Commit an empty surface. The buffer waits for the configure event. */
	wl_surface_commit(W.surface);
	wl_display_roundtrip(W.display);

	if (!W.configured) {
		fail("no configure event");
		return -1;
	}
	return 0;
}

void wl_stop(void)
{
	input_stop();
	drop_buffers();

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
