#define USE_PLUGIN

#include <stdio.h>
#include "plugin.h"
// XXX   #include "thnodes.h"

char	*name = "test";
char	*desc = "Test Plugin";

int	init_plug ( void *a, void *b);
int	plugin_func ( void *a, void *b, unsigned int c);

struct	xp_signal init_sig;
struct	xp_signal func_sig;

int	module_init (int ver, class thPlugin plugin)
{
	/* This check *MUST* be done first */
	if (ver != MODULE_IFACE_VER)
		return 1;
	
	if (module_find (name) != NULL) {
		/* We are already loaded */
		printf("Module already loaded\n");
		return 1;
	}
	
	mod->name = name;
	mod->desc = desc;
		
	init_sig.signal = 0;
	init_sig.callback = XP_CALLBACK(init_plug);
	init_sig.naddr = 0;
	init_sig.mod = mod;

	func_sig.callback = XP_CALLBACK(plugin_func);
        func_sig.naddr = 0;
        func_sig.mod = mod;

	hook_signal(&init_sig);

	return 0;
}

void module_cleanup (struct module *mod)
{
	printf("Sample module unloading\n");
}

int init_plug ( void *a, void *b, void *c, void *d, void *e, char f)
{
	printf("mixer::add loaded on signal %i\n", (int)a);
        func_sig.signal = (int)a;
	hook_signal(&func_sig);
	return 1;
}

int plugin_func ( void *a, void *b, void *c, void *d, void *e, char f)
{
  printf("TEST!!\n");
  return 0;
}

