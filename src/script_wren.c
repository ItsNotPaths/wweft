/* The Wren VM. This is the only file that includes wren.h. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "wren.h"
#include "wweft.h"

#define PATH_LEN 1024
#define SH_TIMEOUT_MS 2000

static struct {
	WrenVM *vm;
	WrenHandle *app;        /* the object given to Surface.run */
	WrenHandle *on_key;     /* the call handle for onKey(_) */
	WrenHandle *on_draw;
	WrenHandle *on_tick;
	WrenHandle *on_message;
	WrenHandle *on_change;
	int cols, rows;         /* what Surface.window asked for */
	int font_done;
	char font_path[PATH_LEN];
	int font_px;
	int dismiss;
	int border_on;
	int border_style;
	char border_chars[32];   /* six code points, UTF-8 */
	int argc;                /* what came after the script path */
	char **argv;            /* close when the keyboard focus goes away */
	size_t bytes;           /* live bytes in the Wren heap */
	char dir[PATH_LEN];     /* the directory of the script */
} S;

/* ------------------------------------------------------- embedded module */

static const char *module_src =
"class Surface {\n"
"  foreign static font(path, size)\n"
"  foreign static window(cols, rows)\n"
"  foreign static anchor(spec)\n"
"  foreign static margin(t, r, b, l)\n"
"  foreign static layer(name)\n"
"  foreign static scale(n)\n"
"  foreign static exclusive(cells)\n"
"  foreign static dismiss(flag)\n"
"  foreign static every(ms)\n"
"  foreign static lifetime(ms)\n"
"  foreign static listen(spec)\n"
"  foreign static watch(path)\n"
"  foreign static output(name)\n"
"  foreign static borderSet(chars, style)\n"
"  foreign static close(code)\n"
"  foreign static emit(text)\n"
"  foreign static spawn(cmd)\n"
"  foreign static read(path)\n"
"  foreign static shWait(cmd, ms)\n"
"  foreign static run(object)\n"
"\n"
// @api Surface.sh(cmd[, ms]) -> [out, rc]  popen with a deadline
"  static border(chars) { borderSet(chars, 0) }\n"
"  static border(chars, style) { borderSet(chars, style) }\n"
"  static sh(cmd) { shWait(cmd, 2000) }\n"
"  static sh(cmd, ms) { shWait(cmd, ms) }\n"
"\n"
"  // The output of a command, one item for each line, empty lines dropped.\n"
// @api Surface.lines(cmd) -> list          output split, empty lines dropped
"  static lines(cmd) {\n"
"    var out = []\n"
"    for (line in sh(cmd)[0].split(\"\\n\")) {\n"
"      if (line.trim() != \"\") out.add(line)\n"
"    }\n"
"    return out\n"
"  }\n"
"}\n"
"\n"
"class Grid {\n"
"  foreign static text(x, y, str, style)\n"
"  foreign static fill(x, y, w, h, style)\n"
"  foreign static width(str)\n"
"  foreign static cols\n"
"  foreign static rows\n"
"\n"
// @api Grid.center(y, str, style)
"  static center(y, str, style) {\n"
"    text(((cols - width(str)) / 2).floor, y, str, style)\n"
"  }\n"
"\n"
"  // Draw a row of items and give back the column of each one.\n"
// @api Grid.row(x, y, items, gap, style, selStyle, sel) -> columns
"  static row(x, y, items, gap, style, selStyle, sel) {\n"
"    var at = []\n"
"    for (i in 0...items.count) {\n"
"      at.add(x)\n"
"      text(x, y, items[i], i == sel ? selStyle : style)\n"
"      x = x + width(items[i]) + gap\n"
"    }\n"
"    return at\n"
"  }\n"
"}\n"
"\n"
"class Style {\n"
"  foreign static define(fg, bg)\n"
"\n"
"  static setup_() {\n"
"    __base = 0\n"
"    __title = define(0xff7fbfff, 0xee151515)\n"
"    __item = define(0xffcccccc, 0xee151515)\n"
"    __sel = define(0xff151515, 0xff7fbfff)\n"
"    __dim = define(0xff707070, 0xee151515)\n"
"  }\n"
// @api Style.base, title, item, sel, dim   ready made style ids
"  static base { __base }\n"
"  static title { __title }\n"
"  static item { __item }\n"
"  static sel { __sel }\n"
"  static dim { __dim }\n"
"}\n"
"Style.setup_()\n"
"\n"
"class Sys {\n"
"  foreign static time\n"
"  foreign static strftime(format)\n"
"  foreign static env(name)\n"
"  foreign static args\n"
"}\n"
"\n"
"class Text {\n"
"  // ASCII only. Wren carries no Unicode case tables.\n"
// @api Text.lower(s) -> str                ASCII only
"  static lower(s) {\n"
"    var out = \"\"\n"
"    for (c in s.codePoints) {\n"
"      out = out + String.fromCodePoint(c >= 65 && c <= 90 ? c + 32 : c)\n"
"    }\n"
"    return out\n"
"  }\n"
"\n"
// @api Text.contains(a, b) -> bool         case insensitive
"  static contains(haystack, needle) {\n"
"    return lower(haystack).contains(lower(needle))\n"
"  }\n"
"\n"
"  // Safe to put inside a shell command. An apostrophe in a file name\n"
"  // breaks the command without this.\n"
// @api Text.quote(s) -> str                safe inside a shell command
"  static quote(s) {\n"
"    return \"'\" + s.replace(\"'\", \"'\\\\''\") + \"'\"\n"
"  }\n"
"}\n"
"\n"
"class Key {\n"
// @api Key.left, right, up, down, enter, escape, tab, space, backspace, delete, home, end
"  static left { \"Left\" }\n"
"  static right { \"Right\" }\n"
"  static up { \"Up\" }\n"
"  static down { \"Down\" }\n"
"  static enter { \"Return\" }\n"
"  static escape { \"Escape\" }\n"
"  static tab { \"Tab\" }\n"
"  foreign static text\n"
"  static space { \"space\" }\n"
"  static backspace { \"BackSpace\" }\n"
"  static delete { \"Delete\" }\n"
"  static home { \"Home\" }\n"
"  static end { \"End\" }\n"
"}\n";

