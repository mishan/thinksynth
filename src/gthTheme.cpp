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
 * Following the desktop's light/dark preference on three platforms.
 *
 * Each one keeps the answer somewhere different and announces changes
 * differently, and none of it is reachable through GTK:
 *
 *   Linux    the freedesktop appearance portal, over D-Bus, with the GNOME
 *            GSettings key as a fallback for a desktop with no portal.
 *   Windows  a registry DWORD, watched on a thread because the Win32 way to
 *            hear about it is a blocking wait.
 *   macOS    a global preference, watched through the distributed
 *            notification centre.
 *
 * The three readers share nothing but the enum they return. What they have in
 * common -- deciding what to do with the answer -- is wantsDark(), which is
 * deliberately free of all of this so that it can be tested anywhere.
 */

#include "config.h"

#include "gthTheme.h"

#include <gtkmm.h>
#include <giomm.h>

#if defined(_WIN32)
# include <windows.h>
# include <thread>
#elif defined(__APPLE__)
# include <CoreFoundation/CoreFoundation.h>
#endif

namespace
{
    gthThemeChoice currentChoice_ = gthThemeChoice::Auto;

    sigc::signal<void ()> schemeChanged_;

    bool watching_ = false;

    /* The theme name the desktop had before any of this touched it. */
    Glib::ustring systemThemeName_;
    bool          haveSystemTheme_ = false;

}

/* ------------------------------------------------------------------------
 * The decision
 * ------------------------------------------------------------------------ */

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

/* ------------------------------------------------------------------------
 * Linux: the freedesktop appearance portal
 *
 * org.freedesktop.portal.Settings.Read("org.freedesktop.appearance",
 * "color-scheme") answers 0 for no preference, 1 for dark and 2 for light --
 * note that 1 is dark, which is the opposite way round from every other
 * boolean in this file and is worth reading twice.
 *
 * The portal is used rather than GSettings because it is what works inside a
 * Flatpak without opening a hole in the sandbox, and because it is not tied
 * to GNOME. GSettings is the fallback for a desktop old enough to have no
 * portal.
 * ------------------------------------------------------------------------ */

#if !defined(_WIN32) && !defined(__APPLE__)

namespace
{
    Glib::RefPtr<Gio::DBus::Proxy> portal_;

    gthColorScheme schemeFromPortalValue (guint32 v)
    {
        switch (v)
        {
            case 1:  return gthColorScheme::Dark;
            case 2:  return gthColorScheme::Light;
            default: return gthColorScheme::NoPreference;
        }
    }

    Glib::RefPtr<Gio::DBus::Proxy> portalProxy (void)
    {
        if (portal_)
            return portal_;

        try
        {
            portal_ = Gio::DBus::Proxy::create_sync(
                Gio::DBus::Connection::get_sync(Gio::DBus::BusType::SESSION),
                "org.freedesktop.portal.Desktop",
                "/org/freedesktop/portal/desktop",
                "org.freedesktop.portal.Settings");
        }
        catch (const Glib::Error &)
        {
            /* No portal, no session bus, no desktop at all. All ordinary on a
               headless machine, and none of them worth a message. */
        }

        return portal_;
    }

    bool readPortal (gthColorScheme &out)
    {
        Glib::RefPtr<Gio::DBus::Proxy> p = portalProxy();

        if (!p)
            return false;

        try
        {
            Glib::VariantContainerBase args =
                Glib::VariantContainerBase::create_tuple({
                    Glib::Variant<Glib::ustring>::create("org.freedesktop.appearance"),
                    Glib::Variant<Glib::ustring>::create("color-scheme")});

            const Glib::VariantContainerBase r =
                p->call_sync("Read", args, 2000);

            /* Read returns (v), and the variant inside holds a uint32. */
            Glib::VariantBase inner;
            r.get_child(inner, 0);

            Glib::Variant<Glib::VariantBase> boxed =
                Glib::VariantBase::cast_dynamic<Glib::Variant<Glib::VariantBase> >(inner);

            Glib::Variant<guint32> v =
                Glib::VariantBase::cast_dynamic<Glib::Variant<guint32> >(boxed.get());

            out = schemeFromPortalValue(v.get());
            return true;
        }
        catch (const Glib::Error &)
        {
            return false;
        }
        catch (const std::bad_cast &)
        {
            return false;
        }
    }

