/*
 * Copyright (C) 2004-2014 Metaphonic Labs
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
    memset(connections_, 0, sizeof(connections_));
    connectionMap_.clear();
}

thMidiController::~thMidiController (void)
{
    /* XXX: later on, check for connection classes and nuke them */
}

void thMidiController::handleMidi (unsigned char channel, unsigned int param,
                                   unsigned int value)
{
    if (!validAddress(channel, param))
        return;

    thMidiControllerConnection *connectionptr = connections_[channel][param];

    if (connectionptr)
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

    if (!validAddress(channel, param))
        return;

    if (connection == NULL)
    {
        connections_[channel][param] = 0;
        connectionMap_.erase(channel * TH_MIDI_CONTROLLERS + param);
    }
    else
    {
        connections_[channel][param] = connection;
        connectionMap_[channel * TH_MIDI_CONTROLLERS + param] = connection;
    }
}

void thMidiController::clearByDestChan (unsigned int chan)
{
    thMidiControllerConnection *connection;
    map<unsigned int, thMidiControllerConnection*>::iterator i =
        connectionMap_.begin();

    while (i != connectionMap_.end())
    {
        map<unsigned int, thMidiControllerConnection*>::iterator j = i;
        connection = (i++)->second;

        if ((unsigned int)connection->destChan() == chan)
        {
            int chan = connection->chan(),
                controller = connection->controller();

            /* The connection carries its own address back, so this is only
               in range if it was in range when the connection was made.
               newConnection now refuses out-of-range ones, but a connection
               built before this check existed is still in a saved .thinkrc. */
            if (chan >= 0 && controller >= 0 &&
                validAddress((unsigned int)chan, (unsigned int)controller))
                connections_[chan][controller] = NULL;

            connectionMap_.erase(j);
        }
    }
}
