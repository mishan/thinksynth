/* $Id: thArg.cpp,v 1.39 2004/04/08 00:34:56 misha Exp $ */

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
}

thArg::thArg (void) /* the equivalent of creating a thArg(NULL, NULL, 0) */
{
	argValues = NULL;
	argNum = 0;
}

thArg::~thArg(void)
{
	if(argValues) {
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

	string *argName = new string(name);
	
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
