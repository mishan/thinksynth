#ifndef HAVE_EXCEPTION_H
#define HAVE_EXCEPTION_H 1

typedef int thIOException;

class thException
{
public:
	thException(void);
	thException(const char *msg);

	~thException();

	const char *string(void);
private:
	char *message;
};

#endif /* HAVE_EXCEPTION_H */
