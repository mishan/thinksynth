# $Id$

SUBDIRS = src plugins patches dsp etc docs #libthink is pulled in by dependency
ALL_SUBDIRS = libthink $(SUBDIRS)
NAME = thinksynth
VERSION = devel
exclusions = .svn .\#* debian

all: config.status $(SUBDIRS)

clean install uninstall: $(ALL_SUBDIRS)

config.status: configure
	sh configure

$(ALL_SUBDIRS): config.status
	$(MAKE) -C $@ $(MAKECMDGOALS)

distclean: config.status $(ALL_SUBDIRS)
	rm -f build.mk config.h config.log config.status *~
	rm -rf autom4te.cache

distrib: config.status distclean
	rm -rf ../$(NAME)-$(VERSION)
	(cd .. && ln -sf $(CURDIR) $(NAME)-$(VERSION))
	(cd .. && tar $(addprefix --exclude ,$(exclusions)) -chzf $(NAME)-$(VERSION).tar.gz $(NAME)-$(VERSION))
	rm -f ../$(NAME)-$(VERSION)
	@echo "Created source release $(NAME)-$(VERSION).tar.gz"

.PHONY: clean distclean install uninstall dist $(SUBDIRS) $(ALL_SUBDIRS)
