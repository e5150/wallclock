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

#include <X11/extensions/Xinerama.h>
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
	int screen;
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
	.screen = -1,
};

static bool running = true;

struct line_t {
	char buf[64];
	int y;
	int ascent;
	int height;
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
	int x, y;
	int w, h;
	GC gc;
	Drawable da;
	Colormap cmap;
	Visual *vis;
	struct line_t text1, text2;
} dc;

static int
textnw(XftFont *xfont, const char *text, int len) {
        XGlyphInfo ext;
        XftTextExtentsUtf8(dc.dpy, xfont, (FcChar8*)text, len, &ext);
        return ext.xOff;
}

static void
initline(struct line_t *line, const struct linearg_t *arg) {
	if (!(line->xfont = XftFontOpenName(dc.dpy,dc.screen, arg->font))) {
		errx(1, "Cannot load font: %s", arg->font);
	}
	line->ascent = line->xfont->ascent;
	line->height = line->xfont->ascent + line->xfont->descent;
	if (args.debug > 1) {
		printf("%s:\n", arg->font);
		printf("  a: %d\n", line->xfont->ascent);
		printf("  d: %d\n", line->xfont->descent);
		printf("  h: %d\n", line->height);
	}
	if (!XftColorAllocName(dc.dpy, dc.vis, dc.cmap, arg->color, &line->color)) {
		errx(1, "Cannot load color: %s", arg->color);
	}
	line->warned = false;
	line->arg = arg;
}

static bool
drawtext(struct line_t *line, struct tm *tmp, bool force) {
	char buf[64] = { 0 };
	if (!strftime(buf, sizeof(buf), line->arg->fmt, tmp)) {
		err(1, "ERROR strftime %s", line->arg->fmt);
	}
	if (!force && !strcmp(buf, line->buf)) {
		/* no need to redraw */
		return false;
	}
	/* non-monospaced */
	size_t len = strlen(buf);
	int w = textnw(line->xfont, buf, len);

	if (!line->warned && w > dc.w) {
		line->warned = true;
		warnx("Excessive width %d for '%s' using font %s", w, buf, line->arg->font);
	}

	XSetForeground(dc.dpy, dc.gc, args.debug > 2 ? 0x302030 : dc.bg.pixel);
	XFillRectangle(dc.dpy, dc.da, dc.gc, dc.x, line->y, dc.w, line->height);

	XftDraw *draw = XftDrawCreate(dc.dpy, dc.da, dc.vis, dc.cmap);

	XftDrawStringUtf8(draw,
	                  &line->color,
	                  line->xfont,
	                  (dc.w - w) / 2,
	                  line->y + line->ascent,
	                  (XftChar8*)buf,
	                  len);
	XftDrawDestroy(draw);
	strncpy(line->buf, buf, sizeof(line->buf));
	return true;
}

static bool
draw() {
	time_t t = time(NULL);
	struct tm *tmp;
	bool dirty = false;
	if (!(tmp = localtime(&t))) {
		err(1, "ERROR: localtime");
	}
	dirty = drawtext(&dc.text1, tmp, dirty);
	dirty = drawtext(&dc.text2, tmp, dirty);
	XSync(dc.dpy, 0);
	return dirty;
}

static void
setup() {
	if (!(dc.dpy = XOpenDisplay(NULL))) {
		errx(1, "Cannot open display");
	}
	dc.screen = DefaultScreen(dc.dpy);
	dc.x = 0;
	dc.y = 0;
	dc.w = DisplayWidth(dc.dpy, dc.screen);
	dc.h = DisplayHeight(dc.dpy, dc.screen);
	dc.root = RootWindow(dc.dpy, dc.screen);
	dc.cmap = DefaultColormap(dc.dpy, dc.screen);
	dc.vis = DefaultVisual(dc.dpy, dc.screen);
	if (XineramaIsActive(dc.dpy)) {
		int n, i = 0;
		XineramaScreenInfo *info = XineramaQueryScreens(dc.dpy, &n);
		if (args.screen != -1) {
			if (args.screen >= n) {
				errx(1, "%d exceeds the number of screens (%d)", args.screen, n);
			}
			i = args.screen;
		} else {
			for (i = 0; i < n; ++i) {
				if (info[i].x_org == 0) {
					break;
				}
			}
		}
		dc.x = info[i].x_org;
		dc.y = info[i].y_org;
		dc.w = info[i].width;
		dc.h = info[i].height;
		XFree(info);
	}
	if (args.debug > 1) {
		printf("x=%d y=%d w=%d h=%d\n", dc.x, dc.y, dc.w, dc.h);
	}
	dc.da = XCreatePixmap(dc.dpy, dc.root, dc.w, dc.h, DefaultDepth(dc.dpy, dc.screen));
	XGCValues gcv = { 0 };
	dc.gc = XCreateGC(dc.dpy, dc.root, GCGraphicsExposures, &gcv);
	if (!XAllocNamedColor(dc.dpy, dc.cmap, args.background, &dc.bg, &dc.bg)) {
		errx(1, "Cannot load color: %s", args.background);
	}

	initline(&dc.text1, &args.text1);
	initline(&dc.text2, &args.text2);
	dc.text1.y = (dc.h - dc.text1.height - dc.text2.height) / 2 + args.text1.dy;
	dc.text2.y = dc.text1.y + dc.text1.height + args.text2.dy;
	XSetForeground(dc.dpy, dc.gc, dc.bg.pixel);
	XFillRectangle(dc.dpy, dc.da, dc.gc, dc.x, dc.y, dc.w, dc.h);

	XSelectInput(dc.dpy, dc.root, ExposureMask);

	draw();
	XCopyArea(dc.dpy, dc.da, dc.root, dc.gc, 0, 0, dc.w, dc.h, dc.x, dc.y);
	XSync(dc.dpy, 0);

}

static void
catch(int signal) {
	running = false;
}

static void
cleanup() {
	XClearWindow(dc.dpy, dc.root);
	XFreePixmap(dc.dpy, dc.da);
	XftColorFree(dc.dpy, dc.vis, dc.cmap, &dc.text1.color);
	XftColorFree(dc.dpy, dc.vis, dc.cmap, &dc.text2.color);
	XFreeGC(dc.dpy, dc.gc);
	XCloseDisplay(dc.dpy);
}

static void
usage() {
	printf("usage: [-s screen] [-b background] [-Ff font] [-Cc color] [-Dd datefmt] [-Yy y-offset]\n");
	exit(1);
}

int
main(int argc, char *argv[]) {
	bool daemonize = true;

	ARGBEGIN {
	case 's':
		args.screen = atoi(EARGF(usage()));
		break;
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
		daemonize = false;
		break;
	default:
		usage();
	} ARGEND;

	if (daemonize) {
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

	struct pollfd pfd = {
		.fd = ConnectionNumber(dc.dpy),
		.events = POLLIN,
	};

	while (running) {
		bool dirty = false;
		switch(poll(&pfd, 1, 1000)) {
		case -1:
			warn("ERROR: poll");
			break;
		case 0:
			dirty = draw();
			break;
		default:
			dirty = true;
		}
		if (dirty) {
			XCopyArea(dc.dpy, dc.da, dc.root, dc.gc, 0, 0, dc.w, dc.h, dc.x, dc.y);
			XSync(dc.dpy, 0);
		}
	}

	cleanup();

	return 0;
}