/* ------------------------------------------------------------- utilities */

/* Put ~ back to the home directory. The result is in out. */
static void expand_home(const char *in, char *out, size_t size)
{
	const char *home = getenv("HOME");

	if (in[0] == '~' && in[1] == '/' && home)
		snprintf(out, size, "%s%s", home, in + 1);
	else
		snprintf(out, size, "%s", in);
}

/* Read every byte. A pipe and a /proc file have no size, so this grows the
 * buffer instead of asking how long the file is. */
static char *read_whole(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	char *data = NULL;
	size_t used = 0;
	size_t room = 0;

	if (!f)
		return NULL;

	for (;;) {
		size_t got;

		if (used + 4096 + 1 > room) {
			char *bigger;

			room = room ? room * 2 : 8192;
			bigger = realloc(data, room);
			if (!bigger) {
				free(data);
				fclose(f);
				return NULL;
			}
			data = bigger;
		}

		got = fread(data + used, 1, 4096, f);
		used += got;
		if (got < 4096)
			break;
	}

	fclose(f);
	if (!data)
		data = calloc(1, 1);
	else
		data[used] = 0;

	if (len)
		*len = used;
	return data;
}

/* ------------------------------------------------------- foreign methods */

/* Records only. main.c opens the font, and reopens it on a scale change. */
static void f_font(WrenVM *vm)
{
	const char *path = wrenGetSlotString(vm, 1);
	int size = (int)wrenGetSlotDouble(vm, 2);

	snprintf(S.font_path, sizeof S.font_path, "%s", path ? path : "");
	S.font_px = size > 0 ? size : 16;
	S.font_done = 1;
}

static void f_window(WrenVM *vm)
{
	S.cols = (int)wrenGetSlotDouble(vm, 1);
	S.rows = (int)wrenGetSlotDouble(vm, 2);
}

static void f_anchor(WrenVM *vm)
{
	wl_set_anchor(wrenGetSlotString(vm, 1));
}

static void f_margin(WrenVM *vm)
{
	wl_set_margin((int)wrenGetSlotDouble(vm, 1), (int)wrenGetSlotDouble(vm, 2),
		      (int)wrenGetSlotDouble(vm, 3), (int)wrenGetSlotDouble(vm, 4));
}

