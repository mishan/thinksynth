# $Id: Makefile,v 1.13 2003/05/03 07:56:51 joshk Exp $

SUBDIRS = src plugins

all clean install uninstall: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@ $(MAKECMDGOALS)

distclean: $(SUBDIRS)
	rm -f build.mk config.log *~

.PHONY: clean distclean install uninstall $(SUBDIRS)
