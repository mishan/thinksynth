# $Id: Makefile,v 1.19 2004/09/05 07:17:16 joshk Exp $

SUBDIRS = libthink src plugins dsp
NAME = thinksynth
VERSION = devel
exclusions = CVS .cvsignore .\#*

all clean install uninstall: $(SUBDIRS)

config.status: configure
	sh configure

$(SUBDIRS): config.status
	$(MAKE) -C $@ $(MAKECMDGOALS)

distclean: config.status $(SUBDIRS) dodistclean

dodistclean:
	rm -f build.mk config.log config.status *~
	rm -rf autom4te.cache

dist: config.status $(SUBDIRS) dodistclean
	rm -rf ../$(NAME)-$(VERSION)
	(cd .. && ln -sf $(CURDIR) $(NAME)-$(VERSION))
	(cd .. && tar $(addprefix --exclude ,$(exclusions)) -chzf $(NAME)-$(VERSION).tar.gz $(NAME)-$(VERSION))
	rm -f ../$(NAME)-$(VERSION)
	@echo "Created source release $(NAME)-$(VERSION).tar.gz"

.PHONY: clean distclean install uninstall dist $(SUBDIRS)
