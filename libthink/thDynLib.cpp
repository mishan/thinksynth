/*
 * Copyright (C) 2004-2026 Metaphonic Labs
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

#include "thDynLib.h"

#if defined(_WIN32)

# ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
# endif
# ifndef NOMINMAX
#  define NOMINMAX
# endif
# include <windows.h>

# include <filesystem>

namespace {

/* FormatMessage for the last error, trimmed of its trailing newline. */
std::string win32Error (DWORD code)
{
    if (code == 0)
        return "";

    LPWSTR buf = NULL;

    const DWORD n = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, code, 0, (LPWSTR)&buf, 0, NULL);

    if (n == 0 || buf == NULL)
        return "unknown error";

    std::wstring w(buf, n);

    LocalFree(buf);

    while (!w.empty() && (w[w.size() - 1] == L'\n' || w[w.size() - 1] == L'\r'))
        w.erase(w.size() - 1);

    /* Narrow it the same way paths are widened, via std::filesystem, rather
       than assuming the active code page can represent it. */
    return std::filesystem::path(w).string();
}

DWORD lastCode = 0;

} /* namespace */

thDynLib::Handle thDynLib::open (const std::string &path)
{
    /* u8path-equivalent: go through fs::path so a plugin under a directory
       with non-ASCII characters loads. LoadLibraryA would go through the
       active code page and fail. */
    const std::wstring wide = std::filesystem::path(path).wstring();

    /* ALTERED_SEARCH_PATH so a plugin's own directory is searched for
       anything it depends on -- the closest thing to the Unix behaviour of
       resolving against what is already loaded. */
    HMODULE h = LoadLibraryExW(wide.c_str(), NULL,
                               LOAD_WITH_ALTERED_SEARCH_PATH);

    lastCode = (h == NULL) ? GetLastError() : 0;

    return (Handle)h;
}

void *thDynLib::symbol (Handle handle, const char *name)
{
    if (handle == NULL || name == NULL)
        return NULL;

    FARPROC p = GetProcAddress((HMODULE)handle, name);

    lastCode = (p == NULL) ? GetLastError() : 0;

    /* The documented way to get from FARPROC to a data or object pointer
       without the compiler complaining about the cast. */
    return (void *)(void (*)(void))p;
}

void thDynLib::close (Handle handle)
{
    if (handle != NULL)
        FreeLibrary((HMODULE)handle);
}

std::string thDynLib::lastError (void)
{
    return win32Error(lastCode);
}

#else /* POSIX */

# ifdef HAVE_DLFCN_H
#  include <dlfcn.h>
# else
#  error Need a dl implementation!
# endif

/* dlerror() both reports and clears. Clearing it first is what makes
   lastError() describe the call just made rather than some earlier failure
   that nobody read -- which is the contract the header states. */
thDynLib::Handle thDynLib::open (const std::string &path)
{
    dlerror();

    return dlopen(path.c_str(), RTLD_NOW);
}

void *thDynLib::symbol (Handle handle, const char *name)
{
    if (handle == NULL || name == NULL)
        return NULL;

    dlerror();

    return dlsym(handle, name);
}

void thDynLib::close (Handle handle)
{
    if (handle != NULL)
        dlclose(handle);
}

std::string thDynLib::lastError (void)
{
# ifdef HAVE_DLERROR
    const char *e = dlerror();

    return (e == NULL) ? "" : e;
# else
    return "";
# endif
}

#endif
