# $Id: Makefile,v 1.12 2003/05/03 07:27:24 joshk Exp $

SUBDIRS = src plugins

all clean install uninstall: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@ $(MAKECMDGOALS)

distclean: $(SUBDIRS)
	rm -f build.inc

.PHONY: clean distclean install uninstall $(SUBDIRS)
