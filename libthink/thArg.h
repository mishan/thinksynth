/* $Id: thArg.h,v 1.44 2004/11/11 10:42:41 misha Exp $ */
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

#ifndef TH_ARG_H
#define TH_ARG_H 1

#include <sigc++/sigc++.h>

class thArg;

typedef SigC::Signal1<void, thArg *> type_signal_arg_changed;

class thArg {
public:
	thArg(const string &name, float *value, const int num);
	thArg(const string &name, const string &node, const string &value);
	thArg(const string &name, const string &chanarg);
	thArg(const thArg *copyArg);
	thArg(void);
	~thArg(void);

	enum ArgType { ARG_VALUE = 0, ARG_POINTER, ARG_CHANNEL, ARG_NOTE };
	/* immidiate value, pointer to another node, pointer to a channel arg, or
	   pointer to a note arg. */

	enum WidgetType { HIDE = 0, SLIDER };
	
	void SetArg(const string &name, float *value, const int num);
	void SetAllocatedArg(const string &name, float *value, const int num);
	void SetArg(const string &name, const string &node, const string &value);
	void SetArg(const string &name, const string &chanarg);

	void SetValue(float value); /* set a single float value */
	void SetValues(float *values, int len); /* set a longer arg */

	void SetIndex (int i) { index_ = i; };
	int GetIndex (void) { return index_; };
	void GetBuffer(float *buffer, unsigned int size);

	unsigned int getLen (void) { return len_; }

	const string &getName (void) const { return name_; };
	const string &getLabel (void) const { return label_; };
	const string &getUnits (void) const { return units_; };
	const string &getComment (void) const { return comment_; };
	float getMin (void) { return min_; };
	float getMax (void) { return max_; };
	WidgetType getWidgetType (void) { return widgetType_; };

	void setLabel (const string &label) { label_ = label; };
	void setUnits (const string &units) { units_ = units; };
	void setComment (const string &comment) { comment_ = comment; };
	void setMin (float min) { min_ = min; };
	void setMax (float max) { max_ = max; };
	void setWidgetType (WidgetType widgetType) { widgetType_ = widgetType; };

	float *Allocate (unsigned int elements);

	string argPointNode; /* name of the node a pointer points to */
	string argPointName; /* name of the argument a pointer points to */
	int argPointNodeID; /* index of the node to which the pointer points */
	int argPointArgID;  /* index of the arg to which the pointer points */
	thArg *argPointArg; /* actual pointer to another arg- for midi chan and
						   note args */
	ArgType argType; /* is this arg a value or a pointer? */

	string name_; /* argument's name */
	int index_; /* where in the arg index this arg is located */
	float *values_; /* a pointer to an array of values */
	unsigned int len_; /* number of elements in argValues */

	/* Okay, a bit more info about the data */
	float min_, max_;  /* for knobs and stuff, I'm sure it will be useful
							  elsewhere, too */
	WidgetType widgetType_;  /* our widget type */
	string label_;  /* This will be displayed in the UI */
	string units_;  /* This will be displayed too...  ms, Hz, sec etc... */
	string comment_;

	type_signal_arg_changed signal_arg_changed (void) {
		return m_signal_arg_changed;
	}

	/* In the near future we have to also implement some way of limiting the
	   values...  like only ints, or only certain fractions (some things will
	   probably use this, I suggest some kind of array of acceptable values,
	   with some way of specifying ints only...)  But just default to all
	   floats between argMin and argMax. */
	/* We will probably also want more things here later, but this was all I
	   could think of NEEDING right now. */

	float operator[] (unsigned int i) const {
		/* empty */
		if(len_ == 0) 
		{
			return 0;
		}
		
		else if(len_ == 1)
		{
			return values_[0];
		}
		else if(i < len_) {
			return values_[i];
		}

		/* else */
		return values_[i%len_];
	}

private:
	type_signal_arg_changed m_signal_arg_changed;
};

#endif /* TH_ARG_H */
