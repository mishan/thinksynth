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

thNode::thNode (char *name, thPlugin *thplug)
{
	nodename = strdup(name);
	plugin = thplug;
	recalc = false;
}

thNode::~thNode (void)
{
	free(nodename);
	/* free anything else */
}

void thNode::SetName(const char *name)
{
	free(nodename);
	nodename = strdup(name);
}

void thNode::SetArg (const char *name, const float *value, int num)
{
	thArg *arg = (thArg *)args.GetData(name);

	if(!arg) {
		arg = new thArg(name, value, num);
		args.Insert(name, arg);
	}
	else {
		arg->SetArg(name, value, num);
	}
}

void thNode::SetArg (const char *name, const char *node, const char *value)
{
	thArg *arg = (thArg *)args.GetData(name);
	
	if(arg) {
		arg->SetArg(name, node, value);
	}
	else {
		arg = new thArg(name, node, value);
		args.Insert(name, arg);
	}
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

void thNode::Process (void)
{
}
