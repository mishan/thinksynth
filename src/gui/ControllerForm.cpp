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

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>

#include "Access.h"
#include "ControllerForm.h"
#include "thhProfileEdit.h"

namespace fs = std::filesystem;

namespace {

const char *const LOOPS[] = { "record", "undo", "clear" };

/* How far an axis moves for Learn to take it as the one meant: past a
   hand's shake, short of a deliberate move. */
const double LEARN_MOVE = 0.25;

std::string
num (double v)
{
    char buf[32];

    snprintf(buf, sizeof(buf), "%.15g", v);
    return buf;
}

/* `w' known by `name', to a screen reader and to the form, which finds it
   again by it once it is drawn afresh. */
template <class W>
W *
named (W *w, const std::string &name)
{
    w->set_name(name);
    nameAs(*w, name);
    return w;
}

const thhFormKind *
kindOf (thHidBinding::Kind k)
{
    for (const thhFormKind &f : thhFormKinds())
        if (f.kind == k)
            return &f;
    return NULL;
}

/* A binding in a few words: its action and what it is of. */
std::string
said (const thHidBinding &b)
{
    const thhFormKind *k = kindOf(b.kind);
    const std::string param = k ? k->param : "";
    const char *scale = b.scale.empty() ? NULL : thHidScaleName(b.scale);

    if (b.kind == thHidBinding::VALUE)
        return b.arg;
    return std::string(k ? k->name : "") +
           (param == "semis" ? " " + std::to_string(b.semis)
            : param == "layer" ? " " + b.layer
            : param == "arg" ? " " + b.arg : "") +
           (scale ? std::string(", ") + scale : "");
}

/* What a control does in a few words: for one bound only in a layer or
   for one hand, where. */
std::string
whatItDoes (const thhFormControl &c)
{
    std::string out;

    if (c.patched)
        return "the patch's";
    if (c.off)
        return "off";
    for (const thHidBinding &b : c.bindings)
        out += (out.empty() ? "" : " + ") + said(b);
    if (out.empty())
    {
        std::set<std::string> where;

        for (const std::string &e : c.elsewhere)
            if (where.insert(e.substr(0, e.find(':'))).second)
                out += (out.empty() ? "in " : ", ") + e.substr(0, e.find(':'));
    }
    return out;
}

/* The descendant of `w' named `name', or NULL. */
Gtk::Widget *
find (Gtk::Widget &w, const std::string &name)
{
    if (w.get_name() == name)
        return &w;
    for (Gtk::Widget *c = w.get_first_child(); c; c = c->get_next_sibling())
        if (Gtk::Widget *found = find(*c, name))
            return found;
    return NULL;
}

/* The first of `w' and its descendants that takes the focus. */
bool
focusFirst (Gtk::Widget &w)
{
    if (w.get_focusable() && w.grab_focus())
        return true;
    for (Gtk::Widget *c = w.get_first_child(); c; c = c->get_next_sibling())
        if (focusFirst(*c))
            return true;
    return false;
}

/* A drop-down of `items', `value' picked. */
Gtk::DropDown *
choice (const std::vector<std::string> &items, const std::string &value)
{
    std::vector<Glib::ustring> shown(items.begin(), items.end());
    Gtk::DropDown *d = Gtk::make_managed<Gtk::DropDown>(shown);

    for (size_t i = 0; i < items.size(); i++)
        if (items[i] == value)
            d->set_selected((guint)i);
    return d;
}

}

ControllerForm::ControllerForm (const thhControls &controls, const char *own,
                                std::function<bool (std::string &)> reload)
    : Gtk::Box(Gtk::Orientation::VERTICAL, 4), controls_(controls), own_(own),
      reload_(reload), top_(Gtk::Orientation::HORIZONTAL, 6),
      learn_("_Learn", true), cards_(Gtk::Orientation::VERTICAL, 2),
      learnOn_(false), backUp_(false), drawing_(false)
{
    problem_.set_xalign(0);
    problem_.set_wrap(true);
    problem_.set_wrap_mode(Pango::WrapMode::WORD_CHAR);
    problem_.add_css_class("dim-label");
    learn_.set_tooltip_text("The next control moved is the one shown");
    learn_.signal_toggled().connect(sigc::mem_fun(*this, &ControllerForm::onLearn));
    top_.append(learn_);
    top_.append(problem_);
    append(top_);
    append(cards_);
}

