# $Id: Makefile,v 1.14 2003/05/03 08:13:06 joshk Exp $

SUBDIRS = src plugins

all clean install uninstall: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@ $(MAKECMDGOALS)

distclean: $(SUBDIRS)
	rm -f build.mk config.log *~
	rm -rf autom4te.cache

.PHONY: clean distclean install uninstall $(SUBDIRS)
