/*
 * Copyright (C) 2004-2014 Metaphonic Labs
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

#include "config.h"

#include <cstdlib>
#include <cstring>

#include <filesystem>
#include <system_error>
#include <vector>

/* tempFile: mkstemp and close on POSIX, GetTempFileNameW on Windows. */
#if defined(_WIN32)
# include <windows.h>
#else
# include <unistd.h>
#endif

#if defined(_WIN32)
# ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
# endif
# ifndef NOMINMAX
#  define NOMINMAX
# endif
# include <windows.h>
#elif defined(__APPLE__)
# include <mach-o/dyld.h>   /* _NSGetExecutablePath */
#else
# include <unistd.h>        /* readlink */
#endif

#include "thUtil.h"

namespace fs = std::filesystem;

static int RangeArray[] = {10, 100, 1000, 10000, 100000, 1000000, 10000000,
                           100000000, 1000000000};

static int RangeSize = sizeof(RangeArray)/sizeof(int);

int thUtil::getNumLength (int num)
{
    /* abs(INT_MIN) is undefined -- there is no positive counterpart in int.
       Widen first, then clamp back into the range the table covers. */
    long long wide = num;

    if (wide < 0)
        wide = -wide;

    if (wide > RangeArray[RangeSize - 1])
        return RangeSize + 1;

    int i;

    for (i = 0; i < RangeSize; i++) {
        if (wide < RangeArray[i]) {
            return i+1;
        }
    }

    return RangeSize+1;
}

/* These were a strrchr(path, '/') pair -- one lifted from ircd-hybrid, one
 * from Lars Wirzenius -- which is correct on Unix and wrong everywhere else:
 * a Windows path separator is a backslash, and "C:file" is relative to the
 * current directory *of drive C*, which no amount of slash-hunting will tell
 * you. std::filesystem knows all of that per platform.
 *
 * Checked case by case against the old implementations. Behaviour on Unix is
 * unchanged, including the odd corners:
 *
 *     basename("foo")   -> "foo"      dirname("foo")   -> ""
 *     basename("a/b")   -> "b"        dirname("a/b")   -> "a"
 *     basename("/foo")  -> "foo"      dirname("/foo")  -> "/"
 *     basename("foo/")  -> ""         dirname("foo/")  -> "foo"
 *     basename("/")     -> ""         dirname("/")     -> "/"
 *
 * One deliberate difference: dirname("/a//b") was "/a/" and is now "/a".
 * Duplicate separators get collapsed. Both name the same directory, and the
 * only consumers are Gtk::FileChooser::set_current_folder and the patch
 * list's display column.
 */

string thUtil::basename (const char *path)
{
    if (path == NULL)
        return "";

    return std::filesystem::path(path).filename().string();
}

string thUtil::dirname (const char *path)
{
    if (path == NULL)
        return "";

    return std::filesystem::path(path).parent_path().string();
}

/* The directory the running executable is in, or "".
 *
 * This is what lets an installed tree find its own data without an absolute
 * path compiled into it, which a macOS .app and a Windows install directory
 * have no alternative to. Moved here from a static in thPluginManager.cpp so
 * that DSP lookup can use it too.
 */
string thUtil::exeDir (void)
{
#if defined(_WIN32)

    wchar_t buf[32768];   /* MAX_PATH is a lie; long paths need the big one */

    const DWORD n = GetModuleFileNameW(NULL, buf, sizeof(buf) / sizeof(buf[0]));

    if (n == 0 || n >= sizeof(buf) / sizeof(buf[0]))
        return "";

    return fs::path(buf, buf + n).parent_path().string();

#elif defined(__APPLE__)

    uint32_t size = 0;

    /* Returns -1 and sets size to what is actually needed. */
    _NSGetExecutablePath(NULL, &size);

    std::vector<char> buf(size + 1, 0);

    if (_NSGetExecutablePath(buf.data(), &size) != 0)
        return "";

    /* May be a symlink or contain ".."; canonical() resolves both, and a
       failure just means we fall back to the unresolved form. */
    std::error_code ec;

    const fs::path resolved = fs::canonical(fs::path(buf.data()), ec);

    return ec ? fs::path(buf.data()).parent_path().string()
              : resolved.parent_path().string();

#else

    /* readlink does not terminate, does not report the length it wanted, and
       silently truncates -- so a fixed buffer can hand back a path that looks
       plausible and names the wrong directory. Grow until it fits. */
    std::vector<char> buf(1024);

    for (;;)
    {
        const ssize_t n = readlink("/proc/self/exe", buf.data(), buf.size());

        if (n <= 0)
            return "";

        if ((size_t)n < buf.size())
        {
            buf[n] = 0;
            return fs::path(buf.data()).parent_path().string();
        }

        if (buf.size() > (1u << 20))
            return "";   /* absurd; something is wrong */

        buf.resize(buf.size() * 2);
    }

#endif
}

/* Find a data file named by a bare filename.
 *
 * A .patch says `dsp ts1.dsp' and nothing about where ts1.dsp lives. The old
 * lookup tried exactly two places -- the name as given, and DSP_PATH -- which
 * meant a .patch only resolved if you happened to be standing in the right
 * directory or had run `make install'. On this machine it silently worked for
 * months because a years-old /usr/local/share/thinksynth/dsp was still there,
 * which is precisely the kind of false pass that makes CI look wrong when it
 * is right.
 */
/* Absolute, and tidied where that is possible.
 *
 * weakly_canonical also folds away "." and ".." and follows symlinks, which
 * makes the path the user is shown in an error message the path the file
 * actually has. It can fail on a filesystem that refuses to be walked, so
 * absolute() -- which only prepends the working directory -- is the fallback,
 * and the raw candidate the fallback's fallback. */
