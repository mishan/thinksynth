# $Id: Makefile,v 1.14 2003/05/06 09:04:39 ink Exp $

# Change this for every new plugin
PLUGIN_DIRS = osc mixer env misc filt test

all clean install uninstall: $(PLUGIN_DIRS)

$(PLUGIN_DIRS):
	$(MAKE) -C $@ $(MAKECMDGOALS)

distclean: $(PLUGIN_DIRS)
	rm -f plugin-common.inc

.PHONY: clean distclean install uninstall $(PLUGIN_DIRS)
