# $Id: Makefile,v 1.16 2003/05/13 18:59:42 ink Exp $

# Change this for every new plugin
PLUGIN_DIRS = osc mixer math env misc filt dist test

all clean install uninstall: $(PLUGIN_DIRS)

$(PLUGIN_DIRS):
	$(MAKE) -C $@ $(MAKECMDGOALS)

distclean: $(PLUGIN_DIRS)
	rm -f plugin-common.inc

.PHONY: clean distclean install uninstall $(PLUGIN_DIRS)
