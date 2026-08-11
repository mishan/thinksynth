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

/* Finding the GTK data that cmake/GtkRuntime.cmake put in the package.
 *
 * On Linux none of this runs: GTK is installed system-wide, the distribution
 * put its schemas and icons where GTK already looks, and the package ships
 * none of it. On macOS and Windows there is no system GTK at all, so the
 * package carries its own copy and has to say where it is -- and it cannot
 * say so at build time, because a .app is dragged wherever the user likes and
 * a .zip is unpacked wherever they unpack it.
 *
 * So: locate the bundle relative to the running executable, then set the
 * three environment variables GTK reads. Nothing here is macOS- or
 * Windows-specific; it keys off whether the files are present, which is what
 * lets the whole path be exercised on Linux with -DTHINK_BUNDLE_GTK=ON.
 */

#include "config.h"

#include <glib.h>
#include <gtk/gtk.h>

#include <stdio.h>
#include <string.h>

#include <filesystem>
#include <string>
#include <vector>

#include "gthGtkRuntime.h"
#include "thUtil.h"

namespace fs = std::filesystem;

static std::string bundleRootFound;

/* Prepend to a GLib-style search path, keeping whatever was there.
 *
 * XDG_DATA_DIRS in particular is not ours to overwrite: on Linux it is how
 * the session tells every application where the system themes are, and
 * replacing it rather than extending it would take the desktop's icons away
 * from an application that only wanted to add its own.
 */
static void prependSearchPath (const char *var, const std::string &dir)
{
    const char *existing = g_getenv(var);

    if (existing == NULL || *existing == 0)
    {
        g_setenv(var, dir.c_str(), TRUE);
        return;
    }

    /* Already first: leave it alone rather than growing the variable on
       every exec of a child process that inherits it. */
    const std::string prefix = dir + G_SEARCHPATH_SEPARATOR_S;

    if (std::string(existing).compare(0, prefix.size(), prefix) == 0)
        return;

    g_setenv(var, (prefix + existing).c_str(), TRUE);
}

/* The bundle root, or "".
 *
 * The candidates are the ones thUtil::findDataFile already searches for DSPs
 * and patches, in the same order, because the packaging puts GTK's data under
 * the same root it puts ours (cmake/Layout.cmake, THINK_PKG_GTK_DIR).
 *
 * A directory counts as the root only if it actually holds bundled GTK data.
 * Testing for the payload rather than for the shape of the path is what keeps
 * a build tree, or a Linux install that deliberately ships none of this, from
 * matching and then pointing GTK at an empty directory -- which would be
 * worse than doing nothing, since GSETTINGS_SCHEMA_DIR naming a directory
 * with no schemas in it is not an error, just an absence.
 */
static std::string findBundleRoot (void)
{
    const std::string exe = thUtil::exeDir();

    if (exe.empty())
        return "";

    const fs::path bin(exe);

    std::vector<fs::path> tries;

    tries.push_back(bin.parent_path() / "Resources");            /* .app     */
    tries.push_back(bin);                                        /* Windows  */
    tries.push_back(bin.parent_path() / "share" / PACKAGE_NAME); /* prefix   */

    std::error_code ec;

    for (size_t i = 0; i < tries.size(); i++)
    {
        const fs::path schemas =
            tries[i] / "share" / "glib-2.0" / "schemas" / "gschemas.compiled";

        if (fs::exists(schemas, ec))
            return tries[i].string();
    }

    return "";
}

/* gdk-pixbuf's loader cache names each module by path, and those paths have
 * to be absolute unless gdk-pixbuf was built -Drelocatable=true -- Homebrew
 * and the usual Linux distribution build are not, so build_module_path()
 * hands the path to g_module_open() unchanged.
 *
 * An absolute path cannot be baked in at build time for something that moves,
 * and both a .app and an unpacked .zip move. So cmake/GtkRuntime.cmake writes
 * the cache with a placeholder where the bundle root goes -- on every
 * platform, rather than only where relocation is unavailable, because one
 * code path that always runs is worth more than two of which one is never
 * tested -- and this fills it in, once, next to the user's other caches.
 */
