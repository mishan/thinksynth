#ifndef TH_PLUGIN_SIGNALER_H
#define TH_PLUGIN_SIGNALER_H 1

/* XXX */
#define NUM_SIGNALS 128

struct thPluginSignal {
	int sigNum;
	int (*callback) (void *, void *, void *, void *, void *, char);

	thPlugin *plugin;
	void *data;
};

class thPluginSignaler {
public:
	thPluginSignaler ();
	~thPluginSignaler ();

	int HookSignal (thPluginSignal *signal);
	void UnhookSignal (thPluginSignal *signal);
	
	int Fire (int sig, void *a, void *b, void *c, void *d, void *e, 
			  char f);
private:
	thList *plugSignals[NUM_SIGNALS];
};

#endif /* TH_PLUGIN_SIGNALER_H */
