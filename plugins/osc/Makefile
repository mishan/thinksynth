LDFLAGS = -ggdb -lm
CFLAGS = -g -Wall -pedantic -fPIC -ffast-math -O2 -I../src
CC = g++

all: static.so
static.so: static.cpp
	$(CC) $(LDFLAGS) $(CFLAGS) -I../../src -DSKIPCONFIG -shared -Wl,-soname,static.so -o $@ $< -lc
clean:
	rm -f static.so

distclean: clean
