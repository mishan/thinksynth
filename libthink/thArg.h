/* $Id: thArg.h,v 1.39 2004/08/01 09:57:40 misha Exp $ */

#ifndef TH_ARG_H
#define TH_ARG_H 1


class thArg {
public:
	thArg(const string &name, float *value, const int num);
	thArg(const string &name, const string &node, const string &value);
	thArg(const string &name, const string &chanarg);
	thArg(void);
	~thArg(void);

	enum thArgType { ARG_VALUE = 0, ARG_POINTER, ARG_CHANNEL, ARG_NOTE };
	/* immidiate value, pointer to another node, pointer to a channel arg, or
	   pointer to a note arg. */

	enum WidgetType { HIDE = 0, SLIDER };
	
	void SetArg(const string &name, float *value, const int num);
	void SetAllocatedArg(const string &name, float *value, const int num);
	void SetArg(const string &name, const string &node, const string &value);
	void SetArg(const string &name, const string &chanarg);

	void SetIndex (int i) { argIndex = i; };
	int GetIndex (void) { return argIndex; };
	void GetBuffer(float *buffer, unsigned int size);

	const string &GetArgName (void) const { return argName; };
	const string &GetArgLabel (void) const { return argLabel; };
	const string &GetArgUnits (void) const { return argUnits; };
	float GetArgMin (void) { return argMin; };
	float GetArgMax (void) { return argMax; };
	WidgetType GetArgWidget (void) { return argWidget; };

	void SetArgLabel (const string &label) { argLabel = label; };
	void SetArgUnits (const string &units) { argUnits = units; };
	void SetArgMin (float min) { argMin = min; };
	void SetArgMax (float max) { argMax = max; };
	void SetArgWidget (WidgetType widget) { argWidget = widget; };

	float *Allocate (unsigned int elements);


	string argName; /* argument's name */
	int argIndex; /* where in the arg index this arg is located */
	float *argValues; /* a pointer to an array of values */
	unsigned int argNum; /* number of elements in argValues */
	string argPointNode; /* name of the node a pointer points to */
	string argPointName; /* name of the argument a pointer points to */
	int argPointNodeID; /* index of the node to which the pointer points */
	int argPointArgID;  /* index of the arg to which the pointer points */
	thArg *argPointArg; /* actual pointer to another arg- for midi chan and
						   note args */
	thArgType argType; /* is this arg a value or a pointer? */

	/* Okay, a bit more info about the data */
	float argMin, argMax;  /* for knobs and stuff, I'm sure it will be useful
							  elsewhere, too */
	WidgetType argWidget;  /* XXX We must #define widget types, like slider,
							   knob, input box, etc etc etc...  This is
							   optional, and will only really be used by io
							   node args */
	/* if argWidget is 0, the parameter is not to be displayed in the UI */
	string argLabel;  /* This will be displayed in the UI */
	string argUnits;  /* This will be displayed too...  ms, Hz, sec etc... */

	/* In the near future we have to also implement some way of limiting the
	   values...  like only ints, or only certain fractions (some things will
	   probably use this, I suggest some kind of array of acceptable values,
	   with some way of specifying ints only...)  But just default to all
	   floats between argMin and argMax. */
	/* We will probably also want more things here later, but this was all I
	   could think of NEEDING right now. */

	float operator[] (unsigned int i) const {
		if(!argNum) {
			return 0;
		}
		else if(argNum == 1)
		{
			return argValues[0];
		}
		else if(i < argNum) {
			return argValues[i];
		}

		/* else */
		return argValues[i%argNum];
	}

};

#endif /* TH_ARG_H */
