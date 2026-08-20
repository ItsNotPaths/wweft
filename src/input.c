/* Keyboard. Build step 1 has no xkbcommon: any key stops the program. */
#include <unistd.h>

#include <wayland-client.h>

#include "wweft.h"

static struct wl_keyboard *keyboard;

static void on_keymap(void *data, struct wl_keyboard *kb, uint32_t format,
		      int32_t fd, uint32_t size)
{
	(void)data;
	(void)kb;
	(void)format;
	(void)size;
	close(fd);   /* step 3 maps this and gives it to xkbcommon */
}

static void on_enter(void *data, struct wl_keyboard *kb, uint32_t serial,
		     struct wl_surface *surface, struct wl_array *keys)
{
	(void)data; (void)kb; (void)serial; (void)surface; (void)keys;
}

static void on_leave(void *data, struct wl_keyboard *kb, uint32_t serial,
		     struct wl_surface *surface)
{
	(void)data; (void)kb; (void)serial; (void)surface;
}

static void on_key(void *data, struct wl_keyboard *kb, uint32_t serial,
		   uint32_t time, uint32_t key, uint32_t state)
{
	(void)data; (void)kb; (void)serial; (void)time; (void)key;

	if (state == WL_KEYBOARD_KEY_STATE_PRESSED)
		loop_quit(0);
}

static void on_modifiers(void *data, struct wl_keyboard *kb, uint32_t serial,
			 uint32_t depressed, uint32_t latched, uint32_t locked,
			 uint32_t group)
{
	(void)data; (void)kb; (void)serial;
	(void)depressed; (void)latched; (void)locked; (void)group;
}

static void on_repeat_info(void *data, struct wl_keyboard *kb, int32_t rate,
			   int32_t delay)
{
	(void)data; (void)kb; (void)rate; (void)delay;
}

static const struct wl_keyboard_listener keyboard_listener = {
	on_keymap, on_enter, on_leave, on_key, on_modifiers, on_repeat_info
};

static void on_capabilities(void *data, struct wl_seat *seat, uint32_t caps)
{
	(void)data;

	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !keyboard) {
		keyboard = wl_seat_get_keyboard(seat);
		wl_keyboard_add_listener(keyboard, &keyboard_listener, NULL);
	}
}

static void on_name(void *data, struct wl_seat *seat, const char *name)
{
	(void)data; (void)seat; (void)name;
}

static const struct wl_seat_listener seat_listener = {
	on_capabilities, on_name
};

void input_bind_seat(struct wl_seat *seat)
{
	wl_seat_add_listener(seat, &seat_listener, NULL);
}

void input_stop(void)
{
	if (keyboard) {
		wl_keyboard_release(keyboard);
		keyboard = NULL;
	}
}
