/* $Id: thEndian.h,v 1.3 2003/04/25 07:18:42 joshk Exp $ */

#ifndef HAVE_ENDIAN_H
#define HAVE_ENDIAN_H

#include <unistd.h>

/* Endianness stuff. Use only [lb]e{32,16}. With a decent compiler this has
 * no runtime overhead (try gcc 3.2 or higher). I like it because it requires no
 * build-time support (endianness defines or a configure script). */
/* TODO: support assembly optimizations. gcc is too stupid to emit bswap on
 * i486+ */
/* private stuff */
#define bswap16(x) ((((x) >> 8) & 0xff) | (((x) & 0xff) << 8))
#define bswap32(x) ((((x) & 0xff000000) >> 24) | (((x) & 0x00ff0000) >>  8) | \
                   (((x) & 0x0000ff00) <<  8) | (((x) & 0x000000ff) << 24))


#ifdef WORDS_BIGENDIAN
#define LITTLE_ENDIAN_Q 0
#else
#define LITTLE_ENDIAN_Q 1
#endif /* WORDS_BIGENDIAN */

/* public stuff
 * le32: convert between host and little endian (32 bits)
 * le16: convert between host and little endian (16 bits)
 * be32: convert between host and big endian (32 bits)
 * be16: convert between host and big endian (16 bits)
 *
 * Use pass in the dest in the first arg and src in the second.
 * Each argument is evaluated exactly once.
 * Constructs such as le32(x,x) are permitted. */
#define le32(dest, x) do { int __feh=x; \
                           if (LITTLE_ENDIAN_Q) dest = x; \
                           else dest = bswap32(__feh); } while(0)
#define le16(dest, x) do { short __feh=x; \
                           if (LITTLE_ENDIAN_Q)  dest = x; \
                           else dest = bswap16(__feh);  } while(0)
#define be32(dest, x) do { int __feh=x; \
                           if (!LITTLE_ENDIAN_Q) dest = x; \
                           else dest = bswap32(__feh); } while(0)
#define be16(dest, x) do { short __feh=x; \
                           if (!LITTLE_ENDIAN_Q) dest = x; \
                           else dest = bswap16(__feh); } while(0)


/* read() and write() wrappers for maintaing data in little-endian 
   byte-ordering; everything outside these wrappers should be stored in
   host-endian format */

inline int leread32(int fd, long *c)
{
	int r;

	r = read(fd, c, 4);
	le32(*c, *c);

	return r;
}

inline int lefread32(FILE *stream, long *c)
{
	int r;

	r = fread(c, 4, 1, stream);
	le32(*c, *c);

	return r;
}

inline int leread16(int fd, short *c)
{
	int r;

	r = read(fd, c, 2);
	le16(*c, *c);

	return r;
}

inline int lefread16(FILE *stream, short *c)
{
	int r;

	r = fread(c, 2, 1, stream);
	le16(*c, *c);

	return r;
}

inline int lewrite32(int fd, long c)
{
	int r;

	le32(c, c);
	r = write(fd, &c, 4);

	return r;
}

inline int lefwrite32(FILE *stream, long c)
{
	int r;

	le32(c, c);
	r = fwrite(&c, 4, 1, stream);

	return r;
}

inline int lewrite16(int fd, short c)
{
	int r;

	le16(c, c);
	r = write(fd, &c, 2);

	return r;
}

inline int lefwrite16(FILE *stream, short c)
{
	int r;

	le16(c, c);
	r = fwrite(&c, 2, 1, stream);

	return r;
}

/* read() and write() wrappers for maintaing data in big-endian 
   byte-ordering */

inline int beread32(int fd, long *c)
{
	int r;

	r = read(fd, c, 4);
	be32(*c, *c);

	return r;
}

inline int befread32(FILE *stream, long *c)
{
	int r;

	r = fread(c, 4, 1, stream);
	be32(*c, *c);

	return r;
}

inline int beread16(int fd, short *c)
{
	int r;

	r = read(fd, c, 2);
	be16(*c, *c);

	return r;
}

inline int befread16(FILE *stream, short *c)
{
	int r;

	r = fread(c, 2, 1, stream);
	be16(*c, *c);

	return r;
}

inline int bewrite32(int fd, long c)
{
	int r;

	be32(c, c);
	r = write(fd, &c, 4);

	return r;
}

inline int befwrite32(FILE *stream, long c)
{
	int r;

	be32(c, c);
	r = fwrite(&c, 4, 1, stream);

	return r;
}

inline int bewrite16(int fd, short c)
{
	int r;

	be16(c, c);
	r = write(fd, &c, 2);

	return r;
}

inline int befwrite16(FILE *stream, short c)
{
	int r;

	be16(c, c);
	r = fwrite(&c, 2, 1, stream);

	return r;
}

#endif /* HAVE_ENDIAN_H */