static void configurePixbufLoaders (const fs::path &root)
{
    if (g_getenv("GDK_PIXBUF_MODULE_FILE") != NULL)
        return;   /* an explicit setting is an override, not a suggestion */

    std::error_code ec;

    const fs::path moduledir = root / "lib" / "gdk-pixbuf-2.0";

    if (!fs::is_directory(moduledir, ec))
        return;

    /* The binary version ("2.10.0") is the only thing in this path that is
       not ours to predict, so take whatever directory is there rather than
       hardcoding a number that changes on gdk-pixbuf's schedule.

       There is one form: cmake/GtkRuntime.cmake rewrites every module path
       in the cache to the placeholder below, on every platform, so the same
       substitution happens everywhere. */
    fs::path templatePath;

    for (fs::directory_iterator it(moduledir, ec), end; it != end; it.increment(ec))
    {
        if (fs::exists(it->path() / "loaders.cache.in", ec))
        {
            templatePath = it->path() / "loaders.cache.in";
            break;
        }
    }

    if (templatePath.empty())
        return;

    gchar *text = NULL;
    gsize  len  = 0;

    if (!g_file_get_contents(templatePath.string().c_str(), &text, &len, NULL))
    {
        g_warning("thinksynth: cannot read %s; bundled image loaders "
                  "will not be used", templatePath.string().c_str());
        return;
    }

    /* The cache format C-escapes its quoted strings, which is why
       gdk-pixbuf-query-loaders writes "lib\\gdk-pixbuf-2.0\\..." on
       Windows. Substituting a raw Windows root put single backslashes in, and
       the parser duly read D:\a\_temp\msys64\tmp\relocated back as
       D:<BEL>_tempmsys64<TAB>mp<CR>elocated -- which, no longer being
       absolute, gdk-pixbuf then helpfully prefixed with the toplevel.
       Harmless everywhere without backslashes in its paths. */
    std::string escaped;

    for (std::string::size_type i = 0; i < bundleRootFound.size(); i++)
    {
        if (bundleRootFound[i] == '\\')
            escaped += '\\';

        escaped += bundleRootFound[i];
    }

    gchar **parts  = g_strsplit(text, "@THINK_BUNDLE_ROOT@", -1);
    gchar  *filled = g_strjoinv(escaped.c_str(), parts);

    g_strfreev(parts);
    g_free(text);

    const fs::path cacheDir = fs::path(g_get_user_cache_dir()) / PACKAGE_NAME;
    const fs::path cacheOut = cacheDir / "loaders.cache";

    /* Rewriting on every start would be harmless but pointless, and it would
       churn a file that a concurrently starting second instance is reading. */
    gchar *previous = NULL;

    const bool current =
        g_file_get_contents(cacheOut.string().c_str(), &previous, NULL, NULL)
        && g_strcmp0(previous, filled) == 0;

    g_free(previous);

    if (!current)
    {
        if (g_mkdir_with_parents(cacheDir.string().c_str(), 0700) != 0
            || !g_file_set_contents(cacheOut.string().c_str(), filled, -1, NULL))
        {
            g_warning("thinksynth: cannot write %s; bundled image loaders "
                      "will not be used", cacheOut.string().c_str());
            g_free(filled);
            return;
        }
    }

    g_free(filled);

    g_setenv("GDK_PIXBUF_MODULE_FILE", cacheOut.string().c_str(), TRUE);
}

void gthGtkRuntime::configure (void)
{
    const std::string root = findBundleRoot();

    if (root.empty())
        return;   /* system GTK, or a build tree. Both are fine. */

    bundleRootFound = root;

    const fs::path base(root);

    std::error_code ec;

    /* GSETTINGS_SCHEMA_DIR names the directory of compiled schemas directly,
       unlike XDG_DATA_DIRS, to which GLib appends glib-2.0/schemas itself.
       Both would work here; this one is the more direct statement, and it is
       what findBundleRoot tested for. */
    prependSearchPath("GSETTINGS_SCHEMA_DIR",
                      (base / "share" / "glib-2.0" / "schemas").string());

    /* Icon themes. GTK looks under <dir>/icons for each XDG data dir. */
    if (fs::is_directory(base / "share" / "icons", ec))
        prependSearchPath("XDG_DATA_DIRS", (base / "share").string());

    configurePixbufLoaders(base);
}

