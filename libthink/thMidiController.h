/* $Id$ */
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

#ifndef TH_MIDICONTROLLER_H
#define TH_MIDICONTROLLER_H 1

/* The thMidiController class has an array of 16 * 128 pointers to midi
   controller event handlers.  A thMidiControllerConnection has a pointer to
   the thArg to modify, and some information like min/max value, exponential/
   linear, etc.  Optionally, the thMidiControllerConnection can have
   instructions to the synth to handle a global operation such as portamento,
   foot pedal and RPN/NRPN parameter handling.  The thMidiControllerConnection
   class also has a pointer to optional addition thMidiControllerConnection
   classes, so one midi controller can control multiple parameters. */

class thMidiController {
public:
	thMidiController (void);
	~thMidiController (void);

	typedef map<unsigned int, thMidiControllerConnection *> ConnectionMap;

	void handleMidi (unsigned char channel, unsigned int param,
					 unsigned int value);

	void newConnection (unsigned char channel, unsigned int param,
						thMidiControllerConnection *connection);
	void clearByDestChan (unsigned int chan);

	ConnectionMap *getConnectionMap (void) { return &connectionMap_; }

	thMidiControllerConnection *getConnection (unsigned char channel, 
											   unsigned int param)
	{ 
		return connections_[channel][param];
	}

private:
	thMidiControllerConnection *connections_[16][128];
	ConnectionMap connectionMap_;
};

#endif /* TH_MIDICONTROLLER_H */
