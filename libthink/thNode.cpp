#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "thList.h"
#include "thPlugin.h"
#include "thNode.h"

thNode::thNode (char *name)
{
	nodename = strdup(name);

	/* create any other objects */
	/* load plugin? */
}

thNode::~thNode ()
{
	free(nodename);

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
