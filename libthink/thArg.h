/* $Id: thArg.h,v 1.26 2003/06/03 23:05:06 aaronl Exp $ */

#ifndef TH_ARG_H
#define TH_ARG_H 1

#define ARG_VALUE 0
#define ARG_POINTER 1



class thArg {
	public:
	thArg(const string &name, float *value, const int num);
	thArg(const string &name, const string &node, const string &value);
	thArg();
	~thArg();
	
	void SetArg(const string &name, float *value, const int num);
	void SetAllocatedArg(const string &name, float *value, const int num);
	void SetArg(const string &name, const string &node, const string &value);

	string GetArgName (void) const { return argName; };

	float GetValue (int position) const { return (argType != ARG_POINTER); };
	float *allocate (int elements);
	string argName; /* argument's name */
	float *argValues; /* a pointer to an array of values */
	int argNum; /* number of elements in argValues */
	string argPointNode; /* name of the node a pointer points to */
	string argPointName; /* name of the argument a pointer points to */
	int argType; /* is this arg a value or a pointer? */
	float& operator[] (int i) const {
		if(argNum == 1) {
			return argValues[0];
		}
		if(i < argNum) {
			return argValues[i];
		}
		return argValues[i%argNum];
	}
};

#endif /* TH_ARG_H */
