#ifndef HAVE_ENDIAN_H
#define HAVE_ENDIAN_H

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


extern int leread32(int fd, long *c);
extern int leread16(int fd, short *c);
extern int lewrite32(int fd, long c);
extern int lewrite16(int fd, short c);

extern int beread32(int fd, long *c);
extern int beread16(int fd, short *c);
extern int bewrite32(int fd, long c);
extern int bewrite16(int fd, short c);

#endif /* HAVE_ENDIAN_H */
