#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "thBTree.h"
#include "thPlugin.h"
#include "thNode.h"
#include "thArg.h"

thNode::thNode (char *name, thPlugin *thplug)
{
	nodename = strdup(name);
	plugin = thplug;
	args = new thBTree;
}

thNode::~thNode ()
{
	free(nodename);
	delete args;
 	/* free anything else */
}

void thNode::SetArg (char *name, float *value, int len)
{
	thArg *arg = (thArg *)args->Find(name);
	if(!arg) {
		arg = new thArg(name, value, len);
		args->Insert(name, arg);
	} else {
		arg->SetArg(name, value, len);
	}
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
