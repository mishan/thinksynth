/* $Id: thNode.cpp,v 1.55 2004/05/09 01:04:38 misha Exp $ */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "think.h"

thNode::thNode (const string &name, thPlugin *thplug)	
{
	plugin = thplug;
	nodename = name;
	recalc = false;

	argindex = (thArg **)calloc(ARGCHUNK, sizeof(thArg *));
	argcounter = 0;
	argsize = ARGCHUNK;
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
		arg->SetIndex(AddArgToIndex(arg));
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
		arg->SetIndex(AddArgToIndex(arg));
	}

	return arg;
}

int thNode::AddArgToIndex (thArg *arg)
{
	thArg **newindex;

	if(argcounter >= argsize)
	{
		newindex = (thArg **)calloc(argsize + ARGCHUNK, sizeof(thArg *));

		memcpy(newindex, argindex, argcounter * sizeof(thArg*));
		free(argindex);
		argindex = newindex;
		argsize += ARGCHUNK;
	}

	argindex[argcounter] = arg;
	argcounter++;

	return (argcounter - 1);
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
	thArg **newindex;

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
			newarg->argPointArgID = data->argPointArgID;
		}
		else continue;
		args[data->argName] = newarg;

		newarg->SetIndex(data->GetIndex());
		while(newarg->GetIndex() > argsize)
		{
			newindex = (thArg **)calloc(argsize + ARGCHUNK, sizeof(thArg *));
			
			memcpy(newindex, argindex, argsize * sizeof(thArg*));
			free(argindex);
			argindex = newindex;
			argsize += ARGCHUNK;
		}
		argindex[newarg->GetIndex()] = newarg;
	}
}

void thNode::Process (void)
{
}
