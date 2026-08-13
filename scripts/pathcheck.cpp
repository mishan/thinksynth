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

/*
 * pathcheck -- does data-file lookup give an answer that is still true later?
 *
 * thUtil::findDataFile searches several places for a bare name, and one of
 * them is the current directory. It used to hand back whichever candidate
 * matched, so a hit there returned something like "dsp/amb01.dsp" -- an
 * answer that is only correct while the working directory stays put.
 *
 * That is not hypothetical, and it is not a Windows quirk either; Windows is
 * only where it was noticed. thinksynth loads a patch's DSP at once and the
 * node editor opens the same file later, from the string the patch recorded.
 * The first worked, the second reported "cannot find dsp/amb01.dsp", and the
 * file was there the whole time.
 *
 * So: every answer must be absolute, and must still open after the working
 * directory has moved. The exe-relative layouts -- a Windows zip with the
 * data beside the binary, a macOS .app with it in Contents/Resources -- are
 * not exercised here, because this harness cannot move itself; they are
 * checked by running the real binary out of each shape.
 */

#include "config.h"
#include "thUtil.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

static int failures = 0;

static void check (bool ok, const char *what, const std::string &detail = "")
{
    printf("%-46s %s%s%s\n", what, ok ? "ok" : "FAILED",
           detail.empty() ? "" : "  ", detail.c_str());

    if (!ok)
        failures++;
}

