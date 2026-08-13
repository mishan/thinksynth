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
 * themecheck -- light or dark, given a choice and what the desktop says.
 *
 * The three platform readers in gthTheme.cpp can only be exercised on the
 * platform they read: a registry value on Windows, a global preference on
 * macOS, a D-Bus portal on Linux. Two of those three are written blind here
 * and CI cannot run either of them against a real desktop.
 *
 * What is the same everywhere is the decision made afterwards, and that is
 * where the mistakes that matter would be -- Auto ignoring the system, an
 * explicit choice being overridden by it, "no preference" being read as dark.
 * So the decision is a pure function with no platform and no toolkit in it,
 * and this pins every case of it.
 *
 * It also pins the thinkrc spelling round-trip, because a preference that
 * cannot be read back is a preference that silently resets.
 */

#include "gthTheme.h"

#include <cstdio>
#include <string>

static int failures = 0;

static const char *nameOf (gthThemeChoice c)
{
    switch (c)
    {
        case gthThemeChoice::Auto:  return "auto";
        case gthThemeChoice::Light: return "light";
        case gthThemeChoice::Dark:  return "dark";
    }

    return "?";
}

static const char *nameOf (gthColorScheme s)
{
    switch (s)
    {
        case gthColorScheme::NoPreference: return "no-preference";
        case gthColorScheme::Light:        return "light";
        case gthColorScheme::Dark:         return "dark";
    }

    return "?";
}

static void expect (gthThemeChoice choice, gthColorScheme system, bool dark)
{
    const bool got = gthTheme::wantsDark(choice, system);

    printf("  choice %-5s + system %-13s -> %-5s %s\n",
           nameOf(choice), nameOf(system), got ? "dark" : "light",
           got == dark ? "ok" : "FAILED");

    if (got != dark)
        failures++;
}

static void expectRoundTrip (const std::string &text, gthThemeChoice expected)
{
    const gthThemeChoice got = gthTheme::fromString(text);
    const bool ok = (got == expected);

    printf("  \"%s\" -> %-5s %s\n", text.c_str(), nameOf(got),
           ok ? "ok" : "FAILED");

    if (!ok)
        failures++;
}

int main (void)
{
    printf("the decision:\n");

    /* Auto is the only one that looks at the desktop. */
    expect(gthThemeChoice::Auto,  gthColorScheme::Dark,         true);
    expect(gthThemeChoice::Auto,  gthColorScheme::Light,        false);

    /* No preference is not dark. The portal returns it when the user has
       expressed no opinion, and GTK's own default is light, so following the
       toolkit is the honest answer rather than guessing. */
    expect(gthThemeChoice::Auto,  gthColorScheme::NoPreference, false);

    /* An explicit choice wins over the desktop, in both directions. This is
       the pair that would break if apply() ever consulted the system first. */
    expect(gthThemeChoice::Light, gthColorScheme::Dark,         false);
    expect(gthThemeChoice::Light, gthColorScheme::Light,        false);
    expect(gthThemeChoice::Light, gthColorScheme::NoPreference, false);
    expect(gthThemeChoice::Dark,  gthColorScheme::Light,        true);
    expect(gthThemeChoice::Dark,  gthColorScheme::Dark,         true);
    expect(gthThemeChoice::Dark,  gthColorScheme::NoPreference, true);

    printf("\nthe thinkrc spelling:\n");

    for (int i = 0; i < 3; i++)
    {
        const gthThemeChoice c = i == 0 ? gthThemeChoice::Auto
                               : i == 1 ? gthThemeChoice::Light
                                        : gthThemeChoice::Dark;

        expectRoundTrip(gthTheme::toString(c), c);
    }

    /* A file written by a future version, or damaged, or hand-edited. Auto is
       the safe reading: the program starts and follows the desktop rather
       than refusing to run over a preferences file. */
    expectRoundTrip("chartreuse", gthThemeChoice::Auto);
    expectRoundTrip("",           gthThemeChoice::Auto);
    expectRoundTrip("Dark",       gthThemeChoice::Auto);   /* case matters */

    printf("\n%s\n", failures ? "themecheck FAILED" : "themecheck ok");

    return failures ? 1 : 0;
}
