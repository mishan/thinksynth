/* $Id: thEndian.h,v 1.6 2004/08/16 09:34:48 misha Exp $ */
/*
 * Copyright (C) 2004 Metaphonic Labs
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef TH_ENDIAN_H
#define TH_ENDIAN_H

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

inline int leread32(int fd, int *c)
{
	int r;

	r = read(fd, c, 4);
	le32(*c, *c);

	return r;
}

inline int lefread32(FILE *stream, int *c)
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

inline int lewrite32(int fd, int c)
{
	int r;

	le32(c, c);
	r = write(fd, &c, 4);

	return r;
}

inline int lefwrite32(FILE *stream, int c)
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

inline int beread32(int fd, int *c)
{
	int r;

	r = read(fd, c, 4);
	be32(*c, *c);

	return r;
}

inline int befread32(FILE *stream, int *c)
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

inline int bewrite32(int fd, int c)
{
	int r;

	be32(c, c);
	r = write(fd, &c, 4);

	return r;
}

inline int befwrite32(FILE *stream, int c)
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

#endif /* TH_ENDIAN_H */
