/*
 * Copyright (C) 2004-2026 The thinksynth authors
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
 * controllerformcheck -- the desktop's configurator form over the Index's
 * profile (src/gui/ControllerForm.h), on a profile file of its own.
 *
 * A card for each part of the controller says what it does; an edit is
 * written to the file, which is then read again, and the focus stays where
 * it was; one that wouldn't play is refused and the file left as it was;
 * Learn opens the card of the control moved, and says to while it waits;
 * the patch's block marks what it plays; a knob of the patch is what an
 * axis added rides; a number half typed when the form is drawn again is
 * dropped; and an edit over a file changed by hand and not read again is
 * refused.
 *
 * NEEDS A DISPLAY, like editorcheck, and skips itself without one.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <gtkmm.h>

#include "ControllerForm.h"
#include "thhIndex.h"
#include "thhProfile.h"

namespace fs = std::filesystem;

static int failures = 0;

static void
check (bool ok, const std::string &what, const std::string &detail = "")
{
    printf("%s  %s%s%s\n", ok ? "ok  " : "FAIL", what.c_str(),
           detail.empty() ? "" : ": ", detail.c_str());

    if (!ok)
        failures++;
}

static void
pump (void)
{
    for (int i = 0; i < 20; i++)
        while (g_main_context_iteration(NULL, FALSE))
            ;
}

static Gtk::Widget *
find (Gtk::Widget &w, const std::string &name)
{
    if (w.get_name().raw() == name)
        return &w;
    for (Gtk::Widget *c = w.get_first_child(); c; c = c->get_next_sibling())
        if (Gtk::Widget *found = find(*c, name))
            return found;
    return NULL;
}

static std::string
read (const fs::path &p)
{
    std::ifstream in(p);
    std::stringstream s;

    s << in.rdbuf();
    return s.str();
}

/* What a card says, its name and its line. */
static std::string
card (Gtk::Widget &form, const std::string &name)
{
    Gtk::Expander *e = dynamic_cast<Gtk::Expander *>(find(form, name));
    Gtk::Label *l = e ? dynamic_cast<Gtk::Label *>(e->get_label_widget()) : NULL;

    return l ? l->get_text().raw() : "";
}

/* The card named `name' opened, as a player opens it to change it. */
static void
open (Gtk::Widget &form, const std::string &name)
{
    if (Gtk::Expander *e = dynamic_cast<Gtk::Expander *>(find(form, name)))
        e->set_expanded(true);
    pump();
}

/* `text' typed into the number named `name', and Enter pressed. */
static void
type (Gtk::Widget &form, const std::string &name, const std::string &text)
{
    Gtk::Entry *e = dynamic_cast<Gtk::Entry *>(find(form, name));

    if (e == NULL)
        return;
    e->grab_focus();
    e->set_text(text);
    g_signal_emit_by_name(e->gobj(), "activate");
    pump();
}

static void
press (Gtk::Widget &form, const std::string &name)
{
    if (Gtk::Button *b = dynamic_cast<Gtk::Button *>(find(form, name)))
        g_signal_emit_by_name(b->gobj(), "clicked");
    pump();
}

