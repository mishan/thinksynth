/* $Id: thMidiController.cpp,v 1.4 2004/11/10 21:25:52 ink Exp $ */
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

/* See thMidiController.h for a blurb on what this class is all about */

#include <string.h>
#include <stdio.h>

#include "think.h"

thMidiController::thMidiController (void)
{
	/* zero the pointer list */
	memset(connections_, 0, 16 * 128 * sizeof(thMidiControllerConnection*));
	connectionList_.clear();
}

thMidiController::~thMidiController (void)
{
	/* XXX: later on, check for connection classes and nuke them */
}

void thMidiController::handleMidi (unsigned char channel, unsigned int param,
								   unsigned int value)
{
	thMidiControllerConnection *connectionptr = connections_[channel][param];

	if(connectionptr)
	{
		connectionptr->setParam(value);

	}
}

void thMidiController::newConnection (unsigned char channel,
									  unsigned int param,
									  thMidiControllerConnection *connection)
{
	/* XXX: Do a NULL-check, and if there is something here, tack it on
	   the linked list */

	if(connection == NULL)
	{
		connections_[channel][param] = 0;
		connectionList_.erase(channel * 128 + param);
	}
	else
	{
		connections_[channel][param] = connection;
		connectionList_[channel * 128 + param] = connection;
	}
}