int main (void)
{
    std::error_code ec;

    /* A tree shaped like an unpacked package, somewhere neither the build nor
       the compiled-in paths know about.
     *
     * The temporary directory is checked before anything is built under it,
     * and its own error_code is used rather than the shared one, because the
     * failure is quiet and the consequence is not: temp_directory_path returns
     * an empty path when it cannot work out where /tmp is -- a TMPDIR pointing
     * at a directory that does not exist is enough -- and an empty path makes
     * "thinksynth-pathcheck" relative. The test then builds its fixture in
     * whatever directory it was run from and, at the end, remove_all's it.
     * Checked by pointing TMPDIR at nothing: it duly created and deleted a
     * tree in the working directory. */
    std::error_code tec;

    const fs::path tmp = fs::temp_directory_path(tec);

    if (tec || tmp.empty())
    {
        fprintf(stderr, "pathcheck: no usable temporary directory (%s).\n"
                        "  TMPDIR names one that does not exist, most likely.\n",
                tec ? tec.message().c_str() : "it came back empty");
        return 2;
    }

    const fs::path root = tmp / "thinksynth-pathcheck";

    /* Belt and braces: everything below deletes this tree, so refusing to
       proceed on anything but an absolute path costs nothing and rules out
       the whole class. */
    if (!root.is_absolute())
    {
        fprintf(stderr, "pathcheck: %s is not absolute; refusing to build and "
                        "delete a fixture there\n", root.string().c_str());
        return 2;
    }

    fs::remove_all(root, ec);
    fs::create_directories(root / "dsp", ec);
    fs::create_directories(root / "patches", ec);

    if (ec)
    {
        fprintf(stderr, "pathcheck: cannot create %s\n", root.string().c_str());
        return 2;
    }

    {
        FILE *f = fopen((root / "dsp" / "pathcheck.dsp").string().c_str(), "w");

        if (f == NULL)
        {
            fprintf(stderr, "pathcheck: cannot write the test DSP\n");
            return 2;
        }

        fputs("/* not parsed; only ever opened */\n", f);
        fclose(f);
    }

    const fs::path was = fs::current_path(ec);

    fs::current_path(root, ec);

    /* The candidate that matches here is the current-directory one, which is
       exactly the case that used to return something relative. */
    const std::string found =
        thUtil::findDataFile("pathcheck.dsp", "dsp", NULL, "");

    check(!found.empty(), "found via the working directory", found);
    check(fs::path(found).is_absolute(), "the answer is absolute", found);

    const std::string dir = thUtil::findDataDir("patches", NULL, "");

    check(!dir.empty(), "findDataDir found the patches directory", dir);
    check(fs::path(dir).is_absolute(), "that answer is absolute too", dir);

    /* An absolute name is handed back absolute, and tidied. */
    const std::string dotted =
        thUtil::findDataFile((root / "dsp" / "." / "pathcheck.dsp").string(),
                             "dsp", NULL, "");

    check(fs::path(dotted).is_absolute() &&
          dotted.find("/./") == std::string::npos &&
          dotted.find("\\.\\") == std::string::npos,
          "an absolute name comes back tidied", dotted);

    /* The part that matters: the synth resolves, something moves the working
       directory, and the editor opens what the synth resolved. */
    fs::current_path(was, ec);

    check(fs::exists(found, ec), "still opens after the directory moves",
          found);
    check(fs::is_directory(dir, ec), "the directory does too", dir);

    fs::remove_all(root, ec);

    /* thUtil::tempFile.
     *
     * The property that matters is the one Windows did not have: the file
     * goes in the directory *this platform* uses for temporary files, rather
     * than one spelled out in the source. The node editor built its own
     * template from TMPDIR with "/tmp" as the fallback, and since Windows
     * sets TEMP and TMP but never TMPDIR, every attempt landed on a path
     * Windows does not have -- so the working copy was never made and the
     * editor said it could not read a file it had not opened.
     *
     * Checked here by pointing the temporary directory somewhere specific and
     * requiring the file to appear there, which is what "wherever this
     * platform keeps them" means on a machine that can only run one of the
     * three. */
    printf("\n");

    const fs::path tdir = root.parent_path() / "thinksynth-pathcheck-tmp";

    fs::remove_all(tdir, ec);
    fs::create_directories(tdir, ec);

#ifdef _WIN32
    _putenv_s("TMP", tdir.string().c_str());
    _putenv_s("TEMP", tdir.string().c_str());
#else
    setenv("TMPDIR", tdir.string().c_str(), 1);
#endif

    const std::string t1 = thUtil::tempFile("pathcheck-");
    const std::string t2 = thUtil::tempFile("pathcheck-");

    check(!t1.empty(), "tempFile made a file", t1);
    check(fs::path(t1).is_absolute(), "its path is absolute", t1);
    check(fs::exists(t1, ec), "and the file is really there", t1);

    /* Two calls must not collide: the editor makes one per open. */
    check(!t2.empty() && t1 != t2, "a second call gets a different name", t2);

    /* The point of the exercise. */
    check(fs::path(t1).parent_path() == tdir,
          "it landed in this platform's temp dir", fs::path(t1).parent_path().string());

    fs::remove(t1, ec);
    fs::remove(t2, ec);

    /* And the case that actually distinguishes the fix from the bug.
     *
     * The old code read TMPDIR and fell back to a literal "/tmp". On Linux
     * that honours TMPDIR, so everything above would have passed against it
     * and caught nothing. Windows is different only in that it sets TEMP and
     * TMP and never TMPDIR -- so here TMPDIR is removed and TMP left set,
     * which is the shape Windows has. The old code goes to /tmp; anything
     * asking the platform goes to TMP. */
#ifndef _WIN32
    unsetenv("TMPDIR");
    setenv("TMP", tdir.string().c_str(), 1);

    /* Which variables temp_directory_path consults is implementation-defined.
       libstdc++ reads TMPDIR, TMP, TEMP and TEMPDIR; another library need not,
       and asserting on it regardless would make this harness fail on a
       platform where nothing is wrong. So the precondition is checked rather
       than assumed: if the library does not honour TMP here, there is nothing
       this case can prove and it says so instead of failing. */
    std::error_code pec;

    if (fs::temp_directory_path(pec) == tdir && !pec)
    {
        const std::string t3 = thUtil::tempFile("pathcheck-");

        check(!t3.empty() && fs::path(t3).parent_path() == tdir,
              "TMP is honoured when TMPDIR is unset",
              t3.empty() ? "(none)" : fs::path(t3).parent_path().string());

        fs::remove(t3, ec);
    }
    else
    {
        printf("%-46s %s\n", "TMP when TMPDIR is unset",
               "skipped; this library does not consult TMP");
    }
#endif

    fs::remove_all(tdir, ec);

    printf("\n%s\n", failures ? "pathcheck FAILED" : "pathcheck ok");

    return failures ? 1 : 0;
}
