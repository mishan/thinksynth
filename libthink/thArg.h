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

#ifndef TH_ARG_H
#define TH_ARG_H 1

#include <sigc++/sigc++.h>

class thArg;

typedef sigc::signal1<void, thArg *> type_signal_arg_changed;

class thArg {
public:
	thArg  (const string &name, float value);
	thArg  (const string &name, const float *value, int num);
	thArg  (const string &name, const string &node, const string &value);
	thArg  (const string &name, const string &chanarg);
	thArg  (const thArg *copyArg);
	thArg  (void);
	~thArg (void);

	enum ArgType { ARG_VALUE = 0, ARG_POINTER, ARG_CHANNEL, ARG_NOTE };
	/* immidiate value, pointer to another node, pointer to a channel arg, or
	   pointer to a note arg. */

	enum WidgetType { HIDE = 0, SLIDER, CHANARG };
	
	void setArg(const string &name, float value);
	void setArg(const string &name, const float *value, int len);
	void setArg(const string &name, const string &node, const string &value);
	void setArg(const string &name, const string &chanarg);

	void setValue(float value); /* set a single float value */
	void setValue(float *values, int len); /* set a longer arg */

	void setIndex (int i) { index_ = i; };
	int index (void) { return index_; };
	void getBuffer (float *buffer, unsigned int size);

	unsigned int len (void) { return len_; }

	const string &name (void) const { return name_; };
	const string &label (void) const { return label_; }
	const string &units (void) const { return units_; }
	const string &comment (void) const { return comment_; }

	float min (void) const { return min_; }
	float max (void) const { return max_; }

	ArgType type (void) const { return type_; }
	WidgetType widgetType (void) const { return widgetType_; }

	void setLabel (const string &label) { label_ = label; };
	void setUnits (const string &units) { units_ = units; };
	void setComment (const string &comment) { comment_ = comment; };
	void setMin (float min) { min_ = min; };
	void setMax (float max) { max_ = max; };
	void setWidgetType (WidgetType widgetType) { widgetType_ = widgetType; };

	float *allocate (unsigned int elements);

	const string &nodePtrName (void) const { return nodePtrName_; }
	const string &argPtrName (void) const { return argPtrName_; }

	int nodePtrId (void) const { return nodePtrId_; }
	void setNodePtrId (int id) { nodePtrId_ = id; }

	int argPtrId (void) const { return argPtrId_; }
	void setArgPtrId (int id) { argPtrId_ = id; }

	thArg *argPtr (void) const { return argPtr_; }
	void setArgPtr (thArg *arg) { argPtr_ = arg; }

	float *values (void) const { return values_; }

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
		if (len_ == 0) 
		{
			return 0;
		}
		
		else if (len_ == 1)
		{
			return values_[0];
		}
		else if (i < len_) {
			return values_[i];
		}

		/* else */
		return values_[i%len_];
	}
protected:
	string nodePtrName_; /* name of the node a pointer points to */
	string argPtrName_;  /* name of the argument a pointer points to */
	int nodePtrId_;      /* index of the node to which the pointer points */
	int argPtrId_;       /* index of the arg to which the pointer points */
	thArg *argPtr_;      /* actual pointer to another arg- for midi chan and
							note args */

	ArgType type_;       /* is this arg a value or a pointer? */
	string name_;        /* argument's name */
	int index_;          /* where in the arg index this arg is located */
	float *values_;      /* a pointer to an array of values */
	unsigned int len_;   /* number of elements in argValues */

	/* Okay, a bit more info about the data */
	float min_, max_;        /* for knobs and stuff, I'm sure it will be useful
								elsewhere, too */
	WidgetType widgetType_;  /* our widget type */
	string label_;           /* This will be displayed in the UI */
	string units_;           /* This will be displayed too. ms, Hz, sec etc.*/
	string comment_;

	type_signal_arg_changed m_signal_arg_changed;
};

#endif /* TH_ARG_H */
