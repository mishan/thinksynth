/* $Id: thArg.cpp,v 1.52 2004/11/11 03:20:12 ink Exp $ */
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

#include <string.h>

#include "think.h"

/* Ownership of value will transfer to us. Same goes for SetAllocatedArg. */

thArg::thArg(const string &name, float *value, const int num)
{
	name_ = name;
	values_ = value;
	len_ = num;

	argType = ARG_VALUE;
	argPointNodeID = -1;   /* so we know it has not been set yet */
	argPointArgID = -1;   /* so we know it has not been set yet */

	widgetType_ = HIDE;
	min_ = 0;
	max_ = MIDIVALMAX;
}

thArg::thArg(const string &name, const string &node, const string &value)
{
	name_ = name;
	values_ = NULL;
	len_ = 0;

	argPointNode = node;
	argPointName = value;

	argType = ARG_POINTER;
	argPointNodeID = -1;   /* so we know it has not been set yet */
	argPointArgID = -1;   /* so we know it has not been set yet */

	widgetType_ = HIDE;
	min_ = 0;
	max_ = MIDIVALMAX;
}

thArg::thArg(const string &name, const string &chanarg)
{
    name_ = name;
    values_ = NULL;
    len_ = 0;

    argPointName = chanarg;

    argType = ARG_CHANNEL;
    argPointNodeID = -1;   /* so we know it has not been set yet */
    argPointArgID = -1;   /* so we know it has not been set yet */
	argPointArg = NULL;

	widgetType_ = HIDE;
	min_ = 0;
	max_ = MIDIVALMAX;
}


thArg::thArg (const thArg *copyArg)
{
	name_ = copyArg->name_;

	len_ = copyArg->len_;
	values_ = new float[len_];
	memcpy(values_, copyArg->values_, len_ * sizeof(float));
	
	comment_ = copyArg->comment_;
	label_ = copyArg->label_;
	units_ = copyArg->units_;
	min_ = copyArg->min_;
	max_ = copyArg->max_;
	widgetType_ = copyArg->widgetType_;

	argPointName = copyArg->argPointName;
	argType = copyArg->argType;
	argPointNodeID = copyArg->argPointNodeID;
	argPointArgID = copyArg->argPointArgID;
	argPointArg = copyArg->argPointArg;
}

/* the equivalent of creating a thArg(NULL, NULL, 0) */
thArg::thArg (void)
{
	values_ = NULL;
	len_ = 0;
	widgetType_ = HIDE;
    argPointNodeID = -1;   /* so we know it has not been set yet */
    argPointArgID = -1;   /* so we know it has not been set yet */
	argPointArg = NULL;
}

thArg::~thArg(void)
{
	if (values_) {
		delete[] values_;
	}
}

float *thArg::Allocate (unsigned int elements)
{
	if(values_ == NULL) {
		values_ = new float[elements];
		len_ = elements;
	}
	else if(len_ != elements) {
		delete[] values_;
		values_ = new float[elements];
		len_ = elements;
	}

	return values_;
}

void thArg::SetArg(const string &name, float *value, const int num)
{
	values_ = Allocate(num);
	
	name_ = name;
	
	memcpy(values_, value, num * sizeof(float));

	len_ = num;
	argType = ARG_VALUE;
}

void thArg::SetAllocatedArg(const string &name, float *value, const int num)
{
	values_ = value;
	name_ = name;
	len_ = num;
	argType = ARG_VALUE;
}

void thArg::SetArg(const string &name, const string &node, const string &value)
{
	name_ = name;
	argPointNode = node;
	argPointName = value;
	
	argType = ARG_POINTER;
}

void thArg::SetArg(const string &name, const string &chanarg)
{
    name_ = name;
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
			buffer[i] = values_[j];
			if(++j >= len_)
			{
				j = 0;
			}
		}
	}
}

void thArg::SetValue(float value)
{
	values_ = Allocate(1);
	values_[0] = value;
}

void thArg::SetValues(float *values, int len)
{
	Allocate(len);
	memcpy(values_, values, len);
}
