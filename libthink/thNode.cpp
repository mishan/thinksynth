/* $Id: thNode.cpp,v 1.49 2003/05/11 06:23:46 joshk Exp $ */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "thArg.h"
#include "thList.h"
#include "thBSTree.h"
#include "thPlugin.h"
#include "thNode.h"

thNode::thNode (const char *name, thPlugin *thplug)	
{
	plugin = thplug;
	nodename = strdup(name);
	recalc = false;
	args = new thBSTree(StringCompare);
}

thNode::~thNode (void)
{
	free(nodename);
	delete args;
}

/* We own this string. The caller may not free it. */
void thNode::SetName(char *name)
{
	free(nodename);
	nodename = strdup(name);
}

thArg *thNode::SetArg (const char *name, float *value, int num)
{
	thArg *arg = (thArg *)args->GetData((void *)name);

	if(!arg) {
		arg = new thArg(name, value, num);
		args->Insert((void *)strdup(name), arg);
	}
	else {
		arg->SetArg(name, value, num);
	}
	return arg;
}

thArg *thNode::SetArg (const char *name, const char *node, const char *value)
{
	thArg *arg = (thArg *)args->GetData((void *)name);

	if(arg) {
		arg->SetArg(name, node, value);
	}
	else {
		arg = new thArg(name, node, value);
		args->Insert((void *)strdup(name), arg);
	}
	return arg;
}

const thArgValue *thNode::GetArg (const char *name)
{
	thArg *arg = (thArg *)args->GetData((void *)name);

	return arg->GetArg();
}

void thNode::PrintArgs (void)
{
	PrintArgs(args);
}

void thNode::PrintArgs (thBSTree *node)
{
	if(!node) {
		return;
	}

	PrintArgs(node->GetLeft());
	printf("%s\n", (char *)node->GetId());
	PrintArgs(node->GetRight());
}

void thNode::CopyArgs (thBSTree *newargs)
{
	thArg *newarg;
	thArgValue *data;
	float *newvalues;

	if(!newargs) {
		return;
	}

	CopyArgs(newargs->GetLeft());

	data = (thArgValue *)((thArg *)newargs->GetData())->GetArg();
	if(data->argType == ARG_VALUE) {
	  newvalues = new float[data->argNum];
	  memcpy(newvalues, data->argValues, data->argNum*sizeof(float));
		newarg = new thArg(data->argName, newvalues, data->argNum);
	}
	else if(data->argType == ARG_POINTER) {
		newarg = new thArg(data->argName, data->argPointNode,
						   data->argPointName);

	}

	args->Insert(data->argName, newarg);


	CopyArgs(newargs->GetRight());
}

void thNode::Process (void)
{
}
