/* picolibc has no readlink, and libthink's thUtil::exeDir asks it where the
   program is. There is no file system to ask: the failure it already
   answers with an empty path. */
#include <errno.h>
#include <sys/types.h>

ssize_t
readlink (const char *path, char *buf, size_t size)
{
    (void)path;
    (void)buf;
    (void)size;
    errno = ENOSYS;
    return -1;
}
