SUBDIRS = src plugins

all clean install uninstall: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@ $(MAKECMDGOALS)

distclean: $(SUBDIRS)
	rm -f build.inc

.PHONY: clean distclean install uninstall $(SUBDIRS)
