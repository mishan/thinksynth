#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "thBTree.h"
#include "thPlugin.h"
#include "thNode.h"

thNode::thNode (char *name, thPlugin *thplug)
{
	nodename = strdup(name);
	plugin = thplug;
	args = NULL;
}

thNode::~thNode ()
{
	free(nodename);
	delete args;
 	/* free anything else */
}

void thNode::SetArg (char *name, float *value)
{
}

void thNode::SetArg (char *name, char *node, char * value)
{
}

float *thNode::GetArg (char *name)
{
	return NULL;
}

void thNode::Process (void)
{
}
