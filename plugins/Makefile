# Change this for every new plugin
PLUGIN_DIRS = osc test

all:
	for i in $(PLUGIN_DIRS); do \
		make -C $$i; \
	done

clean:
	for i in $(PLUGIN_DIRS); do \
		make -C $$i clean; \
	done

distclean: clean
	rm -f plugin-common.inc

install:
	for i in $(PLUGIN_DIRS); do \
		make -C $$i install; \
	done

uninstall:
	for i in $(PLUGIN_DIRS); do \
		make -C $$i uninstall; \
	done