void
ControllerForm::setPath (const std::string &path)
{
    path_ = path;
    draw();
}

void
ControllerForm::setPatch (const std::string &block, const std::vector<std::string> &knobs)
{
    if (block == patch_ && knobs == knobs_)
        return;
    patch_ = block;
    knobs_ = knobs;
    draw();
}

std::string
ControllerForm::text () const
{
    std::ifstream in(path_);
    std::stringstream s;

    s << in.rdbuf();
    return s.str();
}

/* Made to the file, which is then read again; drawn afresh once the
   signal that asked for it is over, as its widget goes with the rows. */
void
ControllerForm::edit (const std::string &op, const std::string &control, int which,
                      const std::string &fields)
{
    std::string t = text(), err;
    std::error_code ec;

    /* The file changed since it was drawn, by hand, say: the form's rows
       aren't its, and the change isn't played yet. */
    if (t != shown_)
        err = "Your profile has changes; Reload them first.";
    else if (thhFormEdit(t, controls_, own_, op, control, which, fields, err))
    {
        /* Beside the file a link names, so the link stays one. */
        fs::path path = fs::weakly_canonical(path_, ec);

        if (ec)
            path = path_;

        const fs::path part = path.string() + ".part";

        fs::create_directories(path.parent_path(), ec);
        {
            std::ofstream out(part, std::ios::binary);

            out << t;
            if (!out.flush())
                err = "can't write " + part.string();
        }
        if (err.empty())
            fs::rename(part, path, ec);
        if (err.empty() && ec)
            err = "can't write " + path.string() + ": " + ec.message();
        if (err.empty() && !reload_(err))
            err = "your profile: " + err;
    }
    Glib::signal_idle().connect_once(sigc::track_obj([this, err] { draw(err); }, *this));
}

