/* $Id: thNode.cpp,v 1.59 2004/08/16 09:34:48 misha Exp $ */
/*
 * Copyright (C) 2004 Metaphonic Labs
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

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

thArg *thNode::SetArg (const string &name, const string &chanarg)
{
    map<string, thArg*>::const_iterator i = args.find(name);
    thArg *arg;

    if(i != args.end() && i->second /* XXX we should not have to do this */) {
        arg = i->second;
        arg->SetArg(name, chanarg);
    }
    else {
        arg = new thArg(name, chanarg);
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
		if(data->argType == thArg::ARG_VALUE) {
			newvalues = new float[data->argNum];
			memcpy(newvalues, data->argValues, data->argNum*sizeof(float));
			newarg = new thArg(data->argName, newvalues, data->argNum);
		}
		else if(data->argType == thArg::ARG_POINTER) {
			newarg = new thArg(data->argName, data->argPointNode,
							   data->argPointName);
			newarg->argPointNodeID = data->argPointNodeID;
			newarg->argPointArgID = data->argPointArgID;
		}
		else if(data->argType == thArg::ARG_CHANNEL) {
			newarg = new thArg(data->argName, data->argPointName);
			newarg->argPointArg = data->argPointArg;
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