    /* GNOME's own key, for a desktop with no portal. "prefer-dark",
       "prefer-light" or "default". */
    bool readGSettings (gthColorScheme &out)
    {
        const Glib::RefPtr<const Gio::SettingsSchemaSource> src =
            Gio::SettingsSchemaSource::get_default();

        if (!src)
            return false;

        if (!src->lookup("org.gnome.desktop.interface", true))
            return false;

        try
        {
            Glib::RefPtr<Gio::Settings> s =
                Gio::Settings::create("org.gnome.desktop.interface");

            const Glib::ustring v = s->get_string("color-scheme");

            if (v == "prefer-dark")
                out = gthColorScheme::Dark;
            else if (v == "prefer-light")
                out = gthColorScheme::Light;
            else
                out = gthColorScheme::NoPreference;

            return true;
        }
        catch (const Glib::Error &)
        {
            return false;
        }
    }

    gthColorScheme readPlatformScheme (void)
    {
        gthColorScheme s = gthColorScheme::NoPreference;

        if (readPortal(s))
            return s;

        if (readGSettings(s))
            return s;

        return gthColorScheme::NoPreference;
    }

    void onPortalSettingChanged (const Glib::ustring &,
                                 const Glib::ustring &signal,
                                 const Glib::VariantContainerBase &params)
    {
        if (signal != "SettingChanged")
            return;

        /* (namespace, key, value). Only one pair is of interest and the
           cheapest way to be sure is to re-read. */
        Glib::Variant<Glib::ustring> ns;
        params.get_child(ns, 0);

        if (ns.get() != "org.freedesktop.appearance")
            return;

        schemeChanged_.emit();
    }

    Glib::RefPtr<Gio::Settings> ifaceSettings_;

    void startPlatformWatch (void)
    {
        Glib::RefPtr<Gio::DBus::Proxy> p = portalProxy();

        if (p)
            p->signal_signal().connect(sigc::ptr_fun(&onPortalSettingChanged));

        /* And the GSettings key, for the desktop that answered the fallback
           rather than the portal. Without this, Auto on such a desktop reads
           the setting once at startup and then never notices it change --
           which is the half of "follow the system" that is easy to leave out,
           because it looks like it works.

           The reference is kept because a Gio::Settings that goes out of
           scope takes its subscription with it. */
        const Glib::RefPtr<const Gio::SettingsSchemaSource> src =
            Gio::SettingsSchemaSource::get_default();

        if (!src || !src->lookup("org.gnome.desktop.interface", true))
            return;

        try
        {
            ifaceSettings_ = Gio::Settings::create("org.gnome.desktop.interface");

            ifaceSettings_->signal_changed("color-scheme").connect(
                [](const Glib::ustring &) { schemeChanged_.emit(); });
        }
        catch (const Glib::Error &)
        {
        }
    }
}

#endif /* Linux */

/* ------------------------------------------------------------------------
 * Windows: HKCU\...\Themes\Personalize\AppsUseLightTheme
 *
 * A DWORD that is 0 for dark and 1 for light -- again the opposite sense to
 * the name one might expect to find. Absent on Windows versions old enough
 * not to have the setting, which reads as no preference.
 *
 * Changes are heard by a thread parked in RegNotifyChangeKeyValue, because
 * the alternative is WM_SETTINGCHANGE and there is no window procedure of
 * ours to put it in. The thread never touches GTK: it wakes a
 * Glib::Dispatcher, which is the same way MIDI input reaches the GUI thread
 * in this program.
 * ------------------------------------------------------------------------ */

#if defined(_WIN32)

namespace
{
    Glib::Dispatcher *winDispatch_ = NULL;

    const wchar_t *kPersonalize =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";

    gthColorScheme readPlatformScheme (void)
    {
        DWORD value = 1;
        DWORD size  = sizeof(value);

        const LSTATUS rc = RegGetValueW(HKEY_CURRENT_USER, kPersonalize,
                                        L"AppsUseLightTheme",
                                        RRF_RT_REG_DWORD, NULL, &value, &size);

        if (rc != ERROR_SUCCESS)
            return gthColorScheme::NoPreference;

        return value == 0 ? gthColorScheme::Dark : gthColorScheme::Light;
    }

    void watchThread (void)
    {
        HKEY key = NULL;

        if (RegOpenKeyExW(HKEY_CURRENT_USER, kPersonalize, 0, KEY_NOTIFY, &key)
            != ERROR_SUCCESS)
            return;

        for (;;)
        {
            /* Blocking wait: returns when anything under the key changes,
               and has to be re-armed each time. */
            if (RegNotifyChangeKeyValue(key, FALSE, REG_NOTIFY_CHANGE_LAST_SET,
                                        NULL, FALSE) != ERROR_SUCCESS)
                break;

            if (winDispatch_ != NULL)
                winDispatch_->emit();
        }

        RegCloseKey(key);
    }