static void f_layer(WrenVM *vm)
{
	wl_set_layer(wrenGetSlotString(vm, 1));
}

static void f_scale(WrenVM *vm)
{
	wl_set_scale((int)wrenGetSlotDouble(vm, 1));
}

static void f_exclusive(WrenVM *vm)
{
	wl_set_exclusive((int)wrenGetSlotDouble(vm, 1));
}

static void f_dismiss(WrenVM *vm)
{
	S.dismiss = wrenGetSlotBool(vm, 1) ? 1 : 0;
}

/* A name, or six code points: corners clockwise from top left, then the
 * horizontal, then the vertical. */
static void f_border(WrenVM *vm)
{
	static const struct {
		const char *name;
		const char *chars;
	} sets[] = {
		{ "line",   "\u250c\u2510\u2518\u2514\u2500\u2502" },
		{ "round",  "\u256d\u256e\u256f\u2570\u2500\u2502" },
		{ "double", "\u2554\u2557\u255d\u255a\u2550\u2551" },
		{ "heavy",  "\u250f\u2513\u251b\u2517\u2501\u2503" },
		{ "ascii",  "++++-|" },
		{ "block",  "\u2588\u2588\u2588\u2588\u2588\u2588" },
	};
	const char *spec = wrenGetSlotString(vm, 1);
	size_t i;

	S.border_style = (int)wrenGetSlotDouble(vm, 2);
	S.border_on = 1;

	for (i = 0; i < sizeof sets / sizeof *sets; i++) {
		if (strcmp(spec, sets[i].name) == 0) {
			snprintf(S.border_chars, sizeof S.border_chars, "%s",
				 sets[i].chars);
			return;
		}
	}
	snprintf(S.border_chars, sizeof S.border_chars, "%s", spec);
}

static void f_every(WrenVM *vm)
{
	loop_every((int)wrenGetSlotDouble(vm, 1));
}

static void f_lifetime(WrenVM *vm)
{
	loop_lifetime((int)wrenGetSlotDouble(vm, 1));
}

static void f_args(WrenVM *vm)
{
	int i;

	wrenEnsureSlots(vm, 2);
	wrenSetSlotNewList(vm, 0);

	for (i = 0; i < S.argc; i++) {
		wrenSetSlotString(vm, 1, S.argv[i]);
		wrenInsertInList(vm, 0, -1, 1);
	}
}

static void f_listen(WrenVM *vm)
{
	msg_listen(wrenGetSlotString(vm, 1));
}

static void f_watch(WrenVM *vm)
{
	msg_watch(wrenGetSlotString(vm, 1));
}

static void f_output(WrenVM *vm)
{
	wl_set_output(wrenGetSlotString(vm, 1));
}

static void f_time(WrenVM *vm)
{
	struct timespec ts;

	clock_gettime(CLOCK_REALTIME, &ts);
	wrenSetSlotDouble(vm, 0, (double)ts.tv_sec + (double)ts.tv_nsec / 1e9);
}

static void f_strftime(WrenVM *vm)
{
	const char *format = wrenGetSlotString(vm, 1);
	char out[256];
	time_t now = time(NULL);
	struct tm tm;

	localtime_r(&now, &tm);
	if (strftime(out, sizeof out, format, &tm) == 0)
		out[0] = 0;

	wrenSetSlotString(vm, 0, out);
}

static void f_env(WrenVM *vm)
{
	const char *value = getenv(wrenGetSlotString(vm, 1));

	if (value)
		wrenSetSlotString(vm, 0, value);
	else
		wrenSetSlotNull(vm, 0);
}

static void f_close(WrenVM *vm)
{
	loop_quit((int)wrenGetSlotDouble(vm, 1));
}

static void f_emit(WrenVM *vm)
{
	fputs(wrenGetSlotString(vm, 1), stdout);
	fputc('\n', stdout);
	fflush(stdout);
}

/* Two forks: the middle child exits, so the command reparents to init and
 * leaves no zombie. CAUTION: SIGCHLD as SIG_IGN would also do that, but it
 * breaks waitpid, and Surface.sh needs the exit status. */
