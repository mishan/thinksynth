#ifndef TH_ARG_H
#define TH_ARG_H 1

class thArg {
public:
	thArg(char *name, float *value, int num);
	thArg();
	~thArg();
	
	void SetArg(float *value, int num);
private:
};

#endif /* TH_ARG_H */