    void startPlatformWatch (void)
    {
        winDispatch_ = new Glib::Dispatcher;

        winDispatch_->connect([]() {
            schemeChanged_.emit();
        });

        std::thread(watchThread).detach();
    }
}

#endif /* _WIN32 */

/* ------------------------------------------------------------------------
 * macOS: the AppleInterfaceStyle global preference
 *
 * Present and equal to "Dark" in dark mode, and simply absent in light mode
 * -- there is no "Light" value to compare against, which is why this reads as
 * a presence test rather than a comparison.
 *
 * CoreFoundation only, no Objective-C, so this stays an ordinary .cpp.
 * ------------------------------------------------------------------------ */

#if defined(__APPLE__)

namespace
{
    gthColorScheme readPlatformScheme (void)
    {
        CFPropertyListRef v =
            CFPreferencesCopyAppValue(CFSTR("AppleInterfaceStyle"),
                                      kCFPreferencesAnyApplication);

        if (v == NULL)
            return gthColorScheme::Light;

        gthColorScheme s = gthColorScheme::Light;

        if (CFGetTypeID(v) == CFStringGetTypeID() &&
            CFStringCompare((CFStringRef)v, CFSTR("Dark"), 0) == kCFCompareEqualTo)
            s = gthColorScheme::Dark;

        CFRelease(v);

        return s;
    }

    void appearanceChanged (CFNotificationCenterRef, void *, CFStringRef,
                            const void *, CFDictionaryRef)
    {
        /* Distributed notifications arrive on the main run loop, which under
           GTK on macOS is the one the GUI is on. */
        schemeChanged_.emit();
    }

    void startPlatformWatch (void)
    {
        CFNotificationCenterAddObserver(
            CFNotificationCenterGetDistributedCenter(),
            NULL,
            appearanceChanged,
            CFSTR("AppleInterfaceThemeChangedNotification"),
            NULL,
            CFNotificationSuspensionBehaviorDeliverImmediately);
    }
}

#endif /* __APPLE__ */

/* ------------------------------------------------------------------------
 * The part that is the same everywhere
 * ------------------------------------------------------------------------ */

/* Asked afresh every time rather than cached.
 *
 * The first version cached the answer and relied on the change notification
 * to invalidate it, which is one more thing to be wrong for no gain: this is
 * called at startup and when the desktop says something changed, and reading
 * a registry value or a GSettings key costs nothing at that rate. Testing it
 * found the cache immediately -- a second read returned the first answer
 * however the setting had moved.
 */
gthColorScheme gthTheme::systemScheme (void)
{
    return readPlatformScheme();
}

sigc::signal<void ()> &gthTheme::signalSystemSchemeChanged (void)
{
    return schemeChanged_;
}

void gthTheme::startWatching (void)
{
    if (watching_)
        return;

    watching_ = true;

    startPlatformWatch();
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

void gthTheme::apply (gthThemeChoice choice)
{
    currentChoice_ = choice;

    Glib::RefPtr<Gtk::Settings> settings = Gtk::Settings::get_default();

    if (!settings)
        return;   /* no display; nothing to theme */

    /* The desktop's own theme, taken the first time through and before
       anything here has changed it, because Auto has to put it back. */
    if (!haveSystemTheme_)
    {
        systemThemeName_ = settings->property_gtk_theme_name();
        haveSystemTheme_ = true;
    }

    /* Setting the property alone is not enough, and this is the whole reason
       the theme name is touched at all.
     *
     * gtk-application-prefer-dark-theme selects the dark *variant* of a theme
     * that ships one. It has no power over a theme that is dark by name -- and
     * a desktop set to dark commonly sets gtk-theme-name to exactly that:
     * Adwaita-dark, Yaru-dark, Breeze-Dark. Against one of those, asking for
     * light changed nothing at all; the application stayed dark and the menu
     * looked broken. Measured rather than guessed: with gtk-theme-name set to
     * Adwaita-dark, prefer-dark=false still gives 0.93 foreground text.
     *
     * So an explicit choice moves to the base theme, where the variant
     * property means something. Auto puts the desktop's own name back, since
     * on Auto the desktop is the one deciding. */
    if (choice == gthThemeChoice::Auto)
        settings->property_gtk_theme_name() = systemThemeName_;
    else
        settings->property_gtk_theme_name() = baseThemeName(systemThemeName_);

    settings->property_gtk_application_prefer_dark_theme() =
        wantsDark(choice, systemScheme());
}

gthThemeChoice gthTheme::current (void)
{
    return currentChoice_;
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
