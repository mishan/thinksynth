# $Id: Makefile,v 1.11 2003/05/02 00:31:34 joshk Exp $

# Change this for every new plugin
PLUGIN_DIRS = osc test env

all clean install uninstall: $(PLUGIN_DIRS)

$(PLUGIN_DIRS):
	$(MAKE) -C $@ $(MAKECMDGOALS)

distclean: $(PLUGIN_DIRS)
	rm -f plugin-common.inc

.PHONY: clean distclean install uninstall $(PLUGIN_DIRS)
