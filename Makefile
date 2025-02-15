CC=gcc
CFLAGS=-Wall -Wextra -pedantic -ggdb
LFLAGS=

.PHONY: clean
.PHONY: memobs

all: memobs

memobs: src/memobs.c src/gstr3.c src/map.c src/wwx86.c src/mem.c
	$(CC) `pkg-config --cflags gtk4` $(CFLAGS) $^ -o $@ $(LFLAGS) `pkg-config --libs gtk4`

clean:
	rm -rf memobs
