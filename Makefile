PROGRAM=think++

CC=g++

SRCS=main.cpp thOSSAudio.cpp endian.cpp AudioBuffer.cpp Exception.cpp thWav.cpp \
	thList.cpp thNode.cpp thMod.cpp thMidiChan.cpp thMidiNote.cpp thPlugin.cpp
OBJS=$(SRCS:.cpp=.o)

LIBS=-ldl
LIBOBJ=$(LIBS:.cpp=.o)

CFLAGS=-g -Wall
LDFLAGS=$(LIBOBJ)

all: $(PROGRAM)

$(PROGRAM): $(OBJS) $(LIBOBJ)
	$(CC) $(CFLAGS) $(OBJS) $(LDFLAGS) -o $@

clean:
	rm -f $(OBJS) $(LIBOBJ) *.core core $(PROGRAM)

distclean: clean

%.o: %.cpp
	$(CC) $(CFLAGS) -c $< -o $@
