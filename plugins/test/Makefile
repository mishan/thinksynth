LDFLAGS = -ggdb -lm
CFLAGS = -g -Wall -pedantic -fPIC -ffast-math -O2 -I../src
CC = g++

all: test.so
test.so: test.cpp
	$(CC) $(LDFLAGS) $(CFLAGS) -I../../src -DSKIPCONFIG -shared -Wl,-soname,test.so -o $@ $< -lc
clean:
	rm -f test.so

distclean: clean
