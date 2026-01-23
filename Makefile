CC      = cc
CFLAGS  = -std=c99 -Wall -Wextra -Wpedantic -O2 -I. -I../..
LDFLAGS =

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
INSTALL ?= install

BIN = wee
SRC = wee.c wee_util.c sbuf.c utf.c lines.c term.c status.c undo.c file.c edit.c ex.c mode.c render.c
OBJ = $(SRC:.c=.o)

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDFLAGS)

clean:
	rm -f $(BIN) $(OBJ)

install: $(BIN)
	$(INSTALL) -d "$(DESTDIR)$(BINDIR)"
	$(INSTALL) -m 755 $(BIN) "$(DESTDIR)$(BINDIR)/$(BIN)"

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/$(BIN)"

.PHONY: all clean install uninstall
