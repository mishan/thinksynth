#include "config.h"

#include <unistd.h>
#include "endian.h"

/* read() and write() wrappers for maintaing data in little-endian 
   byte-ordering; everything outside these wrappers should be stored in
   host-endian format */

int leread32(int fd, long *c)
{
	int r;

	r = read(fd, c, 4);
	le32(*c, *c);

	return r;
}

int leread16(int fd, short *c)
{
	int r;

	r = read(fd, c, 2);
	le16(*c, *c);

	return r;
}

int lewrite32(int fd, long c)
{
	int r;

	le32(c, c);
	r = write(fd, &c, 4);

	return r;
}

int lewrite16(int fd, short c)
{
	int r;

	le16(c, c);
	r = write(fd, &c, 2);

	return r;
}

/* read() and write() wrappers for maintaing data in big-endian 
   byte-ordering */

int beread32(int fd, long *c)
{
	int r;

	r = read(fd, c, 4);
	be32(*c, *c);

	return r;
}

int beread16(int fd, short *c)
{
	int r;

	r = read(fd, c, 2);
	be16(*c, *c);

	return r;
}

int bewrite32(int fd, long c)
{
	int r;

	be32(c, c);
	r = write(fd, &c, 4);

	return r;
}

int bewrite16(int fd, short c)
{
	int r;

	be16(c, c);
	r = write(fd, &c, 2);

	return r;
}
