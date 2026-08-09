# Standalone makefile for the dspcheck and dspstress harnesses.
#
# Not driven by configure -- they just need config.h and libthink from a tree
# that has already been built.
#
#   make -C scripts              # plain build
#   make -C scripts asan         # ASan + UBSan build (dspcheck-asan)
#   make -C scripts tsan         # ThreadSanitizer build (dspstress-tsan)
#
# Run it over the shipped DSPs from the top of the tree:
#
#   LD_LIBRARY_PATH=libthink scripts/dspcheck -p plugins/ \
#       $(find dsp -name '*.dsp')
#
# Exit status is the number of files that failed to load. To make the
# sanitizer build worth anything, libthink and the plugins have to be built
# with the same flags:
#
#   ./configure --enable-debug \
#       CXXFLAGS="-g -O0 -fsanitize=address,undefined" \
#       LDFLAGS="-fsanitize=address,undefined"
#   make && make -C scripts asan
#   LD_LIBRARY_PATH=libthink ASAN_OPTIONS=detect_leaks=0 \
#       scripts/dspcheck-asan -q -p plugins/ $(find dsp -name '*.dsp')

CXX ?= g++
TOP := ..

SIGC_CFLAGS := $(shell pkg-config --cflags sigc++-2.0)
SIGC_LIBS := $(shell pkg-config --libs sigc++-2.0)

# -std matches configure.ac: think.h pulls in <atomic> by way of thRing.h.
CXXFLAGS := -g -O0 -Wall -std=c++11 \
	    -I$(TOP) -I$(TOP)/libthink -I$(TOP)/src $(SIGC_CFLAGS)
LDFLAGS := -L$(TOP)/libthink -lthink $(SIGC_LIBS) -ldl

# gthPatchfile.cpp lives in the binary rather than in libthink, so pull it in
# directly to cover the .patch path. It needs sigc++ but not gtkmm.
SRCS := dspcheck.cpp $(TOP)/src/gthPatchfile.cpp

SAN := -fsanitize=address,undefined -fno-omit-frame-pointer

# ThreadSanitizer cannot be combined with AddressSanitizer, so dspstress needs
# its own build tree configured with -fsanitize=thread throughout.
TSAN := -fsanitize=thread -fno-omit-frame-pointer

all: dspcheck dspstress dsplevel dspgraph dspab dspwrite dsplive dspnew

dspcheck: $(SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS) $(LDFLAGS)

dsplevel: dsplevel.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

# NodeGraph is deliberately GTK-free so it can be exercised headlessly.
# The writer, against every settable value in the corpus.
dspwrite: dspwrite.cpp $(TOP)/src/NodeGraph.cpp $(TOP)/src/NodeEdit.cpp
	$(CXX) $(CXXFLAGS) -o $@ dspwrite.cpp $(TOP)/src/NodeGraph.cpp \
		$(TOP)/src/NodeEdit.cpp $(LDFLAGS)

# Can the editor build a working .dsp from nothing?
dspnew: dspnew.cpp $(TOP)/src/NodeGraph.cpp $(TOP)/src/NodeCatalog.cpp \
	$(TOP)/src/NodeEdit.cpp
	$(CXX) $(CXXFLAGS) -o $@ dspnew.cpp $(TOP)/src/NodeGraph.cpp \
		$(TOP)/src/NodeCatalog.cpp $(TOP)/src/NodeEdit.cpp $(LDFLAGS)

# Does moving a control change the sound of a note already ringing?
dsplive: dsplive.cpp $(TOP)/src/NodeGraph.cpp
	$(CXX) $(CXXFLAGS) -o $@ dsplive.cpp $(TOP)/src/NodeGraph.cpp $(LDFLAGS)

# A/B: same note through two plugin sets, compared bitwise.
dspab: dspab.cpp
	$(CXX) $(CXXFLAGS) -o $@ dspab.cpp $(LDFLAGS)

dspgraph: dspgraph.cpp $(TOP)/src/NodeGraph.cpp $(TOP)/src/NodeLayout.cpp
	$(CXX) $(CXXFLAGS) -o $@ dspgraph.cpp $(TOP)/src/NodeGraph.cpp \
		$(TOP)/src/NodeLayout.cpp $(LDFLAGS)

dspstress: dspstress.cpp
	$(CXX) $(CXXFLAGS) -pthread -o $@ $< $(LDFLAGS)

asan: dspcheck-asan

dspcheck-asan: $(SRCS)
	$(CXX) $(CXXFLAGS) $(SAN) -o $@ $(SRCS) $(SAN) $(LDFLAGS)

tsan: dspstress-tsan

dspstress-tsan: dspstress.cpp
	$(CXX) $(CXXFLAGS) $(TSAN) -pthread -o $@ $< $(TSAN) $(LDFLAGS)

clean:
	rm -f dspcheck dspcheck-asan dspstress dspstress-tsan dsplevel dspgraph

.PHONY: all asan tsan clean
