#ifndef TH_ARG_H
#define TH_ARG_H 1


struct thArgValue {
	char *argName; /* argument's name */
	float *argValues; /* a pointer to an array of values */
	int argNum; /* number of elements in argValues */
};

class thArg {
public:
	thArg(char *name, float *value, int num);
	thArg();
	~thArg();
	
	void SetArg(char *name, float *value, int num);

	const thArgValue *GetArg(void);
private:
 	char *argName;
	float *argValues;
	int argNum;
};

#endif /* TH_ARG_H */
