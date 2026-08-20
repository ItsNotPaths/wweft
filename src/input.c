/* xkbcommon: key names, modifier prefixes, and key repeat on a timerfd. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "wweft.h"

#define NAME_MAX_LEN 96

static struct {
	struct wl_keyboard *keyboard;
	struct xkb_context *ctx;
	struct xkb_keymap *keymap;
	struct xkb_state *state;

	int timer;                /* timerfd for key repeat */
	int rate;                 /* keys in one second */
	int delay;                /* milliseconds before the first repeat */
	uint32_t repeat_key;      /* the raw key that repeats */
	char repeat_name[NAME_MAX_LEN];
} I = { .timer = -1 };

/* ------------------------------------------------------------- key names */

static int is_modifier_key(xkb_keysym_t sym)
{
	switch (sym) {
	case XKB_KEY_Shift_L: case XKB_KEY_Shift_R:
	case XKB_KEY_Control_L: case XKB_KEY_Control_R:
	case XKB_KEY_Alt_L: case XKB_KEY_Alt_R:
	case XKB_KEY_Super_L: case XKB_KEY_Super_R:
	case XKB_KEY_Meta_L: case XKB_KEY_Meta_R:
	case XKB_KEY_Caps_Lock: case XKB_KEY_Num_Lock:
	case XKB_KEY_ISO_Level3_Shift: case XKB_KEY_ISO_Level5_Shift:
		return 1;
	default:
		return 0;
	}
}

static int mod_on(const char *name)
{
	return xkb_state_mod_name_is_active(I.state, name,
					    XKB_STATE_MODS_EFFECTIVE) > 0;
}

/* "Escape", "a", "A", "Ctrl+Left", "Ctrl+Alt+Delete" */
static int key_name(xkb_keycode_t code, char *out, size_t size)
{
	xkb_keysym_t sym = xkb_state_key_get_one_sym(I.state, code);
	char base[64];
	char text[8];
	int printable;

	if (sym == XKB_KEY_NoSymbol || is_modifier_key(sym))
		return -1;

	if (xkb_keysym_get_name(sym, base, sizeof base) < 0)
		return -1;

	/* A printable key already carries the shift, as in "A". */
	printable = xkb_state_key_get_utf8(I.state, code, text, sizeof text) == 1
		    && (unsigned char)text[0] >= 0x20;

	out[0] = 0;
	if (mod_on(XKB_MOD_NAME_CTRL))
		strncat(out, "Ctrl+", size - strlen(out) - 1);
	if (mod_on(XKB_MOD_NAME_ALT))
		strncat(out, "Alt+", size - strlen(out) - 1);
	if (mod_on(XKB_MOD_NAME_LOGO))
		strncat(out, "Super+", size - strlen(out) - 1);
	if (!printable && mod_on(XKB_MOD_NAME_SHIFT))
		strncat(out, "Shift+", size - strlen(out) - 1);
	strncat(out, base, size - strlen(out) - 1);
	return 0;
}

/* ----------------------------------------------------------- key repeat */

static void timer_set(int first_ms, int every_ms)
{
	struct itimerspec ts = {0};

	if (I.timer < 0)
		return;

	ts.it_value.tv_sec = first_ms / 1000;
	ts.it_value.tv_nsec = (long)(first_ms % 1000) * 1000000L;
	ts.it_interval.tv_sec = every_ms / 1000;
	ts.it_interval.tv_nsec = (long)(every_ms % 1000) * 1000000L;
	timerfd_settime(I.timer, 0, &ts, NULL);
}

static void repeat_stop(void)
{
	I.repeat_key = 0;
	timer_set(0, 0);
}

static void repeat_start(xkb_keycode_t code, uint32_t raw, const char *name)
{
	if (I.rate <= 0 || !I.keymap || !xkb_keymap_key_repeats(I.keymap, code))
		return;

	I.repeat_key = raw + 1;   /* +1 so that 0 stays "nothing repeats" */
	snprintf(I.repeat_name, sizeof I.repeat_name, "%s", name);
	timer_set(I.delay, 1000 / I.rate);
}

