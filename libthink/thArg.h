#ifndef TH_ARG_H
#define TH_ARG_H 1

class thArg {
public:
	thArg(char *name, float *value, int num);
	thArg();
	~thArg();
	
	void SetArg(char *name, float *value, int num);

	int GetCount(void);

	/* it's wise to GetCount() before you GetArg() */
	const float *GetArg(void);
private:
 	char *argName;
	float *argValues;
	int argNum;
};

#endif /* TH_ARG_H */
