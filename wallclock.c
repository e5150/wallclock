/* Copyright 2010,2019 Lars Lindqvist
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <X11/Xft/Xft.h>
#include <X11/Xutil.h>
#include <err.h>
#include <locale.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "arg.h"

struct linearg_t {
	const char *fmt;
	const char *font;
	const char *color;
	int dy;
};
static struct {
	struct linearg_t text1, text2;
	const char *background;
	int debug;
} args = {
	.text1 = {
		.fmt   = "%H:%M",
		.font  = "DejaVuSansMono:style=bold:size=400",
		.color = "#202020",
		.dy    = 0,
	},
	.text2 = {
		.fmt   = "%Y-%m-%d %a. v. %V",
		.font  = "DejaVuSansMono:style=bold:size=60",
		.color = "#303030",
		.dy    = 0,
	},
	.background = "#000000",
	.debug = 1,
};

static bool running = true;

struct line_t {
	char buf[64];
	int y;
	int ascent;
	int height;
	int fwidth;
	bool warned;
	XftFont *xfont;
	XftColor color;
	const struct linearg_t *arg;
};

static struct {
	XColor bg;
	Display *dpy;
	int screen;
	Window root;
	int w, h;
	GC gc;
	Drawable da;
	Colormap cmap;
	struct line_t text1, text2;
} dc;

static int
textnw(XftFont *xfont, const char *text, int len) {
        XGlyphInfo ext;
        XftTextExtentsUtf8(dc.dpy, xfont, (FcChar8*)text, len, &ext);
        return ext.width;
}

static void
initline(struct line_t *line, const struct linearg_t *arg) {
	if (!(line->xfont = XftFontOpenName(dc.dpy,dc.screen, arg->font))) {
		errx(1, "Cannot load font: %s", arg->font);
	}
	line->ascent = line->xfont->ascent;
	line->height = line->xfont->ascent + line->xfont->descent;
	line->fwidth = line->xfont->max_advance_width;
	if (line->fwidth > textnw(line->xfont, "0", 1) * 1.5) {
		if (args.debug > 0) {
			warnx("Using non-monospaced font: %s", arg->font);
		}
		line->fwidth = 0;
	}
	if (!XftColorAllocName(dc.dpy,
	                       DefaultVisual(dc.dpy, dc.screen),
	                       DefaultColormap(dc.dpy, dc.screen),
	                       arg->color,
	                       &line->color)) {
		errx(1, "Cannot load color: %s", arg->color);
	}
	line->warned = false;
	line->arg = arg;
}


static void
setup() {
	if (!(dc.dpy = XOpenDisplay(NULL))) {
		errx(1, "Cannot open display");
	}
	dc.screen = DefaultScreen(dc.dpy);
	dc.w = DisplayWidth(dc.dpy, dc.screen);
	dc.h = DisplayHeight(dc.dpy, dc.screen);
	dc.root = RootWindow(dc.dpy, dc.screen);
	dc.cmap = DefaultColormap(dc.dpy, dc.screen);
	dc.da = XCreatePixmap(dc.dpy, dc.root, dc.w, dc.h, DefaultDepth(dc.dpy, dc.screen));
	dc.gc = XCreateGC(dc.dpy, dc.root, 0, 0);
	if (!XAllocNamedColor(dc.dpy, DefaultColormap(dc.dpy, dc.screen), args.background, &dc.bg, &dc.bg)) {
		errx(1, "Cannot load color: %s", args.background);
	}
	initline(&dc.text1, &args.text1);
	initline(&dc.text2, &args.text2);
	dc.text1.y = (dc.h - dc.text1.height - dc.text2.height) / 2 + args.text1.dy;
	dc.text2.y = dc.text1.y + dc.text1.height + args.text2.dy;
	XSetForeground(dc.dpy, dc.gc, dc.bg.pixel);
	XFillRectangle(dc.dpy, dc.da, dc.gc, 0, 0, dc.w, dc.h);

	XSelectInput(dc.dpy, dc.root, ExposureMask);
}

static void
drawtext(struct line_t *line, struct tm *tmp) {
	char buf[64] = { 0 };
	if (!strftime(buf, sizeof(buf), line->arg->fmt, tmp)) {
		err(1, "ERROR strftime %s", line->arg->fmt);
	}
	if (!strcmp(buf, line->buf)) {
		/* no need to redraw */
		return;
	}
	size_t len = strlen(buf);
	int w = line->fwidth * len;
	int ew = 0;
	if (!w) {
		/* non-monospaced */
		w = textnw(line->xfont, buf, len);
		ew = w / 8;
	}

	if (!line->warned && w + ew > dc.w) {
		line->warned = true;
		warnx("Excessive width %d for '%s' using font %s", w + ew, buf, line->arg->font);
	}

	XSetForeground(dc.dpy, dc.gc, args.debug > 2 ? 0x302030 : dc.bg.pixel);
	XFillRectangle(dc.dpy, dc.da, dc.gc, (dc.w - w) / 2, line->y, w + ew, line->height);

	XftDraw *draw = XftDrawCreate(dc.dpy, dc.da, DefaultVisual(dc.dpy, dc.screen), dc.cmap);

	XftDrawStringUtf8(draw,
	                  &line->color,
	                  line->xfont,
	                  (dc.w - w + ew) / 2,
	                  line->y + line->ascent,
	                  (XftChar8*)buf,
	                  len);
	XftDrawDestroy(draw);
	strncpy(line->buf, buf, sizeof(line->buf));
}