int input_timer_fd(void)
{
	return I.timer;
}

void input_timer_fire(void)
{
	uint64_t ticks;

	if (I.timer < 0 || read(I.timer, &ticks, sizeof ticks) != sizeof ticks)
		return;
	if (!I.repeat_key)
		return;

	if (app_on_key(I.repeat_name))
		wl_redraw();
}

/* ------------------------------------------------------------- listeners */

static void on_keymap(void *data, struct wl_keyboard *kb, uint32_t format,
		      int32_t fd, uint32_t size)
{
	char *text;

	(void)data;
	(void)kb;

	if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
		close(fd);
		return;
	}

	text = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (text == MAP_FAILED)
		return;

	repeat_stop();
	xkb_state_unref(I.state);
	xkb_keymap_unref(I.keymap);

	I.keymap = xkb_keymap_new_from_string(I.ctx, text,
					      XKB_KEYMAP_FORMAT_TEXT_V1,
					      XKB_KEYMAP_COMPILE_NO_FLAGS);
	munmap(text, size);

	I.state = I.keymap ? xkb_state_new(I.keymap) : NULL;

	if (getenv("WWEFT_DEBUG"))
		fprintf(stderr, "keymap: %s\n", I.state ? "ok" : "failed");
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
	repeat_stop();
}

static void on_key(void *data, struct wl_keyboard *kb, uint32_t serial,
		   uint32_t time, uint32_t key, uint32_t state)
{
	char name[NAME_MAX_LEN];
	xkb_keycode_t code = key + 8;   /* evdev to xkb */

	(void)data; (void)kb; (void)serial; (void)time;

	if (!I.state)
		return;

	if (state == WL_KEYBOARD_KEY_STATE_RELEASED) {
		if (I.repeat_key == key + 1)
			repeat_stop();
		return;
	}

	repeat_stop();

	if (key_name(code, name, sizeof name) < 0) {
		if (getenv("WWEFT_DEBUG"))
			fprintf(stderr, "key: <no name> raw=%u\n", key);
		return;
	}

	repeat_start(code, key, name);

	if (app_on_key(name))
		wl_redraw();
}

static void on_modifiers(void *data, struct wl_keyboard *kb, uint32_t serial,
			 uint32_t depressed, uint32_t latched, uint32_t locked,
			 uint32_t group)
{
	(void)data; (void)kb; (void)serial;

	if (getenv("WWEFT_DEBUG"))
		fprintf(stderr, "mods: %u %u %u group %u\n",
			depressed, latched, locked, group);

	if (I.state)
		xkb_state_update_mask(I.state, depressed, latched, locked,
				      0, 0, group);
}

static void on_repeat_info(void *data, struct wl_keyboard *kb, int32_t rate,
			   int32_t delay)
{
	(void)data; (void)kb;

	I.rate = rate;
	I.delay = delay;
}

static const struct wl_keyboard_listener keyboard_listener = {
	on_keymap, on_enter, on_leave, on_key, on_modifiers, on_repeat_info
};

static void on_capabilities(void *data, struct wl_seat *seat, uint32_t caps)
{
	(void)data;

	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !I.keyboard) {
		I.keyboard = wl_seat_get_keyboard(seat);
		wl_keyboard_add_listener(I.keyboard, &keyboard_listener, NULL);
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
	I.ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	I.timer = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
	I.rate = 25;
	I.delay = 600;

	wl_seat_add_listener(seat, &seat_listener, NULL);
}

void input_stop(void)
{
	if (I.timer >= 0)
		close(I.timer);
	if (I.keyboard)
		wl_keyboard_release(I.keyboard);
	xkb_state_unref(I.state);
	xkb_keymap_unref(I.keymap);
	xkb_context_unref(I.ctx);
	memset(&I, 0, sizeof I);
	I.timer = -1;
}
