/* $Id: thArg.cpp,v 1.31 2003/05/17 14:51:07 ink Exp $ */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "thArg.h"


/* Gotta take care of the thArgValue class */
thArgValue::thArgValue (void)
{
	argNum = 0;
}

thArgValue::~thArgValue (void)
{
	if(argValues) {
		delete[] argValues;
	}
}

float *thArgValue::allocate (int elements)
{
	printf("Reallocate called on %s, %i %i\n", argName, argNum, elements);
	if(argNum != elements) {
		delete[] argValues;
		argValues = new float[elements];
	}
	else if(argValues == NULL) {
		argValues = new float[elements];
	}
	return argValues;
}

/* Ownership of value will transfer to us, but we copy the strings and you need
   to free those. Same goes for SetArg. */

thArg::thArg(const char *name, float *value, const int num)
{
	argValue.argName = strdup(name);

	argValue.argValues = value;

	argValue.argNum = num;

	argValue.argPointNode = NULL;
	argValue.argPointName = NULL;

	argValue.argType = ARG_VALUE;
}

thArg::thArg(const char *name, const char *node, const char *value)
{
	argValue.argName = strdup(name);
	argValue.argPointNode = strdup(node);
	argValue.argPointName = strdup(value);
	argValue.argValues = NULL;
	argValue.argNum = 0;

	argValue.argType = ARG_POINTER;
}

thArg::thArg (void) /* the equivalent of creating a thArg(NULL, NULL, 0) */
{
}

thArg::~thArg(void)
{
/*	if (argValue.argValues) {
		delete[] argValue.argValues;
		}*/ // This now handled in deconstructor, add the rest
	free (argValue.argName);
	free (argValue.argPointNode);
	free (argValue.argPointName);
}

void thArg::SetArg(const char *name, float *value, const int num)
{
	float *values;

	if(argValue.argName) {
		free (argValue.argName);
	}

	values = argValue.allocate(num);

	argValue.argName = strdup(name);
	
	memcpy(argValue.argValues, value, num);

	argValue.argNum = num;
	argValue.argType = ARG_VALUE;
}

void thArg::SetArg(const char *name, const char *node, const char *value)
{
	if(argValue.argName) {
		delete argValue.argName;
	}

	if(argValue.argPointNode) {
		delete argValue.argPointNode;
	}

	if(argValue.argPointName) {
		delete argValue.argPointName;
	}

	argValue.argName = strdup(name);
	argValue.argPointNode = strdup(node);
	argValue.argPointName = strdup(value);

	argValue.argType = ARG_POINTER;
}
