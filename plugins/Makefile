# $Id: Makefile,v 1.10 2003/04/27 20:57:58 joshk Exp $

# Change this for every new plugin
PLUGIN_DIRS = osc test

all clean install uninstall: $(PLUGIN_DIRS)

$(PLUGIN_DIRS):
	$(MAKE) -C $@ $(MAKECMDGOALS)

distclean: $(PLUGIN_DIRS)
	rm -f plugin-common.inc

.PHONY: clean distclean install uninstall $(PLUGIN_DIRS)