std::string gthGtkRuntime::bundleRoot (void)
{
    return bundleRootFound;
}

/* Each of the three things a package can be missing, asked as a question with
 * a yes/no answer, in the order they would bite:
 *
 *   schema   -- g_settings_new() aborts the process, so this one is a crash
 *               on startup rather than a cosmetic problem.
 *   icon     -- image-missing is the fallback for every failed icon lookup,
 *               and a failed lookup of image-missing itself is fatal too.
 *   pixbuf   -- PNG is what the icons are. Without a loader for it the theme
 *               is present and unreadable, which looks like the icons are
 *               missing when they are not.
 */
int gthGtkRuntime::selfTest (void)
{
    const std::string root = bundleRoot();

    printf("bundled GTK data: %s\n", root.empty() ? "none (system GTK)"
                                                  : root.c_str());

    for (int i = 0; i < 3; i++)
    {
        static const char *vars[] = { "GSETTINGS_SCHEMA_DIR", "XDG_DATA_DIRS",
                                      "GDK_PIXBUF_MODULE_FILE" };
        const char *v = g_getenv(vars[i]);

        printf("  %-22s %s\n", vars[i], (v && *v) ? v : "(unset)");
    }

    int failures = 0;

    GSettingsSchemaSource *source = g_settings_schema_source_get_default();
    GSettingsSchema *schema =
        source ? g_settings_schema_source_lookup(source,
                                                 "org.gtk.Settings.FileChooser",
                                                 TRUE)
               : NULL;

    printf("  %-22s %s\n", "settings schema",
           schema ? "org.gtk.Settings.FileChooser" : "MISSING");

    if (schema)
        g_settings_schema_unref(schema);
    else
        failures++;

    /* Deliberately NOT gtk_icon_theme_has_icon(). GTK3 compiles a fallback
       icon set into its own gresource, so image-missing, folder,
       document-open, list-add, go-up and edit-find are all "found" with the
       system themes hidden and no bundle at all -- a check that cannot fail
       is worse than no check, because it reads like coverage.

       What can fail, and what is actually ours, is whether the icons we
       shipped are on the search path GTK assembled from XDG_DATA_DIRS. */
    std::error_code iec;

    const std::string want = root.empty() ? "" : root + "/share/icons";

    if (!want.empty() && fs::is_directory(want, iec))
    {
        gchar **paths = NULL;
        gint    n     = 0;
        bool    found = false;

        gtk_icon_theme_get_search_path(gtk_icon_theme_get_default(), &paths, &n);

        /* Compared as paths rather than as strings. On Windows the two spell
           the same directory differently -- our root arrives from
           GetModuleFileNameW with backslashes and picks up a forward slash
           from the "/share/icons" appended here -- and a string compare
           called a correctly configured bundle broken. */
        for (gint i = 0; i < n && !found; i++)
        {
            std::error_code pec;
            found = fs::exists(paths[i], pec)
                    && fs::equivalent(paths[i], want, pec);
        }

        printf("  %-22s %s (%d entries)\n", "icon search path",
               found ? "bundled icons present" : "BUNDLED ICONS NOT ON PATH", n);

        g_strfreev(paths);

        if (!found)
            failures++;
    }
    else
    {
        /* No icon theme in the package is a configure-time warning, not a
           failure here: GTK's compiled-in icons carry it, and a GTK
           installation without Adwaita is a real thing to build against. */
        printf("  %-22s %s\n", "icon search path",
               root.empty() ? "n/a, no bundle" : "no icons bundled");
    }

    /* If the package ships SVG icons it needs a loader that can read them,
       and Adwaita has been mostly scalable/ and symbolic/ for years. Find one
       to try loading below.
     *
     * Every .svg is checked for readability rather than just the first,
     * because the first is whichever the directory happens to yield: on Linux
     * that was a real file and on macOS a symlink into a Homebrew keg that
     * the packaging had copied as a symlink, so the same bug passed on one
     * platform and failed on the other. Counting them makes it the same
     * answer everywhere.
     */
    fs::path anSvg;
    int      svgFiles = 0, svgBroken = 0;

    if (!want.empty() && fs::is_directory(want, iec))
    {
        for (fs::recursive_directory_iterator it(want, iec), end;
             it != end; it.increment(iec))
        {
            if (it->path().extension() != ".svg")
                continue;

            svgFiles++;

            /* exists() follows the link, so a dangling one answers false. */
            std::error_code sec;

            if (!fs::exists(it->path(), sec))
                svgBroken++;
            else if (anSvg.empty())
                anSvg = it->path();
        }
    }

    if (svgFiles > 0)
    {
        printf("  %-22s %d file(s), %d unreadable\n", "bundled svg icons",
               svgFiles, svgBroken);

        if (svgBroken > 0)
            failures++;
    }

    /* gdk_pixbuf_get_formats() reports built-in and module-provided formats
       alike, which is what makes the same check right on a gdk-pixbuf that
       compiles its loaders in rather than shipping modules. */
    bool png = false, svg = false;
    int  formats = 0;

    GSList *list = gdk_pixbuf_get_formats();

    for (GSList *l = list; l != NULL; l = l->next, formats++)
    {
        gchar *name = gdk_pixbuf_format_get_name((GdkPixbufFormat *)l->data);

        if (g_strcmp0(name, "png") == 0)
            png = true;
        else if (g_strcmp0(name, "svg") == 0)
            svg = true;

        g_free(name);
    }

    g_slist_free(list);

    /* png is the fatal one and is usually compiled in, so its absence means
       something is badly wrong. */
    printf("  %-22s %d format(s), png %s, svg %s\n", "pixbuf loaders", formats,
           png ? "present" : "MISSING", svg ? "declared" : "not declared");

    if (!png)
        failures++;

    /* And then actually load one, rather than believing the list.
     *
     * gdk_pixbuf_get_formats() reports what the cache *declares*; the module
     * is not dlopened until something asks it to decode. So the list says svg
     * is available whether or not the .so beside it exists -- which I found
     * out by deleting the loader from a bundle and watching this check pass.
     * Decoding a real file from the theme we shipped is the only form of this
     * that has teeth. */
    if (!anSvg.empty())
    {
        GError    *err = NULL;
        GdkPixbuf *pb  =
            gdk_pixbuf_new_from_file_at_size(anSvg.string().c_str(), 16, 16, &err);

        /* Some Linux distributions build gdk-pixbuf to delegate decoding to
           glycin, which is configured from XDG_DATA_DIRS and lives outside
           any application bundle -- so on such a host this cannot succeed
           from a bundle, and the failure says nothing about the package.
           Neither Homebrew nor MSYS2 builds gdk-pixbuf that way, so the
           check keeps its teeth exactly where it is needed. Matched on the
           message because that is where gdk-pixbuf names it. */
        const bool glycin =
            !pb && err && strstr(err->message, "glycin") != NULL;

        printf("  %-22s %s\n", "svg icons load",
               pb      ? "yes"
               : glycin ? "skipped, this gdk-pixbuf decodes via glycin"
                        : (err ? err->message : "NO"));

        if (pb)
            g_object_unref(pb);
        else if (!glycin)
        {
            failures++;

            /* The message above says which module could not be opened and
               not what is actually there, which on macOS left "no such file"
               for a loader the build had been told to install with no way to
               tell whether it was misnamed, misplaced or never copied. */
            const fs::path loaders =
                fs::path(root) / "lib" / "gdk-pixbuf-2.0";

            std::error_code lec;

            for (fs::recursive_directory_iterator it(loaders, lec), end;
                 it != end; it.increment(lec))
                if (!fs::is_directory(it->path(), lec))
                    printf("      bundled: %s\n",
                           fs::relative(it->path(), loaders, lec).string().c_str());

            if (lec)
                printf("      bundled: nothing -- %s does not exist\n",
                       loaders.string().c_str());
        }

        g_clear_error(&err);
    }
    else
    {
        printf("  %-22s %s\n", "svg icons load",
               root.empty() ? "n/a, no bundle" : "n/a, no svg icons bundled");
    }

    printf("%s\n", failures ? "gtk runtime check FAILED" : "gtk runtime ok");

    return failures ? 1 : 0;
}
