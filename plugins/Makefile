# Change this for every new plugin
PLUGIN_DIRS = osc test

all:
	for i in $(PLUGIN_DIRS); do \
		$(MAKE) -C $$i || exit 1; \
	done

clean:
	for i in $(PLUGIN_DIRS); do \
		$(MAKE) -C $$i clean; \
	done

distclean: clean
	rm -f plugin-common.inc

install:
	for i in $(PLUGIN_DIRS); do \
		$(MAKE) -C $$i install; \
	done

uninstall:
	for i in $(PLUGIN_DIRS); do \
		$(MAKE) -C $$i uninstall; \
	done
