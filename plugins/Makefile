# $Id: Makefile,v 1.19 2003/06/24 21:39:01 ink Exp $

# Change this for every new plugin
PLUGIN_DIRS = osc mixer math logic env misc filt delay impulse dist test

all clean install uninstall: $(PLUGIN_DIRS)

$(PLUGIN_DIRS):
	$(MAKE) -C $@ $(MAKECMDGOALS)

distclean: $(PLUGIN_DIRS)
	rm -f plugin-common.inc

.PHONY: clean distclean install uninstall $(PLUGIN_DIRS)