static int
run (void)
{
    char dir[] = "/tmp/controllerformcheck.XXXXXX";

    if (mkdtemp(dir) == NULL)
        return 1;

    const fs::path path = fs::path(dir) / "hid" / "index";
    int reloads = 0;
    std::string played;
    ControllerForm form(thhIndexControls(), THH_INDEX_PROFILE, [&](std::string &err) {
        thHidBlock plays;

        reloads++;
        played = read(path);
        return thhLoadProfileText(played, "index", thhIndexControls(),
                                  THH_INDEX_PROFILE, "", plays, err);
    });
    Gtk::Window win;

    win.set_default_size(900, 700);
    win.set_child(form);
    win.present();
    form.setPath(path.string());
    pump();

    check(card(form, "A button") == "A button   press: note 0" &&
          card(form, "Trigger") == "Trigger   pull: strike 0" &&
          card(form, "System") == "System   nothing",
          "a card for each part of the controller, saying what it does",
          card(form, "A button") + " | " + card(form, "Trigger"));

    open(form, "A button");
    type(form, "a 1 semis", "5");
    check(read(path) == "device index {\n    a = note 5;\n};\n" && reloads == 1 &&
          played == read(path),
          "an edit is written to the player's file, which is read again", read(path));
    check(card(form, "A button") == "A button   press: note 5",
          "and the card says it", card(form, "A button"));

    Gtk::Widget *focus = win.get_focus();

    /* An entry's focus is its text's, inside it. */
    while (focus != NULL && focus->get_name() != "a 1 semis")
        focus = focus->get_parent();
    check(focus != NULL,
          "the focus is where it was, on the field drawn again",
          focus ? focus->get_name().raw() : "none");

    type(form, "a 1 semis", "200");

    Gtk::Label *problem = NULL;

    for (Gtk::Widget *w = form.get_first_child()->get_first_child(); w; w = w->get_next_sibling())
        if (Gtk::Label *l = dynamic_cast<Gtk::Label *>(w))
            problem = l;
    check(read(path) == "device index {\n    a = note 5;\n};\n" && reloads == 1 &&
          problem != NULL && !problem->get_text().empty(),
          "one that wouldn't play is refused, and says why, the file as it was",
          problem ? problem->get_text().raw() : "");

    /* Learn: B pressed after the controllers were read at rest. */
    const thhControls &c = thhIndexControls();
    std::vector<double> rest(c.buttons.size() + c.axes.size(), 0), b = rest;

    b[c.button("b")] = 1;
    form.values(rest);
    if (Gtk::ToggleButton *learn = dynamic_cast<Gtk::ToggleButton *>(
            form.get_first_child()->get_first_child()))
        learn->set_active(true);
    form.values(rest);
    form.setPatch("device index { };", {});
    pump();
    check(problem->get_text() == "Move the control you mean.",
          "Learn says to move a control, drawn again or not", problem->get_text());
    form.values(b);
    pump();

    Gtk::Expander *bCard = dynamic_cast<Gtk::Expander *>(find(form, "B button"));
    Gtk::Widget *bNow = find(form, "b now");

    check(bCard && bCard->get_expanded() && bNow &&
          bNow->get_parent()->get_parent()->has_css_class("accent"),
          "Learn opens the card of the button pressed, and marks it");

    form.setPatch("device index { b = note 3; };", {});
    pump();
    check(card(form, "B button") == "B button   press: the patch's",
          "what the patch's block plays, as the patch's", card(form, "B button"));

    form.setPatch("", { "cutoff" });
    pump();
    open(form, "Thumbstick");
    press(form, "Add stick_x");
    check(read(path).find("stick_x = \"cutoff\";") != std::string::npos,
          "an axis added rides the patch's knob", read(path));

    /* Tab from an option under More goes to the next option. */
    if (Gtk::Expander *m = dynamic_cast<Gtk::Expander *>(find(form, "stick_x 1 more")))
        m->set_expanded(true);
    pump();
    if (Gtk::Widget *channel = find(form, "stick_x 1 channel"))
        channel->grab_focus();
    win.child_focus(Gtk::DirectionType::TAB_FORWARD);

    Gtk::Widget *next = win.get_focus();

    while (next && next->get_name().raw().compare(0, 10, "stick_x 1 ") != 0)
        next = next->get_parent();
    check(next && next->get_name() != "stick_x 1 channel" && next->get_name() != "stick_x 1 more",
          "Tab from an option under More goes to the next option",
          next ? next->get_name().raw() : "none");

    const std::string before = read(path);

    if (Gtk::Entry *e = dynamic_cast<Gtk::Entry *>(find(form, "a 1 semis")))
    {
        e->grab_focus();
        e->set_text("4");
    }
    form.setPatch("device index { b = note 2; };", { "cutoff" });
    pump();
    check(read(path) == before, "a number half typed when the form is drawn again is dropped",
          read(path));

    std::ofstream(path) << "device index {\n    a = note 9;\n};\n";
    press(form, "Remove a 1");
    check(read(path) == "device index {\n    a = note 9;\n};\n" &&
          problem->get_text().find("Reload") != Glib::ustring::npos,
          "an edit over a file changed by hand, not read again, is refused",
          read(path) + problem->get_text().raw());
    press(form, "Remove a 1");
    check(read(path) == "device index {\n    a = note 9;\n};\n",
          "and so is the next, till it is read again", read(path));

    /* The profile a link to the player's own copy, as dotfiles keep it. */
    const fs::path real = fs::path(dir) / "index-real";

    fs::rename(path, real);
    fs::create_symlink(real, path);
    form.reloaded();
    pump();
    type(form, "a 1 semis", "6");
    check(fs::is_symlink(path) && read(real) == "device index {\n    a = note 6;\n};\n",
          "read again, it takes edits; a profile that is a link stays one, its file written",
          read(real));

    /* A profile there that can't be read is not one to write over. */
    fs::permissions(real, fs::perms::none);
    type(form, "a 1 semis", "7");
    fs::permissions(real, fs::perms::owner_read | fs::perms::owner_write);
    check(read(real) == "device index {\n    a = note 6;\n};\n" &&
          problem->get_text().find("can't read") != Glib::ustring::npos,
          "a profile that can't be read is refused, not written over",
          read(real) + problem->get_text().raw());

    /* And one that opens and then fails to read: a file that is memory. */
    fs::remove(path);
    fs::create_symlink("/proc/self/mem", path);
    form.reloaded();
    pump();
    type(form, "a 1 semis", "8");
    check(fs::is_symlink(path) && fs::read_symlink(path) == "/proc/self/mem" &&
          problem->get_text().find("can't read") != Glib::ustring::npos,
          "and one that opens and fails as it is read", problem->get_text().raw());

    fs::remove_all(dir);
    return failures;
}

int
main (int argc, char **argv)
{
    (void)argc;
    (void)argv;

    if (getenv("DISPLAY") == NULL && getenv("WAYLAND_DISPLAY") == NULL)
    {
        printf("skipped: no display (run under xvfb-run to exercise this)\n");
        return 0;
    }

    Glib::RefPtr<Gtk::Application> app =
        Gtk::Application::create("org.metaphonic.thinksynth.controllerformcheck",
                                 Gio::Application::Flags::NON_UNIQUE);
    int rc = 0;

    app->signal_activate().connect([&] { rc = run(); app->quit(); });
    app->run();
    printf("\n%d failure%s\n", rc, rc == 1 ? "" : "s");

    return rc;
}
