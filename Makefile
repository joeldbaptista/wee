CC      = cc
CFLAGS  = -std=c99 -Wall -Wextra -Wpedantic -O2
LDFLAGS =

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
INSTALL ?= install

BIN = wee

all: $(BIN)

$(BIN): wee.c
	$(CC) $(CFLAGS) -o $@ wee.c $(LDFLAGS)

clean:
	rm -f $(BIN)

install: $(BIN)
	$(INSTALL) -d "$(DESTDIR)$(BINDIR)"
	$(INSTALL) -m 755 $(BIN) "$(DESTDIR)$(BINDIR)/$(BIN)"

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/$(BIN)"

.PHONY: all clean install uninstall
