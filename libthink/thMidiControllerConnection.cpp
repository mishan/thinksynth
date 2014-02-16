/* $Id$ */
/*
 * Copyright (C) 2004-2005 Metaphonic Labs
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

#include <math.h>
#include <stdio.h>

#include "think.h"

thMidiControllerConnection::thMidiControllerConnection (thArg *arg, float min,
														float max, int scale,
														int chan,
														int controller,
														int dchan,
														string argName)
{
	arg_ = arg;
	min_ = min;
	max_ = max;
	chan_ = chan;
	controller_ = controller;
	destchan_ = dchan;
	argName_ = argName;
	scale_ = scale;
}

thMidiControllerConnection::~thMidiControllerConnection (void)
{
}

void thMidiControllerConnection::setParam (unsigned int value)
{
	float realvalue;

	realvalue = (float)value/MIDIVALMAX;

	if (scale_ == EXPONENTIAL) /* give it a nice curve */
		realvalue = 1 - logf(M_E - (realvalue * (M_E - 1)));

	arg_->setValue(realvalue * (max_ - min_) + min_);
}
