# $Id: Makefile,v 1.23 2004/09/10 05:31:31 joshk Exp $

SUBDIRS = libthink src plugins dsp etc docs
NAME = thinksynth
VERSION = devel
exclusions = CVS .cvsignore .\#* debian

all clean install uninstall: $(SUBDIRS)

config.status: configure
	sh configure

$(SUBDIRS): config.status
	$(MAKE) -C $@ $(MAKECMDGOALS)

distclean: config.status $(SUBDIRS) dodistclean

dodistclean:
	rm -f build.mk config.h config.log config.status *~
	rm -rf autom4te.cache

distrib: config.status $(SUBDIRS) dodistclean
	rm -rf ../$(NAME)-$(VERSION)
	(cd .. && ln -sf $(CURDIR) $(NAME)-$(VERSION))
	(cd .. && tar $(addprefix --exclude ,$(exclusions)) -chzf $(NAME)-$(VERSION).tar.gz $(NAME)-$(VERSION))
	rm -f ../$(NAME)-$(VERSION)
	@echo "Created source release $(NAME)-$(VERSION).tar.gz"

.PHONY: clean distclean install uninstall dist $(SUBDIRS)