static string absolutePath (const fs::path &p)
{
    std::error_code ec;

    const fs::path canon = fs::weakly_canonical(p, ec);

    if (!ec && !canon.empty())
        return canon.string();

    const fs::path abs = fs::absolute(p, ec);

    return ec ? p.string() : abs.string();
}

string thUtil::findDataFile (const string &name, const string &subdir,
                             const char *envVar, const string &fallback)
{
    if (name.empty())
        return "";

    std::error_code ec;

    /* An absolute path means what it says, found or not: reporting "cannot
       open /what/you/asked/for" is more use than silently substituting. */
    if (fs::path(name).is_absolute())
        return absolutePath(name);

    std::vector<fs::path> tries;

    if (envVar != NULL)
    {
        const char *env = getenv(envVar);

        if (env != NULL && *env != 0)
        {
            tries.push_back(fs::path(env) / name);
            tries.push_back(fs::path(env) / subdir / name);
        }
    }

    /* Relative to the current directory, as given and under the conventional
       subdirectory -- the latter is what makes a source or build tree work
       without anything being installed. */
    tries.push_back(name);
    tries.push_back(fs::path(subdir) / name);

    const string exe = exeDir();

    if (!exe.empty())
    {
        const fs::path bin(exe);

        /* Unix install: <prefix>/bin/thinksynth,
           <prefix>/share/thinksynth/dsp/. */
        tries.push_back(bin.parent_path() / "share" / PACKAGE_NAME / subdir / name);

        /* macOS bundle: Contents/MacOS/thinksynth, Contents/Resources/dsp/. */
        tries.push_back(bin.parent_path() / "Resources" / subdir / name);

        /* Windows install, and a build tree run from its own directory. */
        tries.push_back(bin / subdir / name);
        tries.push_back(bin.parent_path() / subdir / name);
    }

    if (!fallback.empty())
        tries.push_back(fs::path(fallback) / name);

    for (size_t i = 0; i < tries.size(); i++)
        if (fs::exists(tries[i], ec))
            return absolutePath(tries[i]);

    return "";
}

/* The directories the above searches, in the same order and for the same
   reasons. Kept beside it deliberately: two search orders that are supposed
   to agree and are written out separately do not stay agreeing. */
/* An empty file with an unguessable name, in the right directory for this
 * platform.
 *
 * The directory is the reason this exists. The node editor used to build the
 * template itself:
 *
 *     const char *tmp = getenv("TMPDIR");
 *     string tpl = string(tmp && *tmp ? tmp : "/tmp");
 *
 * TMPDIR is a POSIX convention. Windows sets TEMP and TMP and never TMPDIR,
 * so that fell through to "/tmp" -- a directory Windows does not have.
 * mkstemp failed, the working copy was never made, and the editor reported
 * "Could not read <file>" about a file it had not tried to read.
 * temp_directory_path knows the answer on all three platforms.
 */
string thUtil::tempFile (const string &prefix)
{
    std::error_code ec;

    const fs::path dir = fs::temp_directory_path(ec);

    if (ec || dir.empty())
        return "";

#if defined(_WIN32)

    /* GetTempFileName is the Win32 equivalent: it invents a name in the
       directory given, creates the file, and hands back the path -- and the
       per-user temp directory is already private, which is what mkstemp's
       0600 buys on a shared /tmp. */
    const std::wstring wdir = dir.wstring();
    const std::wstring wpre(prefix.begin(), prefix.end());

    wchar_t out[MAX_PATH + 1];

    /* Only the first three characters of the prefix are used, which is a
       documented limit of the API rather than a choice. */
    if (GetTempFileNameW(wdir.c_str(), wpre.c_str(), 0, out) == 0)
        return "";

    return fs::path(out).string();

#else

    /* mkstemp rather than a name built from the pid: it creates the file
       itself, O_EXCL and 0600, so on a shared temp directory without the
       sticky bit there is nothing to guess and nothing to pre-empt.

       The template has to end in the X's, so nothing may be appended. */
    const fs::path tpl = dir / (prefix + "XXXXXX");

    const string t = tpl.string();

    std::vector<char> buf(t.begin(), t.end());

    buf.push_back('\0');

    const int fd = mkstemp(&buf[0]);

    if (fd < 0)
        return "";

    /* Closed rather than kept: everything downstream works by path, and the
       writer deliberately writes through a temporary and renames, which
       replaces the inode under any descriptor we held anyway. */
    close(fd);

    return string(&buf[0]);

#endif
}

string thUtil::findDataDir (const string &subdir, const char *envVar,
                            const string &fallback)
{
    std::error_code ec;

    std::vector<fs::path> tries;

    if (envVar != NULL)
    {
        const char *env = getenv(envVar);

        if (env != NULL && *env != 0)
        {
            tries.push_back(env);
            tries.push_back(fs::path(env) / subdir);
        }
    }

    tries.push_back(subdir);

    const string exe = exeDir();

    if (!exe.empty())
    {
        const fs::path bin(exe);

        tries.push_back(bin.parent_path() / "share" / PACKAGE_NAME / subdir);
        tries.push_back(bin.parent_path() / "Resources" / subdir);
        tries.push_back(bin / subdir);
        tries.push_back(bin.parent_path() / subdir);
    }

    if (!fallback.empty())
        tries.push_back(fallback);

    for (size_t i = 0; i < tries.size(); i++)
        if (fs::is_directory(tries[i], ec))
            return absolutePath(tries[i]);

    return "";
}
