/* $Id: thArg.cpp,v 1.47 2004/08/16 09:34:48 misha Exp $ */
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

#include "think.h"

/* Ownership of value will transfer to us. Same goes for SetAllocatedArg. */

thArg::thArg(const string &name, float *value, const int num)
{
	argName = name;
	argValues = value;
	argNum = num;
	argType = ARG_VALUE;
	argPointNodeID = -1;   /* so we know it has not been set yet */
	argPointArgID = -1;   /* so we know it has not been set yet */
}

thArg::thArg(const string &name, const string &node, const string &value)
{
	argName = name;
	argPointNode = node;
	argPointName = value;
	argValues = NULL;
	argNum = 0;

	argType = ARG_POINTER;
	argPointNodeID = -1;   /* so we know it has not been set yet */
	argPointArgID = -1;   /* so we know it has not been set yet */
}

thArg::thArg(const string &name, const string &chanarg)
{
    argName = name;
    argPointName = chanarg;
    argValues = NULL;
    argNum = 0;

    argType = ARG_CHANNEL;
    argPointNodeID = -1;   /* so we know it has not been set yet */
    argPointArgID = -1;   /* so we know it has not been set yet */
	argPointArg = NULL;
}

/* the equivalent of creating a thArg(NULL, NULL, 0) */
thArg::thArg (void)
{
	argValues = NULL;
	argNum = 0;
}

thArg::~thArg(void)
{
	if (argValues) {
		delete[] argValues;
	}
}

float *thArg::Allocate (unsigned int elements)
{
	if(argValues == NULL) {
		argValues = new float[elements];
		argNum = elements;
	}
	else if(argNum != elements) {
		delete[] argValues;
		argValues = new float[elements];
		argNum = elements;
	}

	return argValues;
}

void thArg::SetArg(const string &name, float *value, const int num)
{
	argValues = Allocate(num);

	/* XXX: who wrote this crackheaded code?? */
//	string *argName = new string(name);
	
	argName = name;
	
	memcpy(argValues, value, num * sizeof(float));

	argNum = num;
	argType = ARG_VALUE;
}

void thArg::SetAllocatedArg(const string &name, float *value, const int num)
{
	argValues = value;
	argName = name;
	argNum = num;
	argType = ARG_VALUE;
}

void thArg::SetArg(const string &name, const string &node, const string &value)
{
	argName = name;
	argPointNode = node;
	argPointName = value;
	
	argType = ARG_POINTER;
}

void thArg::SetArg(const string &name, const string &chanarg)
{
    argName = name;
    argPointName = chanarg;

    argType = ARG_CHANNEL;
}

void thArg::GetBuffer(float *buffer, unsigned int size)
{
	unsigned int i, j;	

	if(argType == ARG_VALUE)
    {
		j = 0; /* depth into the arg float array (for the loop) */
		for(i = 0; i < size; i++)
		{
			buffer[i] = argValues[j];
			if(++j >= argNum)
			{
				j = 0;
			}
		}
	}
}
