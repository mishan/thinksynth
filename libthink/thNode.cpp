/* $Id$ */
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

thArg *thNode::setArg (const string &name, float value)
{
	thArgMap::const_iterator i = args.find(name);
	thArg *arg;

	if(i == args.end() || !i->second /* XXX shouldnt be necessary */) {
		arg = new thArg(name, value);
		args[name] = arg;
		arg->setIndex(AddArgToIndex(arg));
	}
	else {
		arg = i->second;
		arg->setArg(name, value);
	}

	return arg;
}

thArg *thNode::setArg (const string &name, const float *value, int len)
{
	thArgMap::const_iterator i = args.find(name);
	thArg *arg;

	if(i == args.end() || !i->second /* XXX shouldnt be necessary */) {
		arg = new thArg(name, value, len);
		args[name] = arg;
		arg->setIndex(AddArgToIndex(arg));
	}
	else {
		arg = i->second;
		arg->setArg(name, value, len);
	}

	return arg;
}

thArg *thNode::setArg (const string &name, const string &node,
					   const string &value)
{
	thArgMap::const_iterator i = args.find(name);
	thArg *arg;

	if(i != args.end() && i->second /* XXX we should not have to do this */) {
		arg = i->second;
		arg->setArg(name, node, value);
	}
	else {
		arg = new thArg(name, node, value);
		args[name] = arg;
		arg->setIndex(AddArgToIndex(arg));
	}

	return arg;
}

thArg *thNode::setArg (const string &name, const string &chanarg)
{
    thArgMap::const_iterator i = args.find(name);
    thArg *arg;

    if(i != args.end() && i->second /* XXX we should not have to do this */) {
        arg = i->second;
        arg->setArg(name, chanarg);
    }
    else {
        arg = new thArg(name, chanarg);
        args[name] = arg;
        arg->setIndex(AddArgToIndex(arg));
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
	for (thArgMap::const_iterator i = args.begin(); i != args.end(); i++)
		printf("%s\n", i->first.c_str());
}

void thNode::CopyArgs (const map<string, thArg*> &newargs)
{
	thArg *newarg;
	thArg *data;
	thArg **newindex;

	for (map<string,thArg*>::const_iterator i = (newargs).begin(); i != (newargs).end(); i++)
	{
		data = i->second;

		if (data->type() == thArg::ARG_NOTE)
			continue;

		newarg = new thArg(data);

		args[data->name()] = newarg;

		newarg->setIndex(data->index());
		while(newarg->index() > argsize)
		{
			newindex = (thArg **)calloc(argsize + ARGCHUNK, sizeof(thArg *));
			
			memcpy(newindex, argindex, argsize * sizeof(thArg*));
			free(argindex);
			argindex = newindex;
			argsize += ARGCHUNK;
		}
		argindex[newarg->index()] = newarg;
	}
}

void thNode::Process (void)
{
}
