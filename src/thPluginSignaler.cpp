#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "thList.h"
#include "thPlugin.h"
#include "thPluginSignaler.h"

thPluginSignaler::thPluginSignaler ()
{
	int i;

	for(i = 0; i < NUM_SIGNALS; i++) {
		plugSignals[i] = new thList;
	}
}

thPluginSignaler::~thPluginSignaler ()
{
	int i;
	
	for(i = 0; i < NUM_SIGNALS; i++) {
		delete plugSignals[i];
	}
}

int thPluginSignaler::HookSignal (thPluginSignal *signal)
{
	if(!signal || !signal->callback)
		return 1;

	if((signal->sigNum >= 0) && (signal->sigNum < NUM_SIGNALS)) {
		plugSignals[signal->sigNum]->Add(signal);
	}
	else {
		return 1;
	}

	return 0;
}

void thPluginSignaler::UnhookSignal (thPluginSignal *signal)
{
	thListNode *node;
	
	if(!signal) {
		return;
	}

	for(node = plugSignals[signal->sigNum]->GetHead(); node; 
		node = node->prev) {
		if(node->data == signal) {
			plugSignals[signal->sigNum]->Remove(node);
		}
	}
}

int thPluginSignaler::Fire (int sig, void *a, void *b, void *c, void *d,
							void *e, char f)
{
	thListNode *node;
	int flag = 0;

	for(node = plugSignals[sig]->GetHead(); node; node = node->prev) {
		thPluginSignal *signal = (thPluginSignal *)node->data;

		if(signal && signal->callback) {
			flag = signal->callback (a, b, c, d, e, f);
		}
	}

	return flag;
}