Gtk::Widget *
ControllerForm::binding (const thhForm &f, const thhFormControl &c,
                         const thHidBinding &b, int which)
{
    const std::string name = c.control + " " + std::to_string(which + 1);
    const thhFormKind *kind = kindOf(b.kind);
    Gtk::Box *row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
    Gtk::Box *line = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    std::vector<std::string> kinds;

    row->append(*line);

    /* What the device plays, and its own kind, if it doesn't play it: it
       is still what the text says. */
    for (const thhFormKind &k : thhFormKinds())
        if ((c.axis ? k.axis : k.button) &&
            (k.kind == b.kind ||
             std::find(f.cannot.begin(), f.cannot.end(), k.name) == f.cannot.end()))
            kinds.push_back(k.name);

    const auto set = [this, c, which](const std::string &fields) {
        edit("set", c.control, which, fields);
    };
    const auto field = [&](Gtk::Widget *w, const std::string &key) {
        return named(w, name + " " + key);
    };
    /* A number, changed once Enter is pressed or the focus leaves it. */
    const auto number = [&](const std::string &key, const std::string &value) {
        Gtk::Entry *e = Gtk::make_managed<Gtk::Entry>();
        auto focus = Gtk::EventControllerFocus::create();

        e->set_text(value);
        e->set_width_chars(6);
        e->set_placeholder_text("none");
        e->set_input_purpose(Gtk::InputPurpose::NUMBER);
        /* Once: the field goes as the form is drawn again, and its focus
           leaving then is no second change. */
        const auto sent = std::make_shared<bool>(false);
        const auto changed = [this, e, key, value, set, sent] {
            /* A number half typed when the form is drawn again is dropped,
               as the page drops it. */
            if (!*sent && !drawing_ && e->get_text().raw() != value)
            {
                *sent = true;
                set(key + "=" + e->get_text().raw());
            }
        };
        e->signal_activate().connect(changed);
        focus->signal_leave().connect(changed);
        e->add_controller(focus);
        return field(e, key);
    };

    Gtk::DropDown *action = choice(kinds, kind->name);

    field(action, "action");
    action->property_selected().signal_changed().connect([this, action, kinds, set] {
        const std::string k = kinds[action->get_selected()];

        set("kind=" + k + (k == "value" && !knobs_.empty() ? "\narg=" + knobs_[0] : ""));
    });
    line->append(*action);

    const std::string param = kind->param;

    if (param == "semis")
        line->append(*number("semis", std::to_string(b.semis)));
    else if (param == "layer" || param == "arg")
    {
        std::vector<std::string> items = param == "layer" ? f.layers
            : b.kind == thHidBinding::LOOP ? std::vector<std::string>(std::begin(LOOPS), std::end(LOOPS))
            : knobs_;
        const std::string now = param == "layer" ? b.layer : b.arg;

        if (std::find(items.begin(), items.end(), now) == items.end() && !now.empty())
            items.insert(items.begin(), now);

        Gtk::DropDown *d = choice(items, now);

        field(d, param);
        d->property_selected().signal_changed().connect([d, items, param, set] {
            set(param + "=" + items[d->get_selected()]);
        });
        line->append(*d);
    }

    Gtk::Button *remove = named(Gtk::make_managed<Gtk::Button>("Remove"), "Remove " + name);

    remove->signal_clicked().connect([this, c, which] { edit("remove", c.control, which); });
    line->append(*remove);

    /* Its options, under More. */
    static const char *const OPTIONS[] = {
        "channel", "min", "max", "at", "off", "step", "scale", "range",
        "center", "smooth", "shape", "both", "latch" };
    Gtk::Expander *more = named(Gtk::make_managed<Gtk::Expander>("More"), name + " more");
    Gtk::FlowBox *opts = Gtk::make_managed<Gtk::FlowBox>();
    bool any = false;

    opts->set_selection_mode(Gtk::SelectionMode::NONE);
    opts->set_focusable(false);
    for (const char *key : OPTIONS)
    {
        if (!thhTakes(b.kind, key))
            continue;
        any = true;

        Gtk::Box *opt = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
        const std::string k = key;

        opt->append(*Gtk::make_managed<Gtk::Label>(k));
        if (k == "latch" || k == "both")
        {
            Gtk::CheckButton *box = Gtk::make_managed<Gtk::CheckButton>();

            box->set_active(k == "latch" ? b.latch : b.both);
            box->signal_toggled().connect([box, k, set] {
                set(k + "=" + (box->get_active() ? "1" : "0"));
            });
            opt->append(*field(box, k));
        }
        else if (k == "step" || k == "shape" || k == "scale")
        {
            std::vector<std::string> items = k == "step"
                ? std::vector<std::string>{ "semitone", "octave" }
                : k == "shape" ? std::vector<std::string>{ "linear", "exp" }
                : std::vector<std::string>{ "none" };
            const char *scale = b.scale.empty() ? NULL : thHidScaleName(b.scale);
            const std::string now = k == "step" ? (b.octaves ? "octave" : "semitone")
                : k == "shape" ? (b.shape == thHidBinding::EXP ? "exp" : "linear")
                : scale ? scale : "none";

            if (k == "scale")
                for (const std::string &s : thHidScaleNames())
                    items.push_back(s);

            Gtk::DropDown *d = choice(items, now);

            d->property_selected().signal_changed().connect([d, items, k, set] {
                const std::string v = items[d->get_selected()];

                set(k + "=" + (v == "none" ? "" : v));
            });
            opt->append(*field(d, k));
        }
        else
        {
            const bool none = (k == "min" && !b.hasMin) || (k == "max" && !b.hasMax);
            const double v = k == "channel" ? b.channel : k == "min" ? b.min
                : k == "max" ? b.max : k == "at" ? b.at : k == "off" ? b.off
                : k == "range" ? b.range : k == "center" ? b.center : b.smooth;

            opt->append(*number(k, none ? "" : num(v)));
        }
        opts->append(*opt);
        /* Tab goes to the option, not to the flow box's child around it. */
        opt->get_parent()->set_focusable(false);
    }
    if (any)
    {
        more->set_child(*opts);
        more->set_expanded(open_.count(name + " more") > 0);
        more->property_expanded().signal_changed().connect([this, more, name] {
            if (more->get_expanded())
                open_.insert(name + " more");
            else
                open_.erase(name + " more");
        });
        row->append(*more);
    }
    return row;
}