static void f_spawn(WrenVM *vm)
{
	const char *cmd = wrenGetSlotString(vm, 1);
	pid_t pid = fork();

	if (pid == 0) {
		setsid();
		if (fork() == 0) {
			execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
			_exit(127);
		}
		_exit(0);
	}
	if (pid > 0)
		waitpid(pid, NULL, 0);
}

static void f_read(WrenVM *vm)
{
	char path[PATH_LEN];
	char *data;

	expand_home(wrenGetSlotString(vm, 1), path, sizeof path);
	data = read_whole(path, NULL);

	if (!data) {
		wrenSetSlotNull(vm, 0);
		return;
	}
	wrenSetSlotString(vm, 0, data);
	free(data);
}

/* Run a command and capture stdout. It kills the child at the deadline. */
static void f_sh(WrenVM *vm)
{
	const char *cmd = wrenGetSlotString(vm, 1);
	int limit_ms = (int)wrenGetSlotDouble(vm, 2);
	int fd[2];
	pid_t pid;
	char *out = NULL;
	size_t len = 0;
	int rc = -1;
	int status;

	if (limit_ms < 1)
		limit_ms = SH_TIMEOUT_MS;

	if (pipe(fd) < 0) {
		wrenSetSlotNewList(vm, 0);
		return;
	}

	pid = fork();
	if (pid == 0) {
		close(fd[0]);
		dup2(fd[1], STDOUT_FILENO);
		close(fd[1]);
		execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
		_exit(127);
	}
	close(fd[1]);

	if (pid > 0) {
		struct pollfd p = { .fd = fd[0], .events = POLLIN };

		for (;;) {
			char buf[4096];
			ssize_t n;
			int ready = poll(&p, 1, limit_ms);

			if (ready <= 0) {            /* deadline or error */
				kill(pid, SIGKILL);
				break;
			}
			n = read(fd[0], buf, sizeof buf);
			if (n <= 0)
				break;

			out = realloc(out, len + (size_t)n + 1);
			memcpy(out + len, buf, (size_t)n);
			len += (size_t)n;
			out[len] = 0;
		}
		waitpid(pid, &status, 0);
		rc = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
	}
	close(fd[0]);

	wrenEnsureSlots(vm, 2);
	wrenSetSlotNewList(vm, 0);
	wrenSetSlotString(vm, 1, out ? out : "");
	wrenInsertInList(vm, 0, -1, 1);
	wrenSetSlotDouble(vm, 1, rc);
	wrenInsertInList(vm, 0, -1, 1);
	free(out);
}

static void f_run(WrenVM *vm)
{
	if (S.app)
		wrenReleaseHandle(vm, S.app);
	S.app = wrenGetSlotHandle(vm, 1);
}

static void f_text(WrenVM *vm)
{
	grid_text((int)wrenGetSlotDouble(vm, 1), (int)wrenGetSlotDouble(vm, 2),
		  wrenGetSlotString(vm, 3), (int)wrenGetSlotDouble(vm, 4));
}

static void f_fill(WrenVM *vm)
{
	grid_fill((int)wrenGetSlotDouble(vm, 1), (int)wrenGetSlotDouble(vm, 2),
		  (int)wrenGetSlotDouble(vm, 3), (int)wrenGetSlotDouble(vm, 4),
		  (int)wrenGetSlotDouble(vm, 5));
}

static void f_width(WrenVM *vm)
{
	wrenSetSlotDouble(vm, 0, grid_str_w(wrenGetSlotString(vm, 1)));
}

static void f_cols(WrenVM *vm)
{
	wrenSetSlotDouble(vm, 0, grid_cols());
}

static void f_rows(WrenVM *vm)
{
	wrenSetSlotDouble(vm, 0, grid_rows());
}

static void f_key_text(WrenVM *vm)
{
	wrenSetSlotString(vm, 0, input_key_text());
}

static void f_define(WrenVM *vm)
{
	uint32_t fg = (uint32_t)wrenGetSlotDouble(vm, 1);
	uint32_t bg = (uint32_t)wrenGetSlotDouble(vm, 2);
	int id = grid_add_style(fg, bg);

	wrenSetSlotDouble(vm, 0, id < 0 ? 0 : id);
}

