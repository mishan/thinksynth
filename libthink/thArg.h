/* $Id: thArg.h,v 1.19 2003/04/27 09:44:51 ink Exp $ */

#ifndef TH_ARG_H
#define TH_ARG_H 1

#define ARG_VALUE 0
#define ARG_POINTER 1

struct thArgValue {
	char *argName; /* argument's name */
	float *argValues; /* a pointer to an array of values */
	int argNum; /* number of elements in argValues */
	char *argPointNode; /* name of the node a pointer points to */
	char *argPointName; /* name of the argument a pointer points to */
	int argType; /* is this arg a value or a pointer? */
	float& operator[] (int i) { return argValues[i%argNum]; }
};

class thArg {
	public:
		thArg(const char *name, float *value, const int num);
		thArg(const char *name, const char *node, const char *value);
		thArg();
		~thArg();
	
		void SetArg(const char *name, float *value, const int num);
		void SetArg(const char *name, const char *node, const char *value);

		char *GetArgName (void) const { return argValue.argName; };
		const thArgValue *GetArg(void) { return &argValue; };

		float GetValue (int position) const { return (argValue.argType != ARG_POINTER); };

	private:
		thArgValue argValue;
};

#endif /* TH_ARG_H */
