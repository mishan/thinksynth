#include <stdlib.h>
#include <stdio.h>
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