/* -------------------------------------------------------------- binding */

struct entry {
	const char *class_name;
	const char *signature;
	WrenForeignMethodFn fn;
};

/* CAUTION: a signature that does not match exactly fails at bind time, and
 * the error does not say which one. */
static const struct entry methods[] = {
	// @api Surface.font(path, px)              "" searches $WWEFT_FONT, fc-match, built-in
	{ "Surface", "font(_,_)",       f_font },
	// @api Surface.window(cols, rows)          0 on an axis fills the output
	{ "Surface", "window(_,_)",     f_window },
	// @api Surface.anchor(spec)                "center", "top", "bottom-left", ...
	{ "Surface", "anchor(_)",       f_anchor },
	// @api Surface.margin(t, r, b, l)          cells
	{ "Surface", "margin(_,_,_,_)", f_margin },
	// @api Surface.layer(name)                 "overlay", "top", "bottom", "background"
	{ "Surface", "layer(_)",        f_layer },
	// @api Surface.scale(n)                    0 follows the output
	{ "Surface", "scale(_)",        f_scale },
	// @api Surface.exclusive(cells)            -1 popup, 0 none, n reserves n rows
	{ "Surface", "exclusive(_)",    f_exclusive },
	// @api Surface.dismiss(flag)               close when the focus leaves. Default true
	{ "Surface", "dismiss(_)",      f_dismiss },
	// @api Surface.border(chars[, style])      "line" "round" "double" "heavy" "ascii" "block", or six of your own. The window grows one cell on every side
	{ "Surface", "borderSet(_,_)",  f_border },
	// @api Surface.every(ms)                   0 stops it. onTick() is called
	{ "Surface", "every(_)",        f_every },
	// @api Surface.lifetime(ms)                quit after this long. Any call resets it
	{ "Surface", "lifetime(_)",     f_lifetime },
	// @api Surface.listen(spec)                a name, or a unix socket path
	{ "Surface", "listen(_)",       f_listen },
	// @api Surface.watch(path)                 a file another program writes. onChange(path)
	{ "Surface", "watch(_)",        f_watch },
	// @api Surface.output(name)                which monitor, as in "eDP-1"
	{ "Surface", "output(_)",       f_output },
	// @api Sys.time -> Num                     seconds since the epoch
	{ "Sys",     "time",            f_time },
	// @api Sys.strftime(fmt) -> str            local time, as in "%H:%M"
	{ "Sys",     "strftime(_)",     f_strftime },
	// @api Sys.env(name) -> str                or null
	{ "Sys",     "env(_)",          f_env },
	// @api Sys.args -> list                    what came after the script path
	{ "Sys",     "args",            f_args },
	// @api Surface.close(code)                 exit with this code
	{ "Surface", "close(_)",        f_close },
	// @api Surface.emit(text)                  one line to stdout
	{ "Surface", "emit(_)",         f_emit },
	// @api Surface.spawn(cmd)                  never blocks, never waits
	{ "Surface", "spawn(_)",        f_spawn },
	// @api Surface.read(path) -> str           whole file or null. A leading ~ expands
	{ "Surface", "read(_)",         f_read },
	{ "Surface", "shWait(_,_)",     f_sh },
	// @api Surface.run(object)                 the object gets onDraw, onKey, onTick, onMessage
	// @api onDraw(grid)                        fill the cells. Called before every frame
	// @api onKey(name) -> bool                 false means not handled, no redraw
	// @api onTick()                            from Surface.every
	// @api onMessage(line)                     from Surface.listen
	// @api onChange(path)                      from Surface.watch
	{ "Surface", "run(_)",          f_run },
	// @api Grid.text(x, y, str, style)
	{ "Grid",    "text(_,_,_,_)",   f_text },
	// @api Grid.fill(x, y, w, h, style)
	{ "Grid",    "fill(_,_,_,_,_)", f_fill },
	// @api Grid.width(str) -> columns          display columns, not code points
	{ "Grid",    "width(_)",        f_width },
	// @api Grid.cols, Grid.rows                the size the compositor gave
	{ "Grid",    "cols",            f_cols },
	{ "Grid",    "rows",            f_rows },
	// @api Key.text -> str                     what the key typed, "" for Escape
	{ "Key",     "text",            f_key_text },
	// @api Style.define(fg, bg) -> id          0xAARRGGBB, 32 slots
	{ "Style",   "define(_,_)",     f_define },
};

