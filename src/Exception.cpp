#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Exception.h"

thException::thException(void)
{
	message = NULL;
}

thException::thException(const char *msg)
{
	printf("construcor called with %s\n", msg);
	message = strdup(msg);
}

thException::~thException (void)
{
	if(message) {
		delete message;
	}
}

const char *thException::string (void)
{
	return message;
}
