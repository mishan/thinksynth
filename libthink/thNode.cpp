/* $Id: thNode.cpp,v 1.35 2003/04/25 22:22:20 ink Exp $ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "thArg.h"
#include "thList.h"
#include "thBSTree.h"
#include "thPlugin.h"
#include "thPluginManager.h"
#include "thNode.h"

thNode::thNode (const char *name, thPlugin *thplug)	
{
	plugin = thplug;
	nodename = strdup(name);
	recalc = false;
}

thNode::~thNode (void)
{
	free(nodename);
	/* free anything else */
}

/* We own this string. The caller may not free it. */
void thNode::SetName(char *name)
{
	free(nodename);
	nodename = name;
}

thArg *thNode::SetArg (const char *name, float *value, int num)
{
	thArg *arg = (thArg *)args.GetData(name);

	if(!arg) {
		arg = new thArg(name, value, num);
		args.Insert(name, arg);
	}
	else {
		arg->SetArg(name, value, num);
	}
	return arg;
}

thArg *thNode::SetArg (const char *name, const char *node, const char *value)
{
	thArg *arg = (thArg *)args.GetData(name);
	
	if(arg) {
		arg->SetArg(name, node, value);
	}
	else {
		arg = new thArg(name, node, value);
		args.Insert(name, arg);
	}
	return arg;
}

const thArgValue *thNode::GetArg (const char *name)
{
	thArg *arg = (thArg *)args.GetData(name);

	return arg->GetArg();
}

void thNode::PrintArgs (void)
{
	args.PrintTree();
}

void thNode::SetPlugin (thPlugin *plug)
{
	plugin = plug;
}

void thNode::CopyArgs (thList *newargs)
{
	thListNode *listnode;
	thArg *newarg;
	thArgValue *data;

	for(listnode = newargs->GetHead(); listnode; listnode = listnode->prev) {
		data = (thArgValue *)((thArg *)listnode->data)->GetArg();
		if(data->argType == ARG_VALUE) {
			newarg = new thArg(data->argName, data->argValues, data->argNum);
		}
		else if(data->argType == ARG_POINTER) {
			newarg = new thArg(data->argName, data->argPointNode, data->argPointName);
		}
		args.Insert(data->argName, newarg);
	}
}

void thNode::Process (void)
{
}