static WrenForeignMethodFn bind_method(WrenVM *vm, const char *module,
				       const char *class_name, bool is_static,
				       const char *signature)
{
	size_t i;

	(void)vm;
	if (strcmp(module, "wweft") != 0 || !is_static)
		return NULL;

	for (i = 0; i < sizeof methods / sizeof *methods; i++)
		if (strcmp(class_name, methods[i].class_name) == 0 &&
		    strcmp(signature, methods[i].signature) == 0)
			return methods[i].fn;

	return NULL;
}

/* -------------------------------------------------------------- modules */

static void free_source(WrenVM *vm, const char *name, struct WrenLoadModuleResult result)
{
	(void)vm;
	(void)name;
	free((void *)result.source);
}

static char *find_module(const char *name)
{
	const char *xdg = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");
	char path[PATH_LEN * 2];
	char *data;

	if (S.dir[0]) {
		snprintf(path, sizeof path, "%s/%.200s.wren", S.dir, name);
		if ((data = read_whole(path, NULL)))
			return data;
	}
	if (xdg && *xdg) {
		snprintf(path, sizeof path, "%s/wweft/%.200s.wren", xdg, name);
		if ((data = read_whole(path, NULL)))
			return data;
	}
	if (home) {
		snprintf(path, sizeof path, "%s/.config/wweft/%.200s.wren", home, name);
		if ((data = read_whole(path, NULL)))
			return data;
	}
	return NULL;
}

static WrenLoadModuleResult load_module(WrenVM *vm, const char *name)
{
	WrenLoadModuleResult result = {0};

	(void)vm;

	if (strcmp(name, "wweft") == 0) {
		result.source = module_src;   /* static, so no onComplete */
		return result;
	}

	result.source = find_module(name);
	if (result.source)
		result.onComplete = free_source;
	return result;
}

/* ------------------------------------------------------------ callbacks */

static void on_write(WrenVM *vm, const char *text)
{
	(void)vm;
	fputs(text, stdout);
}

static void on_error(WrenVM *vm, WrenErrorType type, const char *module,
		     int line, const char *message)
{
	(void)vm;

	switch (type) {
	case WREN_ERROR_COMPILE:
		fprintf(stderr, "wweft: %s line %d: %s\n", module, line, message);
		break;
	case WREN_ERROR_RUNTIME:
		fprintf(stderr, "wweft: %s\n", message);
		break;
	case WREN_ERROR_STACK_TRACE:
		fprintf(stderr, "  at %s line %d: %s\n", module, line, message);
		break;
	}
}

/* Counts the live bytes, so that the memory report is exact. */
static void *count_alloc(void *ptr, size_t size, void *user)
{
	(void)user;

	if (ptr)
		S.bytes -= malloc_usable_size(ptr);

	if (size == 0) {
		free(ptr);
		return NULL;
	}

	ptr = realloc(ptr, size);
	if (ptr)
		S.bytes += malloc_usable_size(ptr);
	return ptr;
}

/* ------------------------------------------------------------------ api */

int script_init(const char *path)
{
	WrenConfiguration config;
	char *source;
	const char *slash;

	slash = strrchr(path, '/');
	if (slash) {
		size_t n = (size_t)(slash - path);
		if (n >= sizeof S.dir)
			n = sizeof S.dir - 1;
		memcpy(S.dir, path, n);
		S.dir[n] = 0;
	}

	source = read_whole(path, NULL);
	if (!source) {
		fprintf(stderr, "wweft: cannot read %s\n", path);
		return -1;
	}

	wrenInitConfiguration(&config);
	config.reallocateFn = count_alloc;
	config.writeFn = on_write;
	config.errorFn = on_error;
	config.loadModuleFn = load_module;
	config.bindForeignMethodFn = bind_method;

	/* The default first collection is at 10 MB. A popup never reaches it,
	 * and a script that runs for hours should collect sooner. */
	config.initialHeapSize = 1024 * 1024;
	config.minHeapSize = 256 * 1024;

	S.dismiss = 1;   /* a popup goes away when the focus does */
	S.vm = wrenNewVM(&config);
	if (!S.vm) {
		free(source);
		return -1;
	}

	if (wrenInterpret(S.vm, "main", source) != WREN_RESULT_SUCCESS) {
		free(source);
		return -1;
	}
	free(source);

	if (!S.app) {
		fprintf(stderr, "wweft: the script never called Surface.run\n");
		return -1;
	}

	S.on_key = wrenMakeCallHandle(S.vm, "onKey(_)");
	S.on_draw = wrenMakeCallHandle(S.vm, "onDraw(_)");
	S.on_tick = wrenMakeCallHandle(S.vm, "onTick()");
	S.on_message = wrenMakeCallHandle(S.vm, "onMessage(_)");
	S.on_change = wrenMakeCallHandle(S.vm, "onChange(_)");
	return 0;
}

