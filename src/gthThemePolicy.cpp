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

/*
 * The theme decisions: no platform, no toolkit, no state.
 *
 * Split out of gthTheme.cpp rather than merely described as separable. The
 * header has always claimed this was "the only part that can be tested
 * everywhere", but themecheck was compiling the whole of gthTheme.cpp to
 * reach it -- which pulls in gtkmm, and on macOS pulls in CoreFoundation, and
 * duly failed to link a harness that uses neither:
 *
 *     Undefined symbols for architecture arm64:
 *       "_CFPreferencesCopyAppValue", referenced from: ...
 *
 * Now the claim is structural rather than aspirational. This file compiles
 * anywhere a C++ compiler does; the platform readers and everything that
 * touches Gtk::Settings stay next door.
 */

#include "gthTheme.h"

bool gthTheme::wantsDark (gthThemeChoice choice, gthColorScheme system)
{
    switch (choice)
    {
        case gthThemeChoice::Light:
            return false;

        case gthThemeChoice::Dark:
            return true;

        case gthThemeChoice::Auto:
            /* NoPreference means the desktop has not said, which is not the
               same as saying light -- but light is what GTK does by default,
               so it is the honest answer to "no opinion". */
            return system == gthColorScheme::Dark;
    }

    return false;
}

/* "Adwaita-dark" -> "Adwaita". Left alone if there is no such suffix. */
std::string gthTheme::baseThemeName (const std::string &name)
{
    static const char *suffixes[] = { "-dark", "-Dark", "-DARK", ":dark" };

    for (size_t i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); i++)
    {
        const std::string suffix = suffixes[i];

        if (name.size() > suffix.size() &&
            name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
            return name.substr(0, name.size() - suffix.size());
    }

    return name;
}

gthThemeChoice gthTheme::fromString (const std::string &s)
{
    if (s == "light")
        return gthThemeChoice::Light;

    if (s == "dark")
        return gthThemeChoice::Dark;

    return gthThemeChoice::Auto;
}

std::string gthTheme::toString (gthThemeChoice choice)
{
    switch (choice)
    {
        case gthThemeChoice::Light: return "light";
        case gthThemeChoice::Dark:  return "dark";
        case gthThemeChoice::Auto:  break;
    }

    return "auto";
}