Gtk::Widget *
ControllerForm::control (const thhForm &f, const thhFormControl &c, Gtk::Label *&now)
{
    Gtk::Box *box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
    Gtk::Box *head = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    Gtk::Label *role = Gtk::make_managed<Gtk::Label>(c.role);
    Gtk::Label *whose = Gtk::make_managed<Gtk::Label>(
        (c.patched ? "the patch's plays over " : "") +
        (c.off ? std::string("off") : c.yours ? "yours"
         : !c.bindings.empty() ? f.own : "nothing"));
    Gtk::Box *buttons = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);

    now = named(Gtk::make_managed<Gtk::Label>("-"), c.control + " now");
    box->set_margin_start(12);
    role->add_css_class("heading");
    whose->add_css_class("dim-label");
    head->append(*role);
    head->append(*now);
    head->append(*whose);
    box->append(*head);
    for (size_t i = 0; i < c.bindings.size(); i++)
        box->append(*binding(f, c, c.bindings[i], (int)i));
    for (const std::string &e : c.elsewhere)
    {
        Gtk::Label *l = Gtk::make_managed<Gtk::Label>(e);

        l->set_xalign(0);
        l->set_wrap(true);
        l->add_css_class("dim-label");
        box->append(*l);
    }

    const auto button = [&](const char *label, const std::string &op, const std::string &fields) {
        Gtk::Button *b = named(Gtk::make_managed<Gtk::Button>(label),
                               std::string(label) + " " + c.control);

        b->signal_clicked().connect([this, c, op, fields] { edit(op, c.control, -1, fields); });
        buttons->append(*b);
    };

    /* A button does one thing. An axis is first a knob, where there are
       knobs and the device plays them. */
    std::string first;

    for (const thhFormKind &k : thhFormKinds())
        if ((c.axis ? k.axis : k.button) && first.empty() &&
            std::find(f.cannot.begin(), f.cannot.end(), k.name) == f.cannot.end() &&
            (k.kind != thHidBinding::VALUE || !knobs_.empty()))
            first = k.name;
    if ((c.axis || c.bindings.empty()) && !first.empty())
        button("Add", "add", "kind=" + first + (first == "value" ? "\narg=" + knobs_[0] : ""));
    if (c.yours || c.off)
        button("Reset", "reset", "");
    if (!c.off && !c.bindings.empty())
        button("Off", "off", "");
    box->append(*buttons);
    return box;
}

