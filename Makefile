# $Id: Makefile,v 1.24 2004/10/01 09:16:21 joshk Exp $

SUBDIRS = src plugins dsp etc docs #libthink is pulled in by dependency
CLEAN_SUBDIRS = libthink $(SUBDIRS)
NAME = thinksynth
VERSION = devel
exclusions = CVS .cvsignore .\#* debian

clean: $(CLEAN_SUBDIRS)
all install uninstall: $(SUBDIRS)

config.status: configure
	sh configure

$(SUBDIRS): config.status
	$(MAKE) -C $@ $(MAKECMDGOALS)

distclean: config.status $(CLEAN_SUBDIRS) dodistclean

dodistclean:
	rm -f build.mk config.h config.log config.status *~
	rm -rf autom4te.cache

distrib: config.status $(SUBDIRS) dodistclean
	rm -rf ../$(NAME)-$(VERSION)
	(cd .. && ln -sf $(CURDIR) $(NAME)-$(VERSION))
	(cd .. && tar $(addprefix --exclude ,$(exclusions)) -chzf $(NAME)-$(VERSION).tar.gz $(NAME)-$(VERSION))
	rm -f ../$(NAME)-$(VERSION)
	@echo "Created source release $(NAME)-$(VERSION).tar.gz"

.PHONY: clean distclean install uninstall dist $(SUBDIRS) $(CLEAN_SUBDIRS)
