CFLAGS   = -g -O2 -fPIC -std=c99
CFLAGS  += -Wall -Wextra -Wpedantic
CFLAGS  += -Wwrite-strings -Wdate-time
CFLAGS  += -Wunused -Wno-unused-parameter
CFLAGS  += -Wshadow -Wstrict-overflow -fno-strict-aliasing
CFLAGS  += -D_XOPEN_SOURCE=700 -D_DEFAULT_SOURCE
CFLAGS  += -lX11 -lXinerama $(shell pkg-config --cflags xft)
LDFLAGS  = $(shell pkg-config --libs xft)

CC      ?= gcc
INSTALL ?= install

PREFIX  ?= /usr/local
MANDIR  ?= $(PREFIX)/man

SRC = wallclock.c
PRG = $(SRC:.c=)
all: $(PRG)

tags: $(SRC)
	ctags $<

.c:
	$(CC)  $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	@rm -vf $(PRG) core tags *.o *.oo vgcore.* core

install: all
	$(INSTALL) -m 755 -D -t $(DESTDIR)$(PREFIX)/bin $(PRG)
	$(INSTALL) -m 644 -D -t $(DESTDIR)$(PREFIX)$(MANDIR) $(PRG).1


.PHONY:
	all install clean
