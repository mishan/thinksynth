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

#ifndef GTH_THEME_H
#define GTH_THEME_H

#include <sigc++/sigc++.h>

#include <string>

/* Light or dark, and following the desktop when asked to.
 *
 * GTK will not do this by itself. Its Win32 backend answers twenty-one
 * settings -- cursor blink, drag threshold, font name, xft hinting -- and not
 * one of them is the colour scheme; there is no reference to
 * AppsUseLightTheme anywhere in it. So an application that wants to match the
 * desktop has to go and ask the desktop, on each platform, in that platform's
 * own way.
 */

/* What the desktop says it wants. NoPreference is a real answer and not an
 * error: it is what the freedesktop portal returns when the user has not
 * expressed one, and it means "use your own default" rather than "light".
 */
enum class gthColorScheme
{
    NoPreference,
    Light,
    Dark
};

/* What the user asked us for, which is not the same question. */
enum class gthThemeChoice
{
    Auto,
    Light,
    Dark
};

namespace gthTheme
{
    /* The decision, on its own, with no platform and no toolkit in it.
     *
     * Separated out because it is the only part that is the same everywhere,
     * and therefore the only part that can be tested everywhere --
     * scripts/themecheck does exactly that. The platform readers below can
     * only be exercised on the platform they read.
     */
    bool wantsDark (gthThemeChoice choice, gthColorScheme system);

    /* The desktop's current preference. NoPreference where there is no way to
     * ask, which is a normal outcome rather than a failure. */
    gthColorScheme systemScheme (void);

    /* Emitted on the GUI thread when the desktop changes its mind. Connect
     * before calling startWatching. */
    sigc::signal<void ()> &signalSystemSchemeChanged (void);

    /* Begin watching for changes. Idempotent, and a no-op where the platform
     * offers no notification -- in which case the signal simply never fires
     * and the setting is whatever it was at startup. */
    void startWatching (void);

    /* Apply a choice to the running application, and remember it.
     *
     * `choice' is the user's; the system scheme is consulted only when it is
     * Auto. Safe to call repeatedly, which is what the change signal does.
     */
    void apply (gthThemeChoice choice);

    /* The choice currently in force. */
    gthThemeChoice current (void);

    /* The name of a theme with any dark suffix taken off: "Adwaita-dark"
     * becomes "Adwaita", "Yaru-dark" becomes "Yaru".
     *
     * Needed because gtk-application-prefer-dark-theme selects the dark
     * *variant* of a theme that has one, and cannot do anything at all about
     * a theme that is dark by name -- which is what a desktop set to dark
     * usually hands us. Setting the property to false against Adwaita-dark
     * leaves the application exactly as dark as it was.
     *
     * Public so it can be tested; there is no other reason.
     */
    std::string baseThemeName (const std::string &name);

    /* thinkrc spelling, both ways. Unknown text reads as Auto rather than
     * failing: a preferences file from a newer version should not stop an
     * older one from starting. */
    gthThemeChoice fromString (const std::string &s);
    std::string    toString   (gthThemeChoice choice);
}

#endif /* GTH_THEME_H */
