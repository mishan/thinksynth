/* $Id: thNode.cpp,v 1.52 2004/02/18 23:41:16 ink Exp $ */

#include "think.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "thArg.h"
#include "thPlugin.h"
#include "thNode.h"

thNode::thNode (const string &name, thPlugin *thplug)	
{
	plugin = thplug;
	nodename = name;
	recalc = false;
}

thNode::~thNode (void)
{
	DestroyMap(args);
}

thArg *thNode::SetArg (const string &name, float *value, int num)
{
	map<string, thArg*>::const_iterator i = args.find(name);
	thArg *arg;

	if(i == args.end() || !i->second /* XXX shouldnt be necessary */) {
		arg = new thArg(name, value, num);
		args[name] = arg;
	}
	else {
		arg = i->second;
		arg->SetArg(name, value, num);
	}
	return arg;
}

thArg *thNode::SetArg (const string &name, const string &node, const string &value)
{
	map<string, thArg*>::const_iterator i = args.find(name);
	thArg *arg;

	if(i != args.end() && i->second /* XXX we should not have to do this */) {
		arg = i->second;
		arg->SetArg(name, node, value);
	}
	else {
		arg = new thArg(name, node, value);
		args[name] = arg;
	}
	return arg;
}

void thNode::PrintArgs (void)
{
	for (map<string,thArg*>::const_iterator i = args.begin(); i != args.end(); i++)
		printf("%s\n", i->first.c_str());
}

void thNode::CopyArgs (const map<string, thArg*> &newargs)
{
	thArg *newarg;
	thArg *data;
	float *newvalues;

	for (map<string,thArg*>::const_iterator i = (newargs).begin(); i != (newargs).end(); i++)
	{
		data = i->second;
		if(data->argType == ARG_VALUE) {
			newvalues = new float[data->argNum];
			memcpy(newvalues, data->argValues, data->argNum*sizeof(float));
			newarg = new thArg(data->argName, newvalues, data->argNum);
		}
		else if(data->argType == ARG_POINTER) {
			newarg = new thArg(data->argName, data->argPointNode,
							   data->argPointName);
			newarg->argPointNodeID = data->argPointNodeID;
		}
		else continue;
		args[data->argName] = newarg;
	}
}

void thNode::Process (void)
{
}
