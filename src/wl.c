/* Wayland: layer surface, shm buffers, damage. No font, no VM. */
#define _GNU_SOURCE
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
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
	int width;
	int height;
	bool configured;
} W;

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

static int make_buffers(void)
{
	int stride = W.width * 4;
	size_t one = (size_t)stride * W.height;
	struct wl_shm_pool *pool;
	int fd;
	int i;

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

static void draw(void)
{
	struct buffer *b = free_buffer();

	if (!b || !W.configured)
		return;

	render_frame(b->pixels, W.width, W.height);

	b->busy = true;
	wl_surface_attach(W.surface, b->wl, 0, 0);
	wl_surface_damage_buffer(W.surface, 0, 0, W.width, W.height);
	wl_surface_commit(W.surface);
}

/* ------------------------------------------------------- layer surface */

static void on_configure(void *data, struct zwlr_layer_surface_v1 *ls,
			 uint32_t serial, uint32_t width, uint32_t height)
{
	(void)data;

	zwlr_layer_surface_v1_ack_configure(ls, serial);

	if (width > 0)
		W.width = (int)width;
	if (height > 0)
		W.height = (int)height;

	if (!W.configured) {
		if (make_buffers() < 0) {
			loop_quit(1);
			return;
		}
		W.configured = true;
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
	} else if (strcmp(iface, zwlr_layer_shell_v1_interface.name) == 0) {
		W.shell = wl_registry_bind(reg, name,
					   &zwlr_layer_shell_v1_interface,
					   pick(version, 4));
	}
}

static void on_global_remove(void *data, struct wl_registry *reg, uint32_t name)
{
	(void)data;
	(void)reg;
	(void)name;
}

static const struct wl_registry_listener registry_listener = {
	on_global, on_global_remove
};

/* ---------------------------------------------------------------- start */

int wl_start(int width, int height)
{
	W.width = width;
	W.height = height;

	W.display = wl_display_connect(NULL);
	if (!W.display) {
		fail("no Wayland display. Is WAYLAND_DISPLAY set?");
		return -1;
	}

	W.registry = wl_display_get_registry(W.display);
	wl_registry_add_listener(W.registry, &registry_listener, NULL);
	wl_display_roundtrip(W.display);

	if (!W.compositor || !W.shm || !W.shell) {
		fail("the compositor has no wlr-layer-shell support");
		return -1;
	}

	if (W.seat)
		input_bind_seat(W.seat);

	W.surface = wl_compositor_create_surface(W.compositor);
	W.layer = zwlr_layer_shell_v1_get_layer_surface(
		W.shell, W.surface, NULL,
		ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "wweft");
	zwlr_layer_surface_v1_add_listener(W.layer, &layer_listener, NULL);

	zwlr_layer_surface_v1_set_size(W.layer, (uint32_t)W.width,
				       (uint32_t)W.height);
	zwlr_layer_surface_v1_set_anchor(W.layer, 0);   /* no anchor = centre */
	zwlr_layer_surface_v1_set_exclusive_zone(W.layer, -1);
	zwlr_layer_surface_v1_set_keyboard_interactivity(W.layer, 1);

	/* Commit an empty surface. The buffer waits for the configure event. */
	wl_surface_commit(W.surface);
	wl_display_roundtrip(W.display);

	if (!W.configured) {
		fail("no configure event");
		return -1;
	}
	return 0;
}

void wl_redraw(void)
{
	draw();
}

void wl_stop(void)
{
	int i;

	input_stop();

	for (i = 0; i < 2; i++)
		if (W.buffers[i].wl)
			wl_buffer_destroy(W.buffers[i].wl);
	if (W.pool_data)
		munmap(W.pool_data, W.pool_size);
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
