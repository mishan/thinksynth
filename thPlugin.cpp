#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "thPlugin.h"

thPlugin::thPlugin (char *name, int id, bool state)
{
	plugName = strdup(name);
	plugId = id;
	plugState = state;
}

thPlugin::~thPlugin ()
{
	free(plugName);
}

char *thPlugin::GetName (void)
{
	return plugName;
}

int thPlugin::Fire (void)
{
	return 0;
}