void
ControllerForm::draw (const std::string &why)
{
    /* The control the focus is on, by its name: an entry's is its text's,
       inside it. */
    Gtk::Root *root = get_root();
    Gtk::Widget *had = root ? root->get_focus() : NULL;

    while (had && had != this && (had->get_name().empty() || !find(*this, had->get_name()) ||
                                  had->get_name().raw().compare(0, 3, "Gtk") == 0 ||
                                  had->get_name().raw().compare(0, 7, "gtkmm__") == 0))
        had = had->get_parent();

    const std::string focused = had && had != this ? had->get_name().raw() : "";
    shown_ = text();

    const thhForm f = thhFormOf(controls_, own_, shown_, patch_);

    say(!f.problem.empty() ? f.problem : !why.empty() ? why
        : learnOn_ ? "Move the control you mean." : "");
    backUp_ = f.backUp;
    /* Until the focus is back: the old rows' focus leaves them as it
       comes. */
    drawing_ = true;
    while (Gtk::Widget *w = cards_.get_first_child())
        cards_.remove(*w);
    drawn_.clear();

    /* The cards, in the order their first control comes. */
    std::vector<std::string> order;
    std::map<std::string, std::vector<const thhFormControl *>> on;

    for (const thhFormControl &c : f.controls)
    {
        if (on[c.card].empty())
            order.push_back(c.card);
        on[c.card].push_back(&c);
    }
    for (const std::string &card : order)
    {
        std::string line;

        for (const thhFormControl *c : on[card])
            if (c->patched || c->off || !c->bindings.empty() || !c->elsewhere.empty())
                line += (line.empty() ? "" : "   ") + c->role + ": " + whatItDoes(*c);

        Gtk::Expander *e = named(Gtk::make_managed<Gtk::Expander>(), card);

        describeAs(*e, line.empty() ? "nothing" : line);
        Gtk::Box *inside = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
        Gtk::Label *label = Gtk::make_managed<Gtk::Label>();

        label->set_wrap(true);
        label->set_xalign(0);
        label->set_markup("<b>" + Glib::Markup::escape_text(card) + "</b>   " +
                          Glib::Markup::escape_text(line.empty() ? "nothing" : line));
        e->set_label_widget(*label);
        for (const thhFormControl *c : on[card])
        {
            Gtk::Label *now = NULL;
            Gtk::Widget *w = control(f, *c, now);

            inside->append(*w);
            drawn_.push_back({ c->control, card, c->axis, now, (Gtk::Box *)w });
        }
        e->set_child(*inside);
        e->set_expanded(open_.count(card) > 0);
        e->property_expanded().signal_changed().connect([this, e, card] {
            if (e->get_expanded())
                open_.insert(card);
            else
                open_.erase(card);
        });
        cards_.append(*e);
    }

    /* drawn_ in the form's order, which its values come in. */
    std::vector<Drawn> ordered;

    for (const thhFormControl &c : f.controls)
        for (const Drawn &d : drawn_)
            if (d.control == c.control)
                ordered.push_back(d);
    drawn_ = ordered;

    if (!focused.empty())
        if (Gtk::Widget *w = find(*this, focused))
            w->grab_focus();
    drawing_ = false;
}

/* Shown, and said to a screen reader, as the page's line is. */
void
ControllerForm::say (const std::string &text)
{
    if (problem_.get_text().raw() == text)
        return;
    problem_.set_text(text);
    if (!text.empty())
        ::announce(problem_, text);
}

void
ControllerForm::onLearn ()
{
    learnOn_ = learn_.get_active();
    learning_.clear();
    say(learnOn_ ? "Move the control you mean." : "");
}

void
ControllerForm::values (const std::vector<double> &v)
{
    const bool here = v.size() == drawn_.size();

    for (size_t i = 0; i < drawn_.size(); i++)
    {
        const std::string t = !here ? "-" : drawn_[i].axis ? num(std::round(v[i] * 100) / 100)
                            : v[i] != 0 ? "down" : "up";

        if (drawn_[i].now->get_text().raw() != t)
            drawn_[i].now->set_text(t);
    }
    if (!learnOn_)
        return;
    /* A device gone and come back is a new start. */
    if (!here)
    {
        learning_.clear();
        return;
    }
    if (learning_.empty())
    {
        learning_ = v;
        for (size_t i = 0; backUp_ && i < v.size(); i++)
            if (!drawn_[i].axis)
                learning_[i] = 0;
    }
    for (size_t i = 0; i < v.size(); i++)
        if (std::fabs(v[i] - learning_[i]) > (drawn_[i].axis ? LEARN_MOVE : 0.5))
        {
            const Drawn d = drawn_[i];

            learn_.set_active(false);
            say("");
            for (const Drawn &o : drawn_)
                o.box->remove_css_class("accent");
            open_.insert(d.card);
            if (Gtk::Widget *card = find(*this, d.card))
                ((Gtk::Expander *)card)->set_expanded(true);
            d.box->add_css_class("accent");
            focusFirst(*d.box);
            return;
        }
}
