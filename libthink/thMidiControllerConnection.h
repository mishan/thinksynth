/* $Id $ */
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

#ifndef TH_MIDICONTROLLERCONNECTION_H
#define TH_MIDICONTROLLERCONNECTION_H 1

/* This is the class that holds the actual data needed to set parameter values
   via midi controllers.  This includes minimum and maximum setting values,
   exponential/linear curve mapping and a pointer to the thArg being set. */

class thMidiControllerConnection {
public:
	thMidiControllerConnection (thArg *arg, float min, float max, int chan, 
								int controller, int dchan, string instrument,
								string argName);
	~thMidiControllerConnection (void);

	void setMin (float min) { min_ = min; }
	void setMax (float max) { max_ = max; }
	float getMin (void) { return min_; }
	float getMax (void) { return max_; }
	int getChan (void) { return chan_; }
	int getController (void) { return controller_; }
	int getDestChan (void) { return destchan_; }
	string getInstrument (void) { return instrument_; }
	string getArgName (void) { return argName_; }
	thArg *getArg (void) { return arg_; }
	/* XXX: put linear/exponential here too */

	void setParam (unsigned int value);
	
private:
	thArg *arg_;
	int chan_, controller_;
	int destchan_;
	string instrument_, argName_;
	float min_, max_;
	enum scale { LINEAR = 0, EXPONENTIAL };
	int scale_;
};

#endif /* TH_MIDICONTROLLERCONNECTION */
