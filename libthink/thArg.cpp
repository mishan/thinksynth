/* $Id: thArg.cpp,v 1.22 2003/04/27 08:57:16 aaronl Exp $ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "thArg.h"

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
	if (argValue.argValues) {
		delete[] argValue.argValues;
	}
	free (argValue.argName);
	free (argValue.argPointNode);
	free (argValue.argPointName);
}

void thArg::SetArg(const char *name, float *value, const int num)
{
	if(argValue.argName) {
		delete argValue.argName;
	}

	if(argValue.argValues) {
		delete argValue.argValues;
	}

	argValue.argName = strdup(name);
	
	argValue.argValues = value;

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
