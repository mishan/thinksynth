/* $Id: thArg.h,v 1.29 2004/02/18 23:41:16 ink Exp $ */

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

	float *Allocate (unsigned int elements);
	string argName; /* argument's name */
	float *argValues; /* a pointer to an array of values */
	unsigned int argNum; /* number of elements in argValues */
	string argPointNode; /* name of the node a pointer points to */
	string argPointName; /* name of the argument a pointer points to */
	int argPointNodeID; /* index of the node to which the pointer points */
	int argType; /* is this arg a value or a pointer? */
	float operator[] (unsigned int i) const {
		if(!argNum) {
			return 0;
		}
		else if(argNum == 1) {
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