void script_set_args(int count, char **args)
{
	S.argc = count;
	S.argv = args;
}

void script_window_size(int *cols, int *rows)
{
	*cols = S.cols;
	*rows = S.rows;
}

int script_font_done(void)
{
	return S.font_done;
}

void script_font(const char **path, int *px)
{
	*path = S.font_path[0] ? S.font_path : NULL;
	*px = S.font_px;
}

int script_dismiss(void)
{
	return S.dismiss;
}

int script_border(int *style, const char **chars)
{
	if (style)
		*style = S.border_style;
	if (chars)
		*chars = S.border_chars;
	return S.border_on;
}

int script_on_key(const char *name)
{
	bool handled = true;

	if (!S.vm || !S.app)
		return 0;

	wrenEnsureSlots(S.vm, 2);
	wrenSetSlotHandle(S.vm, 0, S.app);
	wrenSetSlotString(S.vm, 1, name);

	if (wrenCall(S.vm, S.on_key) != WREN_RESULT_SUCCESS)
		return 0;

	if (wrenGetSlotType(S.vm, 0) == WREN_TYPE_BOOL)
		handled = wrenGetSlotBool(S.vm, 0);

	return handled ? 1 : 0;
}

void script_on_draw(void)
{
	if (!S.vm || !S.app)
		return;

	wrenEnsureSlots(S.vm, 2);
	wrenSetSlotHandle(S.vm, 0, S.app);
	wrenGetVariable(S.vm, "wweft", "Grid", 1);
	wrenCall(S.vm, S.on_draw);
}

/* Both are optional. A script that calls Surface.every without an onTick
 * gets a runtime error naming the missing method, which is the right one. */
void script_on_tick(void)
{
	if (!S.vm || !S.app)
		return;

	wrenEnsureSlots(S.vm, 1);
	wrenSetSlotHandle(S.vm, 0, S.app);
	wrenCall(S.vm, S.on_tick);
}

void script_on_message(const char *line)
{
	if (!S.vm || !S.app)
		return;

	wrenEnsureSlots(S.vm, 2);
	wrenSetSlotHandle(S.vm, 0, S.app);
	wrenSetSlotString(S.vm, 1, line);
	wrenCall(S.vm, S.on_message);
}

void script_on_change(const char *path)
{
	if (!S.vm || !S.app)
		return;

	wrenEnsureSlots(S.vm, 2);
	wrenSetSlotHandle(S.vm, 0, S.app);
	wrenSetSlotString(S.vm, 1, path);
	wrenCall(S.vm, S.on_change);
}

size_t script_bytes(void)
{
	return S.bytes;
}

void script_close(void)
{
	if (!S.vm)
		return;

	if (S.on_key)
		wrenReleaseHandle(S.vm, S.on_key);
	if (S.on_draw)
		wrenReleaseHandle(S.vm, S.on_draw);
	if (S.on_tick)
		wrenReleaseHandle(S.vm, S.on_tick);
	if (S.on_message)
		wrenReleaseHandle(S.vm, S.on_message);
	if (S.on_change)
		wrenReleaseHandle(S.vm, S.on_change);
	if (S.app)
		wrenReleaseHandle(S.vm, S.app);

	wrenFreeVM(S.vm);
	memset(&S, 0, sizeof S);
}
