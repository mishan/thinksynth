# $Id: Makefile,v 1.16 2004/01/28 05:25:59 misha Exp $

SUBDIRS = libthink src plugins dsp

all clean install uninstall: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@ $(MAKECMDGOALS)

distclean: $(SUBDIRS)
	rm -f build.mk config.log *~
	rm -rf autom4te.cache

.PHONY: clean distclean install uninstall $(SUBDIRS)
