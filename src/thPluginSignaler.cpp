/* $Id: thPluginSignaler.cpp,v 1.8 2003/05/30 00:55:42 aaronl Exp $ */

#include "think.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "thPlugin.h"
#include "thPluginSignaler.h"

thPluginSignaler::thPluginSignaler ()
{
}

thPluginSignaler::~thPluginSignaler ()
{
}

int thPluginSignaler::HookSignal (thPluginSignal &signal)
{
	if(!signal.callback)
		return 1;

	if((signal.sigNum >= 0) && (signal.sigNum < NUM_SIGNALS)) {
		plugSignals[signal.sigNum].push_back(signal);
	}
	else {
		return 1;
	}

	return 0;
}

void thPluginSignaler::UnhookSignal (thPluginSignal &signal)
{
	for (list<thPluginSignal>::iterator i = plugSignals[signal.sigNum].begin(); i != plugSignals[signal.sigNum].end(); i++)
		if(memcmp((void*)&*i, (void*)&signal, sizeof(thPluginSignal)) == 0) {
			plugSignals[signal.sigNum].erase(i);
		}
}

int thPluginSignaler::Fire (int sig, void *a, void *b, void *c, void *d,
							void *e, char f)
{
	int flag = 0;

	for(list<thPluginSignal>::const_iterator i = plugSignals[sig].begin(); i != plugSignals[sig].end(); i++) {
		thPluginSignal signal = *i;

		if(signal.callback) {
			flag = signal.callback (a, b, c, d, e, f);
		}
	}

	return flag;
}
