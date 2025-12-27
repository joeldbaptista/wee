CC      = cc
CFLAGS  = -std=c99 -Wall -Wextra -Wpedantic -O2
LDFLAGS =

BIN = wee

all: $(BIN)

$(BIN): wee.c
	$(CC) $(CFLAGS) -o $@ wee.c $(LDFLAGS)

clean:
	rm -f $(BIN)

.PHONY: all clean
