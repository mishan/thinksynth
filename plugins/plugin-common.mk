GNU_RMDIR_OPTION = --ignore-fail-on-non-empty

PLUGIN_NAMES := $(addsuffix .so,$(PLUGIN_NAMES))

all: $(PLUGIN_NAMES)

include ../../build.mk

INCLUDES = -I../../libthink -I../..

plugin_path=/usr/local/lib/thinksynth/plugins/

CXXFLAGS += -DPLUGIN_BUILD -I/usr/include/sigc++-2.0 -I/usr/lib/x86_64-linux-gnu/sigc++-2.0/include

install:
	mkdir -p $(DESTDIR)$(plugin_path)/$(notdir $(CURDIR))
	for plugin in $(PLUGIN_NAMES); do \
	  cp -f $$plugin $(DESTDIR)$(plugin_path)/$(notdir $(CURDIR)); \
	done

uninstall:
	for plugin in $(PLUGIN_NAMES); do \
	  rm -f $(DESTDIR)$(plugin_path)/$(notdir $(CURDIR))/$$plugin; \
	done
	-rmdir $(GNU_RMDIR_OPTION) $(DESTDIR)$(plugin_path)/$(notdir $(CURDIR))
