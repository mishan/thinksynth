#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "thBTree.h"
#include "thPlugin.h"
#include "thArg.h"
#include "thNode.h"

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

void thNode::SetName(char *name) {
	free(nodename);
	nodename = name;
};

void thNode::SetArg (char *name, float *value, int num)
{
	thArg *arg = (thArg *)args->Find(name);
	if(!arg) {
		arg = new thArg(name, value, num);
		args->Insert(name, arg);
	} else {
		arg->SetArg(name, value, num);
	}
}

void thNode::SetArg (char *name, char *node, char * value)
{
}

thArgValue *thNode::GetArg (char *name)
{
	thArg *arg = (thArg *)args->Find(name);
	return (thArgValue *)arg->GetArg();
}

void thNode::Process (void)
{
}
