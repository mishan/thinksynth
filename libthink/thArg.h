#ifndef TH_ARG_H
#define TH_ARG_H 1

enum thArgType { ARG_FLOAT, ARG_PTR, ARG_STR };

struct thArgValue {
	char *argName; /* argument's name */
	void *argValues;
	int argNum; /* number of elements in argValues */

	thArgType argType; /* denotes datatype of argValues */
};

class thArg {
public:
	thArg(char *name, float *value, int num);
	thArg();
	~thArg();
	
	void SetArg(char *name, float *value, int num);

	int GetCount(void);

	/* it's wise to GetCount() before you GetArg() */
	const thArgValue *GetArg(void);
private:
 	char *argName;
	float *argValues;
	int argNum;
};

#endif /* TH_ARG_H */
