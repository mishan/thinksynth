/* $Id: thArg.cpp,v 1.34 2003/09/16 01:02:29 misha Exp $ */

#include "config.h"
#include "think.h"

#include "thArg.h"


/* Ownership of value will transfer to us. Same goes for SetAllocatedArg. */

thArg::thArg(const string &name, float *value, const int num)
{
	argName = name;
	argValues = value;
	argNum = num;
	argType = ARG_VALUE;
}

thArg::thArg(const string &name, const string &node, const string &value)
{
	argName = name;
	argPointNode = node;
	argPointName = value;
	argValues = NULL;
	argNum = 0;

	argType = ARG_POINTER;
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


float *thArg::Allocate (int elements)
{
	if(argNum != elements) {
		delete[] argValues;
		argValues = new float[elements];
		argNum = elements;
	}
	else if(argValues == NULL) {
		argValues = new float[elements];
		argNum = elements;
	}
	return argValues;
}

void thArg::SetArg(const string &name, float *value, const int num)
{
	float *values;

	values = Allocate(num);

	argName = name;
	
	memcpy(argValues, value, num);

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
