#ifndef HAVE_EXCEPTION_H
#define HAVE_EXCEPTION_H 1

typedef int IOException;

class Exception
{
public:
	Exception(void);
	Exception(const char *msg);

	~Exception();

	const char *string(void);
private:
	char *message;
};

#endif /* HAVE_EXCEPTION_H */
