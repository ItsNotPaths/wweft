/* stb_truetype and the glyph cache. It knows no grid and no Wayland. */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "stb_truetype.h"
#pragma GCC diagnostic pop

#include "spleen8x16.h"
#include "wweft.h"

#define CACHE_SLOTS 512      /* power of two, open addressing */

struct entry {
	uint32_t cp;             /* 0 = free */
	struct glyph g;
	unsigned char *own;      /* the buffer to free */
};

static struct {
	int bitmap;              /* 1 = the spleen fallback */
	stbtt_fontinfo info;
	unsigned char *file;
	float scale;
	int cell_w, cell_h, baseline;
	int scale_dev;           /* device pixels for one logical pixel */
	int scale_up;            /* whole number scale of the fallback */
	char source[256];
	struct entry cache[CACHE_SLOTS];
} F;

/* ---------------------------------------------------------------- search */

static char *read_line_cmd(const char *cmd)
{
	static char buf[512];
	FILE *p = popen(cmd, "r");
	size_t n;

	if (!p)
		return NULL;
	if (!fgets(buf, sizeof buf, p)) {
		pclose(p);
		return NULL;
	}
	pclose(p);

	n = strlen(buf);
	while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
		buf[--n] = 0;
	return n > 0 ? buf : NULL;
}

static unsigned char *read_file(const char *path, size_t *size)
{
	FILE *f = fopen(path, "rb");
	unsigned char *data;
	long n;

	if (!f)
		return NULL;
	if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) <= 0) {
		fclose(f);
		return NULL;
	}
	rewind(f);

	data = malloc((size_t)n);
	if (!data) {
		fclose(f);
		return NULL;
	}
	if (fread(data, 1, (size_t)n, f) != (size_t)n) {
		free(data);
		fclose(f);
		return NULL;
	}
	fclose(f);
	*size = (size_t)n;
	return data;
}

/* A cell must divide by the scale, or the buffer size is not legal. */
static int round_up(int v, int m)
{
	return ((v + m - 1) / m) * m;
}

static int open_ttf(const char *path, int px)
{
	size_t size;
	int ascent, descent, gap, advance, lsb;

	F.file = read_file(path, &size);
	if (!F.file)
		return -1;

	if (!stbtt_InitFont(&F.info, F.file, stbtt_GetFontOffsetForIndex(F.file, 0))) {
		free(F.file);
		F.file = NULL;
		return -1;
	}

	F.scale = stbtt_ScaleForPixelHeight(&F.info, (float)px);
	stbtt_GetFontVMetrics(&F.info, &ascent, &descent, &gap);
	stbtt_GetCodepointHMetrics(&F.info, 'M', &advance, &lsb);

	F.baseline = (int)(ascent * F.scale + 0.5f);
	F.cell_h = (int)((ascent - descent + gap) * F.scale + 0.5f);
	F.cell_w = (int)(advance * F.scale + 0.5f);
	if (F.cell_w < 1)
		F.cell_w = 1;
	if (F.cell_h < 1)
		F.cell_h = 1;
	F.cell_w = round_up(F.cell_w, F.scale_dev);
	F.cell_h = round_up(F.cell_h, F.scale_dev);

	snprintf(F.source, sizeof F.source, "%s", path);
	return 0;
}

static void open_fallback(int px)
{
	F.bitmap = 1;
	F.scale_up = px / SPLEEN8X16_HEIGHT;
	if (F.scale_up < 1)
		F.scale_up = 1;
	F.cell_w = round_up(SPLEEN8X16_WIDTH * F.scale_up, F.scale_dev);
	F.cell_h = round_up(SPLEEN8X16_HEIGHT * F.scale_up, F.scale_dev);
	F.baseline = SPLEEN8X16_HEIGHT * F.scale_up - 4 * F.scale_up;
	snprintf(F.source, sizeof F.source, "spleen 8x16 x%d", F.scale_up);
}

int font_open(const char *path, int px, int scale)
{
	const char *env;

	memset(&F, 0, sizeof F);

	if (scale < 1)
		scale = 1;
	F.scale_dev = scale;
	px *= scale;              /* the font renders in device pixels */

	if (path && *path && open_ttf(path, px) == 0)
		return 0;

	env = getenv("WWEFT_FONT");
	if (env && *env && open_ttf(env, px) == 0)
		return 0;

	env = read_line_cmd("fc-match -f '%{file}' monospace 2>/dev/null");
	if (env && *env && open_ttf(env, px) == 0)
		return 0;

	open_fallback(px);
	return 0;
}

void font_close(void)
{
	int i;

	for (i = 0; i < CACHE_SLOTS; i++)
		free(F.cache[i].own);
	free(F.file);
	memset(&F, 0, sizeof F);
}

/* ----------------------------------------------------------------- cache */

static struct entry *slot_for(uint32_t cp)
{
	uint32_t h = cp * 2654435761u;
	int i;

	for (i = 0; i < CACHE_SLOTS; i++) {
		struct entry *e = &F.cache[(h + (uint32_t)i) & (CACHE_SLOTS - 1)];
		if (e->cp == cp || e->cp == 0)
			return e;
	}
	return NULL;   /* full: draw nothing */
}

static void make_ttf_glyph(struct entry *e, uint32_t cp)
{
	int w = 0, h = 0, xoff = 0, yoff = 0;
	unsigned char *bm;

	bm = stbtt_GetCodepointBitmap(&F.info, 0, F.scale, (int)cp,
				      &w, &h, &xoff, &yoff);
	e->own = bm;
	e->g.alpha = bm;
	e->g.w = w;
	e->g.h = h;
	e->g.left = xoff;
	e->g.top = yoff;
}

static void make_bitmap_glyph(struct entry *e, uint32_t cp)
{
	int s = F.scale_up;
	int w = SPLEEN8X16_WIDTH * s;
	int h = SPLEEN8X16_HEIGHT * s;
	unsigned char *a;
	int y, x;

	if (cp < SPLEEN8X16_FIRST || cp > SPLEEN8X16_LAST)
		cp = '?';

	a = calloc((size_t)w * (size_t)h, 1);
	if (!a)
		return;

	for (y = 0; y < h; y++) {
		unsigned char row = spleen8x16[cp - SPLEEN8X16_FIRST][y / s];
		for (x = 0; x < w; x++)
			if (row & (0x80 >> (x / s)))
				a[y * w + x] = 255;
	}

	e->own = a;
	e->g.alpha = a;
	e->g.w = w;
	e->g.h = h;
	e->g.left = 0;
	e->g.top = -F.baseline;
}

const struct glyph *font_glyph(uint32_t cp)
{
	struct entry *e = slot_for(cp);

	if (!e)
		return NULL;
	if (e->cp == cp)
		return e->g.alpha ? &e->g : NULL;

	e->cp = cp;
	if (F.bitmap)
		make_bitmap_glyph(e, cp);
	else
		make_ttf_glyph(e, cp);

	return e->g.alpha ? &e->g : NULL;
}

int font_cell_w(void)    { return F.cell_w; }
int font_scale(void)     { return F.scale_dev; }
int font_cell_h(void)    { return F.cell_h; }
int font_baseline(void)  { return F.baseline; }
const char *font_source(void) { return F.source; }