static void
draw(bool dirty) {
	if (dirty) {
		time_t t = time(NULL);
		struct tm *tmp;
		if (!(tmp = localtime(&t))) {
			err(1, "ERROR: localtime");
		}
		drawtext(&dc.text1, tmp);
		drawtext(&dc.text2, tmp);
	}
	XCopyArea(dc.dpy, dc.da, dc.root, dc.gc, 0, 0, dc.w, dc.h, 0, 0);
	XSync(dc.dpy, 0);
}

static void
catch(int signal) {
	printf("caught %d\n", signal);
	running = false;
}

static void
cleanup() {
	XClearWindow(dc.dpy, dc.root);
	XFreePixmap(dc.dpy, dc.da);
	XftColorFree(dc.dpy, DefaultVisual(dc.dpy, dc.screen), dc.cmap, &dc.text1.color);
	XftColorFree(dc.dpy, DefaultVisual(dc.dpy, dc.screen), dc.cmap, &dc.text2.color);
	XFreeGC(dc.dpy, dc.gc);
	XCloseDisplay(dc.dpy);
}

static void
usage() {
	printf("usage: [-b background] [-Ff font] [-Cc color] [-Dd datefmt] [-Yy y-offset]\n");
	exit(1);
}

int
main(int argc, char *argv[]) {
	bool background = true;

	ARGBEGIN {
	case 'q':
		--args.debug;
		break;
	case 'v':
		++args.debug;
		break;
	case 'b':
		args.background = EARGF(usage());
		break;
	case 'F':
		args.text1.font = EARGF(usage());
		break;
	case 'f':
		args.text2.font = EARGF(usage());
		break;
	case 'C':
		args.text1.color = EARGF(usage());
		break;
	case 'c':
		args.text2.color = EARGF(usage());
		break;
	case 'D':
		args.text1.fmt = EARGF(usage());
		break;
	case 'd':
		args.text2.fmt = EARGF(usage());
		break;
	case 'Y':
		args.text1.dy = atoi(EARGF(usage()));
		break;
	case 'y':
		args.text2.dy = atoi(EARGF(usage()));
		break;
	case 'x':
		background = false;
		break;
	default:
		usage();
	} ARGEND;

	if (background) {
		switch (fork()) {
		case -1:
			err(1, "ERROR: fork");
		case 0:
			if (setsid() < 0) {
				err(1, "ERROR: setsid");
			}
			break;
		default:
			exit(0);
		}
	}

	if (!setlocale(LC_ALL, "")) {
		warn("WARNING: setlocale failed");
	}

	if (signal(SIGINT,  catch) == SIG_ERR
	 || signal(SIGHUP,  catch) == SIG_ERR
	 || signal(SIGTERM, catch) == SIG_ERR) {
		warn("WARNING: unable to catch signals");
	}

	setup();
	draw(true);

	struct pollfd pfd = {
		.fd = ConnectionNumber(dc.dpy),
		.events = POLLIN,
	};

	while (running) {
		switch(poll(&pfd, 1, 1000)) {
		case -1:
			warn("ERROR: poll");
			break;
		case 0:
			draw(true);
			break;
		default:
			draw(false);
		}
	}

	cleanup();

	return 0;
}
