# $Id: Makefile,v 1.15 2003/09/02 04:45:35 joshk Exp $

SUBDIRS = src plugins dsp

all clean install uninstall: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@ $(MAKECMDGOALS)

distclean: $(SUBDIRS)
	rm -f build.mk config.log *~
	rm -rf autom4te.cache

.PHONY: clean distclean install uninstall $(SUBDIRS)
