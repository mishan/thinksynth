# $Id: Makefile,v 1.18 2003/05/23 23:44:39 ink Exp $

# Change this for every new plugin
PLUGIN_DIRS = osc mixer math env misc filt delay impulse dist test

all clean install uninstall: $(PLUGIN_DIRS)

$(PLUGIN_DIRS):
	$(MAKE) -C $@ $(MAKECMDGOALS)

distclean: $(PLUGIN_DIRS)
	rm -f plugin-common.inc

.PHONY: clean distclean install uninstall $(PLUGIN_DIRS)
