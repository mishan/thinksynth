# $Id: Makefile,v 1.13 2003/05/03 02:47:57 ink Exp $

# Change this for every new plugin
PLUGIN_DIRS = osc mixer env misc test

all clean install uninstall: $(PLUGIN_DIRS)

$(PLUGIN_DIRS):
	$(MAKE) -C $@ $(MAKECMDGOALS)

distclean: $(PLUGIN_DIRS)
	rm -f plugin-common.inc

.PHONY: clean distclean install uninstall $(PLUGIN_DIRS)
